#include <Arduino_GFX_Library.h>
#include <arduinoFFT.h>
#include <WiFiS3.h>

// CA SSL root certificate to verify HTTPS server
const char* flask_ca = \
"-----BEGIN CERTIFICATE-----\n" \

"-----END CERTIFICATE-----\n";

// Pins & constants
#define SAMPLES 64
#define SAMPLING_FREQUENCY 8000
#define MIC_PIN A0
#define TFT_CS 10
#define TFT_DC 8
#define TFT_RST 9

// Wi‑Fi & server
const char* ssid = "Your Wi-Fi";
const char* password = "Password";
const char* host = "Server";
const uint16_t httpsPort = 443; // Default port
const String endpoint = "/function";

// WAV recording params
const int sampleRate = 4000; // Hz
const float totalSeconds = 2;
const int totalSamples = (int)(sampleRate * totalSeconds);
uint8_t audioBuffer[totalSamples];
int sampleIndex = 0;

// Timing
unsigned long samplingPeriod_us;
unsigned long lastSampleMicros = 0;

// Detection thresholds
double backgroundEnergy = 0;
const double alpha = 0.05, thresholdMultiplier = 2.5;
#define ENERGY_HISTORY_SIZE 5
double energyHistory[ENERGY_HISTORY_SIZE] = {0};
int energyIndex = 0;

// State flags
bool isRecording = false, isSending = false;
unsigned long lastDetection = 0;
const unsigned long cooldownMs = 1000;
String lastStatus = "", lastPrediction = "";
bool predictionUpdated = false; // For displaying purposes

// Grace period parameters
bool gracePeriodPassed = false;
unsigned long gracePeriodStartTime = 0;
const unsigned long gracePeriodDuration = 10000; // 10 s

// TFT display (adjust according to hardware)
Arduino_DataBus *bus = new Arduino_HWSPI(TFT_DC, TFT_CS);
Arduino_GFX *gfx = new Arduino_ILI9341(bus, TFT_RST);

// FFT initialising
double vReal[SAMPLES], vImag[SAMPLES];
ArduinoFFT<double> FFT(vReal, vImag, SAMPLES, SAMPLING_FREQUENCY);

// Wi‑Fi
WiFiSSLClient wifiSSL;

void setup() {
  Serial.begin(115200);
  delay(1000);

  // Start screen
  gfx->begin();
  gfx->setRotation(3); // Landscape for this specific display
  gfx->fillScreen(BLACK);
  gfx->setTextSize(2);
  gfx->setTextColor(WHITE, BLACK);
  gfx->setCursor(90, 10);
  gfx->print("Noise Level");

  samplingPeriod_us = round(1e6 / SAMPLING_FREQUENCY);

  WiFi.begin(ssid, password);
  Serial.print("Connecting to WiFi");
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println("\nWiFi connected!");
  Serial.print("IP address: ");
  Serial.println(WiFi.localIP());

  gracePeriodStartTime = millis();
  gracePeriodPassed = false;
  Serial.println("Grace period started.");

  gfx->setCursor(10, 110);
  gfx->setTextSize(2);
  gfx->setTextColor(YELLOW, BLACK);
  gfx->print("Prediction: ---"); // No prediction yet

}

// Running loop
void loop() {
  unsigned long now = millis();

  // Grace period to learn background noises
  if (!gracePeriodPassed) {
    double e = fetchEnergy();
    renderDisplay(e, e > backgroundEnergy * thresholdMultiplier);
    if (now - gracePeriodStartTime >= gracePeriodDuration) {
      gracePeriodPassed = true;
      Serial.println("Grace period done.");
    }
    return;
  }

  // Post-grace: start detecting cough
  if (!isRecording && !isSending) {
    double e = fetchEnergy();
    bool cough = e > backgroundEnergy * thresholdMultiplier && isSustainedEnergy(); // A cough is when current energy reaches higher than normal and
    renderDisplay(e, cough);                                                        // sustained energy for a short period (sudden spike)

    // Start recording if cough detection is triggered
    if (cough && now - lastDetection > cooldownMs) {
      lastDetection = now;
      Serial.println("Cough detected - start recording WAV.");
      isRecording = true;
      sampleIndex = 0;
      lastSampleMicros = micros();
    }
  }

  // Record WAV into buffer
  if (isRecording) recordWavSample();

  // Send WAV when ready
  if (isSending) sendWavFile();

  if (predictionUpdated) {
    renderPrediction();  // Refresh LCD
    predictionUpdated = false;
  }
}

// Combined audio sampling + FFT + energy tracking
// Compute how much sound energy lies in the human voice range
double fetchEnergy() {
  for (int i = 0; i < SAMPLES; i++) { // Read mic samples
    unsigned long t0 = micros();
    vReal[i] = analogRead(MIC_PIN);
    vImag[i] = 0;
    while (micros() - t0 < samplingPeriod_us);
  }

  // Apply windowing + FFT
  FFT.windowing(vReal, SAMPLES, FFT_WIN_TYP_HAMMING, FFT_FORWARD);
  FFT.compute(vReal, vImag, SAMPLES, FFT_FORWARD);
  FFT.complexToMagnitude(vReal, vImag, SAMPLES);

  // Compute total energy within range
  double sum = 0;
  for (int i = 3; i < SAMPLES / 2; i++) {
    double freq = i * SAMPLING_FREQUENCY / SAMPLES;
    if (freq >= 300 && freq <= 2000) sum += vReal[i]; // 300-2000 Hz is suitable for cough detection
  }

  backgroundEnergy = (1 - alpha) * backgroundEnergy + alpha * sum; // Adaptive background noise estimation
  // Store recent energy lvls for sustained detection (constaint high lvl noises)
  energyHistory[energyIndex] = sum > backgroundEnergy * thresholdMultiplier ? sum : 0;
  energyIndex = (energyIndex + 1) % ENERGY_HISTORY_SIZE;

  return sum;
}

bool isSustainedEnergy() {
  int count = 0;
  for (int i = 0; i < ENERGY_HISTORY_SIZE; i++)
    if (energyHistory[i] > 0) count++;
  return count >= 2; // At least 2 high-energy frames
}

void renderDisplay(double energy, bool cough) {
  const int x = 0, y = 40, w = 320, h = 15;
  // Bar for noise lvl
  gfx->fillRect(x, y, w, h, DARKGREY);
  int len = constrain((int)(energy / 6000.0 * w), 0, w);
  uint16_t c = cough ? BLUE : GREEN;
  gfx->fillRect(x, y, len, h, c);

  // Status text
  String st = cough ? "Cough Detected" : "Ambient Noise";
  if (st != lastStatus) {
    gfx->fillRect(x, y + h + 10, w, 20, BLACK);
    gfx->setCursor(x + 10, y + h + 10);
    gfx->setTextSize(2);
    gfx->setTextColor(c, BLACK);
    gfx->print(st);
    lastStatus = st;
  }
}

void renderPrediction() {
  const int x = 0, y = 110, w = 320, h = 40;
  
  // Clear prediction area
  gfx->fillRect(x, y, w, h, BLACK);

  // Print title
  gfx->setCursor(x + 10, y);
  gfx->setTextSize(2);
  gfx->setTextColor(WHITE, BLACK);
  gfx->print("Prediction:");

  // Print prediction text below the title
  gfx->setCursor(x + 10, y + 20);
  gfx->setTextSize(2);
  gfx->setTextColor(YELLOW, BLACK);
  gfx->print(lastPrediction);
}


void recordWavSample() {
  unsigned long now = micros();
  if (now - lastSampleMicros >= (1000000 / sampleRate)) {
    lastSampleMicros = now;
    audioBuffer[sampleIndex++] = analogRead(MIC_PIN) >> 2; // 10-bit to 8-bit

    if (sampleIndex >= totalSamples) {
      isRecording = false;
      isSending = true; 
      Serial.println("Recording complete.");
    }
  }
}

  void sendWavFile() {
    Serial.println("Sending WAV file...");
    wifiSSL.setCACert(flask_ca); // CA cert is needed for HTTPS

    // HTTPS connection
    if (!wifiSSL.connect(host, httpsPort)) {
      Serial.println("[!] HTTPS connect failed.");
      isSending = false;
      return;
    }

    // WAV header
    uint8_t header[44]; 
    writeWavHeader(header, totalSamples, sampleRate, 8, 1);
    uint32_t length = 44 + totalSamples;

    // HTTP POST
    wifiSSL.print("POST " + endpoint + " HTTP/1.1\r\n");
    wifiSSL.print("Host: " + String(host) + "\r\n");
    wifiSSL.print("Content-Type: audio/wav\r\n");
    wifiSSL.print("Content-Length: " + String(length) + "\r\n");
    wifiSSL.print("Connection: close\r\n\r\n");

    // Send WAV header + data
    wifiSSL.write(header, 44);
    wifiSSL.write(audioBuffer, totalSamples);

  // Read response
  String resp = "";
  unsigned long timeout = millis() + 5000;
  while (wifiSSL.connected() && millis() < timeout) {
    while (wifiSSL.available()) resp += char(wifiSSL.read());
  }

  Serial.println("Server response:");
  Serial.println(resp);
  extractPrediction(resp);

  wifiSSL.stop();
  isSending = false;
}

void extractPrediction(const String& resp) {
  String prediction = "";

  // Look for specific phrases
  int start = resp.indexOf("Your prediction label response from server");
  if (start != -1) {
    // Find the colon that separates the label from the actual result
    start = resp.indexOf(":", start);
    if (start != -1) {
      prediction = resp.substring(start + 1);
    }
  } else {
    int alt = resp.indexOf("Prediction:");
    if (alt != -1) prediction = resp.substring(alt + 11);
    else prediction = resp;
  }

  prediction.trim();
  if (prediction.length() == 0) prediction = "No prediction";

  // Store and display
  lastPrediction = prediction;
  predictionUpdated = true;
  
  Serial.print("Prediction: ");
  Serial.println(prediction);
  
  renderPrediction();
}

// WAV header helper
// 44-byte (PCM, 8-bit mono)
void writeWavHeader(uint8_t* buf, uint32_t totalAudioLen, uint32_t sampleRate, uint16_t bitsPerSample, uint16_t channels) {
  uint32_t byteRate = sampleRate * channels * bitsPerSample / 8;
  uint32_t dataChunkSize = totalAudioLen;
  uint32_t chunkSize = 36 + dataChunkSize;

  memcpy(buf, "RIFF", 4);
  buf[4] = chunkSize & 0xff; buf[5] = (chunkSize >> 8) & 0xff;
  buf[6] = (chunkSize >> 16) & 0xff; buf[7] = (chunkSize >> 24) & 0xff;
  memcpy(buf + 8, "WAVEfmt ", 8);
  buf[16] = 16; buf[17] = buf[18] = buf[19] = 0;
  buf[20] = 1; buf[21] = 0;
  buf[22] = channels; buf[23] = 0;
  buf[24] = sampleRate & 0xff; buf[25] = (sampleRate >> 8) & 0xff;
  buf[26] = (sampleRate >> 16) & 0xff; buf[27] = (sampleRate >> 24) & 0xff;
  buf[28] = byteRate & 0xff; buf[29] = (byteRate >> 8) & 0xff;
  buf[30] = (byteRate >> 16) & 0xff; buf[31] = (byteRate >> 24) & 0xff;
  uint16_t blockAlign = channels * bitsPerSample / 8;
  buf[32] = blockAlign; buf[33] = 0;
  buf[34] = bitsPerSample; buf[35] = 0;
  memcpy(buf + 36, "data", 4);
  buf[40] = dataChunkSize & 0xff; buf[41] = (dataChunkSize >> 8) & 0xff;
  buf[42] = (dataChunkSize >> 16) & 0xff; buf[43] = (dataChunkSize >> 24) & 0xff;
}
