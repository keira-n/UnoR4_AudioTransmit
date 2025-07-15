# UnoR4_CoughAudioTransmit
Record a short audio and send as multiple chunks of JSON payloads to a server for prediction.<br/><br/>

> [!NOTE]
> In collaboration with @River1808 <br/>
> Server can be found at https://github.com/River1808/flask-cough-api

## Hardware
Arduino Uno R4 Wifi Board<br/>
MAX9814 Microphone<br/>
ILI9341 LCD Display<br/>
8-Channel Bi-directional Level Shifter (Converts voltage levels between 5V and 3.3V for board and display communication)<br/><br/>
## How to find a website's CA root certificate on Google Chrome
### Find the "View site information button"

<img width="1600" height="848" alt="image" src="https://github.com/user-attachments/assets/2e78c79c-c96b-4347-9410-b97619695e04" /><br/>

### Look for this part
If the connection is secure it should look like this.<br/>

<img width="421" height="548" alt="image" src="https://github.com/user-attachments/assets/77d59c47-3665-41b2-8aff-5e76164f80fd" /><br/>
Click on it.

### View the certificate

<img width="423" height="353" alt="image" src="https://github.com/user-attachments/assets/60cab53b-0fde-472e-895d-5c9f34ebca7a" /><br/>

### Export the certificate

<img width="682" height="841" alt="image" src="https://github.com/user-attachments/assets/783b4904-f2ca-47df-bdfc-3a66a7250ab1" /><br/>

### Find the exported file
Once you have found the file, open it in notepad or any text editor. You should see something like this now:
```
-----BEGIN CERTIFICATE-----
\[Encoded Certificate]
-----END CERTIFICATE-----
```
> [!IMPORTANT]
> Double-check the right format for your programming language!
