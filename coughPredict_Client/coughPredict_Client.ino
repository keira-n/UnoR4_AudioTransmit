#include <Arduino_GFX_Library.h>
#include <arduinoFFT.h>
#include <WiFiS3.h>
#include <Arduino_JSON.h>

// CA root cert
const char* flask_ca = \
"-----BEGIN CERTIFICATE-----\n" \
// Add your own cert
"-----END CERTIFICATE-----\n";

// Pins and const
// Adjust this to your own wire mapping
#define SAMPLES 64
#define SAMPLING_FREQUENCY 8000
#define MIC_PIN A0
#define TFT_CS 10
#define TFT_DC 8
#define TFT_RST 9

// WiFi and server
const char* ssid = "Your SSID (Network name)";
const char* password = "Password";
const char* host = "Host address";
const uint16_t httpsPort = 443; // Default HTTPS port
const String endpoint = "";

// Audio recording parameters
const int sampleRate = 8000; // 8kHz
const int totalSeconds = 5;
const int totalSamples = sampleRate * totalSeconds; // 40000 samples
const int chunkSamples = 400;                       // 400 samples per chunk
const int chunksCount = totalSamples / chunkSamples; // 100 chunks total

uint8_t audioChunkBuffer[chunkSamples];
int currentChunkIndex = 0;

unsigned long samplingPeriod_us;

// Energy detection variables
double backgroundEnergy = 0;
const double alpha = 0.05, thresholdMultiplier = 2.5;
#define ENERGY_HISTORY_SIZE 5
double energyHistory[ENERGY_HISTORY_SIZE] = {0};
int energyIndex = 0;
String lastStatus = "";

// Network client
WiFiSSLClient wifiSSL;
String lastPrediction = "";
const unsigned long startupDelayMs = 10000;

// Operation status
bool isRecording = false;
bool isSending = false;

String sessionID = "";

// TFT display objects
Arduino_DataBus *bus = new Arduino_HWSPI(TFT_DC, TFT_CS);
Arduino_GFX *gfx = new Arduino_ILI9341(bus, TFT_RST);

// FFT objects
double vReal[SAMPLES], vImag[SAMPLES];
ArduinoFFT<double> FFT(vReal, vImag, SAMPLES, SAMPLING_FREQUENCY);

unsigned long lastSampleMicros = 0;
int sampleIndex = 0;

unsigned long lastDetection = 0, coughMessageEnd = 0;
const unsigned long cooldownMs = 1000;

bool gracePeriodPassed = false;
unsigned long gracePeriodStartTime = 0;
const unsigned long gracePeriodDuration = 10000; // 10 seconds grace period

// Generate random SessionID
String generateSessionID(int length) {
  const char charset[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789";
  String session = "";
  for (int i = 0; i < length; i++) {
    session += charset[random(0, sizeof(charset) - 1)];
  }
  return session;
}

void setup() {
  Serial.begin(115200);
  delay(1000);

  randomSeed(analogRead(A3));
  sessionID = generateSessionID(6);
  Serial.print("Session ID: ");
  Serial.println(sessionID);

  gfx->begin();
  gfx->setRotation(3);
  gfx->fillScreen(BLACK);

  gfx->setTextSize(2);
  gfx->setTextColor(WHITE, BLACK);
  gfx->setCursor(90, 10);
  gfx->print("Noise Level");

  samplingPeriod_us = round(1e6 / SAMPLING_FREQUENCY);

  connectToWiFi();
  
  // Start grace period as soon as WiFi is connected
  gracePeriodStartTime = millis();  // Record when grace period starts
  gracePeriodPassed = false;         // Reset grace period flag
  Serial.println("Grace period started.");
}

void loop() {
  unsigned long now = millis();

  // Check if grace period is still active
  if (!gracePeriodPassed) {
    if (now - gracePeriodStartTime >= gracePeriodDuration) {
      gracePeriodPassed = true;
      Serial.println("Grace period done. System is ready.");
    } else {
      Serial.println("Stabilizing system. Grace period in effect...");

      // Continue monitoring sound levels for cough detection during grace period (stabilising)
      sampleAudioFFT();
      double coughEnergy = calculateCoughEnergy();
      updateBackgroundNoise(coughEnergy);
      double dynT = backgroundEnergy * thresholdMultiplier;
      updateEnergyHistory(coughEnergy, dynT);

      bool coughDetected = isSustainedEnergy();
      if (coughDetected && now - lastDetection > cooldownMs) {
        lastDetection = now;
        Serial.println("Cough detected (During Grace Period).");
      }
      displayCurrentSoundLevel(coughEnergy, false);
      return;
    }
  }

  // After grace period, proceed with the other operations
  if (!isRecording && !isSending) {
    sampleAudioFFT();
    double coughEnergy = calculateCoughEnergy();
    updateBackgroundNoise(coughEnergy);
    double dynT = backgroundEnergy * thresholdMultiplier;
    updateEnergyHistory(coughEnergy, dynT);

    bool coughDetected = isSustainedEnergy() && (now - lastDetection > cooldownMs);

    if (coughDetected) {
      lastDetection = now;

      if (now >= startupDelayMs) {
        coughMessageEnd = now + 1000;
        Serial.println("Cough detected. Recording...");
        isRecording = true;
        sampleIndex = 0;
        currentChunkIndex = 0;
      } else {
        Serial.println("Cough detected during grace period.");
      }
    }

    displayCurrentSoundLevel(coughEnergy, (now < coughMessageEnd));
  }

  // Handle recording and sending after detection
  if (isRecording) {
    recordAudioChunk();
  }

  if (isSending) {
    sendCurrentChunk();
  }
}

// Audio Sampling for FFT
void sampleAudioFFT() {
  for (int i = 0; i < SAMPLES; i++) {
    unsigned long t0 = micros();
    vReal[i] = analogRead(MIC_PIN);
    vImag[i] = 0;
    while (micros() - t0 < samplingPeriod_us);
  }
}

double calculateCoughEnergy() {
  FFT.windowing(vReal, SAMPLES, FFT_WIN_TYP_HAMMING, FFT_FORWARD);
  FFT.compute(vReal, vImag, SAMPLES, FFT_FORWARD);
  FFT.complexToMagnitude(vReal, vImag, SAMPLES);

  double sum = 0;
  for (int i = 3; i < SAMPLES / 2; i++) {
    double freq = i * SAMPLING_FREQUENCY / SAMPLES;
    if (freq >= 300 && freq <= 2000) sum += vReal[i];
  }
  return sum;
}

// Energy Analysis
void updateBackgroundNoise(double e) {
  backgroundEnergy = (1 - alpha) * backgroundEnergy + alpha * e;
}

void updateEnergyHistory(double e, double t) {
  energyHistory[energyIndex] = (e > t) ? e : 0;
  energyIndex = (energyIndex + 1) % ENERGY_HISTORY_SIZE;
}

bool isSustainedEnergy() {
  int count = 0;
  for (int i = 0; i < ENERGY_HISTORY_SIZE; i++) {
    if (energyHistory[i] > 0) count++;
  }
  return count >= 2;
}

// Display
void displayCurrentSoundLevel(double energy, bool cough) {
  const int x = 0, y = 40, w = 320, h = 15;
  gfx->fillRect(x, y, w, h, DARKGREY);  // Clear the old bar
  int len = constrain((int)(energy / 6000.0 * w), 0, w);  // Normalise energy for the bar
  uint16_t c = cough ? BLUE : GREEN;  // Use blue for cough detection, green for normal
  gfx->fillRect(x, y, len, h, c);  // Draw the energy bar

  String st = cough ? "Cough Detected" : "Ambient Noise";
  if (st != lastStatus) {
    gfx->fillRect(x, y + h + 10, w, 20, BLACK);  // Clear previous text
    gfx->setCursor(x + 10, y + h + 10);
    gfx->setTextSize(2);
    gfx->setTextColor(c, BLACK);
    gfx->print(st);
    lastStatus = st;
  }

  // Display the prediction message
  if (lastPrediction != "") {
    gfx->fillRect(x, y + h + 30, w, 20, BLACK);  // Clear previous prediction
    gfx->setCursor(x + 10, y + h + 40);
    gfx->setTextSize(2);
    gfx->setTextColor(WHITE, BLACK); 
    gfx->print("Prediction: ");
    gfx->print(lastPrediction);
  }
}


// WiFi connection and status
void connectToWiFi() {
  Serial.print("Connecting to WiFi");
  WiFi.begin(ssid, password);
  int tries = 0;
  while (WiFi.status() != WL_CONNECTED && tries < 60) {
    Serial.print(".");
    delay(500);
    tries++;
  }
  if (WiFi.status() == WL_CONNECTED) {
    Serial.println("\nWiFi connected!");
    Serial.print("IP address: ");
    Serial.println(WiFi.localIP());
  } else {
    Serial.println("\n[!!!] WiFi connection failed.");
  }
}

// Recording in chunks
void recordAudioChunk() {
  unsigned long now = micros();  // Track time in microseconds
  if (now - lastSampleMicros >= 125) {  // 125 us per sample for 8kHz sampling rate
    lastSampleMicros = now;

    // Read mic, convert 10-bit to 8-bit by shifting 2 bits right
    audioChunkBuffer[sampleIndex] = analogRead(MIC_PIN) >> 2;
    sampleIndex++;

    if (sampleIndex >= chunkSamples) {
      // Finished one chunk
      Serial.print("Chunk ");
      Serial.print(currentChunkIndex + 1);
      Serial.print("/"); 
      Serial.println(chunksCount);

      currentChunkIndex++;
      sampleIndex = 0;

      if (currentChunkIndex >= chunksCount) {
        // Done recording all chunks
        isRecording = false;
        isSending = true;
        currentChunkIndex = 0;
        Serial.println("Recording done. Sending...");
      }
    }
  }
}

// Send one chunk at a time
void sendCurrentChunk() {
  Serial.print("Sending chunk ");
  Serial.print(currentChunkIndex + 1);
  Serial.print("/"); 
  Serial.println(chunksCount);

  wifiSSL.setCACert(flask_ca);
  if (!wifiSSL.connect(host, httpsPort)) {
    Serial.println("[!!!] HTTPS connection failed");
    return;
  }

  // Prepare JSON audio array string for the current chunk
  String audioArray = "[";
  for (int i = 0; i < chunkSamples; i++) {
    audioArray += String(audioChunkBuffer[i]);
    if (i < chunkSamples - 1) audioArray += ",";
  }
  audioArray += "]";

  String payload = "{\"session_id\":\"" + sessionID + "\"," +
                   "\"chunk_id\":" + String(currentChunkIndex) + "," +
                   "\"total_chunks\":" + String(chunksCount) + "," +
                   "\"audio\":" + audioArray + "}";

  wifiSSL.print("POST " + endpoint + " HTTP/1.1\r\n");
  wifiSSL.print("Host: " + String(host) + "\r\n");
  wifiSSL.print("Content-Type: application/json\r\n");
  wifiSSL.print("Content-Length: " + String(payload.length()) + "\r\n");
  wifiSSL.print("Connection: close\r\n\r\n");
  wifiSSL.print(payload);

  // Read response only on last chunk
  if (currentChunkIndex == chunksCount - 1) {
    String response = "";
    unsigned long timeout = millis() + 3000;
    while (wifiSSL.connected() && millis() < timeout) {
      while (wifiSSL.available()) {
        char c = wifiSSL.read();
        response += c;
      }
    }

    // Print response
    Serial.println("Full response:");
    Serial.println(response);

    // Look for the prediction that contains the conclusion
    int startIndex = response.indexOf("<strong>Prediction:</strong>") + strlen("<strong>Prediction:</strong>");
    int endIndex = response.indexOf("</p>", startIndex);  // Find the end of the prediction paragraph

    if (startIndex != -1 && endIndex != -1) {
        String conclusion = response.substring(startIndex, endIndex);
        Serial.print("Conclusion received: ");
        Serial.println(conclusion);
        conclusion.trim();  // Remove any surrounding whitespace
        lastPrediction = conclusion;  // Store the conclusion message

        // Debug: Check the prediction
        Serial.print("lastPrediction: ");
        Serial.println(lastPrediction);  // Debug line to print lastPrediction
    } else {
        Serial.println("[!!!] Conclusion not found in the response.");
    }
  }

  wifiSSL.stop();

  currentChunkIndex++;
  if (currentChunkIndex >= chunksCount) {
    Serial.println("All chunks sent.");
    isSending = false;
  }
}