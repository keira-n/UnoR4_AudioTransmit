# UnoR4_CoughAudioTransmit
Record a short audio and send as multiple chunks of JSONs to a server for prediction.<br/><br/>

## How to find a website's CA root certificate on Google Chrome
### Find the "View site information button"

<img width="1656" height="848" alt="image" src="https://github.com/user-attachments/assets/2e78c79c-c96b-4347-9410-b97619695e04" /><br/>

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
> Reminder!
> Double-check the right format for your programming language!
