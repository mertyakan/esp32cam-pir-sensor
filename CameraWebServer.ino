#include "esp_camera.h"
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <UniversalTelegramBot.h>
#include <ArduinoJson.h>

#define CAMERA_MODEL_AI_THINKER
#include "camera_pins.h"

// WiFi bilgileri
const char* ssid = "***";
const char* password = "***";

String BOTtoken = "***";
String CHAT_ID  = "***";
WiFiClientSecure client;
UniversalTelegramBot bot(BOTtoken, client);

// --- PIR ayarları ---
#define PIR_PIN 13
volatile bool motionDetected = false;
unsigned long lastMotionTime = 0;
unsigned long motionStartTime = 0;
const unsigned long motionCooldown = 2000; // Her fotoğraf arası 2 saniye
const unsigned long motionTimeout = 15000; // Hareket bittiğinde 15 saniye daha bekle
bool inMotionSession = false;

// ISR
void IRAM_ATTR detectsMovement() {
  motionDetected = true;
}

// Telegram mesaj kontrol periyodu
unsigned long lastBotCheck = 0;
const unsigned long botPollInterval = 1000; // ms

void setup() {
  Serial.begin(115200);
  Serial.println();

  if (psramFound()) {
    Serial.println("✅ PSRAM bulundu.");
  } else {
    Serial.println("⚠️ PSRAM bulunamadı!");
  }

  // Kamera ayarları
  camera_config_t config;
  config.ledc_channel = LEDC_CHANNEL_0;
  config.ledc_timer = LEDC_TIMER_0;
  config.pin_d0 = Y2_GPIO_NUM;
  config.pin_d1 = Y3_GPIO_NUM;
  config.pin_d2 = Y4_GPIO_NUM;
  config.pin_d3 = Y5_GPIO_NUM;
  config.pin_d4 = Y6_GPIO_NUM;
  config.pin_d5 = Y7_GPIO_NUM;
  config.pin_d6 = Y8_GPIO_NUM;
  config.pin_d7 = Y9_GPIO_NUM;
  config.pin_xclk = XCLK_GPIO_NUM;
  config.pin_pclk = PCLK_GPIO_NUM;
  config.pin_vsync = VSYNC_GPIO_NUM;
  config.pin_href = HREF_GPIO_NUM;
  config.pin_sccb_sda = SIOD_GPIO_NUM;
  config.pin_sccb_scl = SIOC_GPIO_NUM;
  config.pin_pwdn = PWDN_GPIO_NUM;
  config.pin_reset = RESET_GPIO_NUM;
  config.xclk_freq_hz = 20000000;
  config.frame_size = FRAMESIZE_VGA;
  config.pixel_format = PIXFORMAT_JPEG;
  config.jpeg_quality = 12;
  config.fb_count = 1;
  config.fb_location = psramFound() ? CAMERA_FB_IN_PSRAM : CAMERA_FB_IN_DRAM;

  if (esp_camera_init(&config) != ESP_OK) {
    Serial.println("❌ Kamera başlatılamadı!");
    return;
  }

  pinMode(PIR_PIN, INPUT);
  attachInterrupt(digitalPinToInterrupt(PIR_PIN), detectsMovement, RISING);

  WiFi.begin(ssid, password);
  WiFi.setSleep(false);
  Serial.print("WiFi bağlanıyor");
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println("\n✅ WiFi bağlı: " + WiFi.localIP().toString());

  client.setInsecure();
  bot.sendMessage(CHAT_ID, "ESP32-CAM çevrimiçi! 📸", "");
}

void loop() {
  unsigned long now = millis();

  // PIR durumunu kontrol et
  int pirState = digitalRead(PIR_PIN);
  
  if (pirState == HIGH) {
    // Hareket var!
    if (!inMotionSession) {
      // Yeni hareket oturumu başladı
      Serial.println("🐦 Yeni hareket algılandı - oturum başladı!");
      inMotionSession = true;
      motionStartTime = now;
      lastMotionTime = now;
      sendPhotoTelegram();
    } else {
      // Devam eden oturum - belirli aralıklarla fotoğraf çek
      if (now - lastMotionTime >= motionCooldown) {
        Serial.println("📸 Hareket devam ediyor - yeni fotoğraf çekiliyor...");
        lastMotionTime = now;
        sendPhotoTelegram();
      }
    }
    motionStartTime = now; // Son hareket zamanını güncelle
  } else {
    // Hareket yok - ama oturum hala aktif mi?
    if (inMotionSession && (now - motionStartTime >= motionTimeout)) {
      Serial.println("🐦 Hareket bitti - oturum kapatıldı.");
      inMotionSession = false;
    }
  }

  // Telegram komutlarını kontrol et
  if (now - lastBotCheck > botPollInterval) {
    int numNewMessages = bot.getUpdates(bot.last_message_received + 1);

    while (numNewMessages) {
      Serial.println("📩 Yeni mesaj(lar) var.");
      handleNewMessages(numNewMessages);
      numNewMessages = bot.getUpdates(bot.last_message_received + 1);
    }

    lastBotCheck = now;
  }

  delay(50);
}

void handleNewMessages(int numNewMessages) {
  for (int i = 0; i < numNewMessages; i++) {
    String chat_id = bot.messages[i].chat_id;
    String text = bot.messages[i].text;

    if (chat_id != CHAT_ID) {
      bot.sendMessage(chat_id, "❌ Yetkisiz kullanıcı!", "");
      continue;
    }

    if (text == "/photo") {
      Serial.println("📸 /photo komutu alındı.");
      bot.sendMessage(CHAT_ID, "📸 Fotoğraf çekiliyor...", "");
      sendPhotoTelegram();
    } 
    else if (text == "/start") {
      bot.sendMessage(CHAT_ID, "Merhaba! /photo komutu ile anlık fotoğraf çekebilirsin 📷", "");
    }
  }
}

void sendPhotoTelegram() {
  Serial.println("📸 Fotoğraf çekiliyor...");
  
  // Eski frame'i buffer'dan temizle
  camera_fb_t* fb_old = esp_camera_fb_get();
  if (fb_old) {
    esp_camera_fb_return(fb_old);
    Serial.println("🗑️ Eski frame temizlendi");
  }
  
  // PIR tetiklendikten sonra sensörün stabilize olması için bekle
  delay(500);
  
  // Şimdi GÜNCEL frame'i çek
  camera_fb_t* fb = esp_camera_fb_get();
  if (!fb) {
    Serial.println("❌ Kamera yakalama başarısız!");
    bot.sendMessage(CHAT_ID, "❌ Kamera hatası!", "");
    return;
  }

  Serial.printf("Fotoğraf boyutu: %u byte\n", fb->len);
  Serial.printf("Free heap: %u, PSRAM: %u\n", ESP.getFreeHeap(), ESP.getFreePsram());

  WiFiClientSecure telegramClient;
  telegramClient.setInsecure();
  telegramClient.setTimeout(30000); // 30 saniye timeout

  Serial.println("Telegram'a bağlanılıyor...");
  if (!telegramClient.connect("api.telegram.org", 443)) {
    Serial.println("❌ Telegram sunucusuna bağlanılamadı!");
    esp_camera_fb_return(fb);
    bot.sendMessage(CHAT_ID, "❌ Telegram bağlantı hatası!", "");
    return;
  }
  Serial.println("✅ Telegram'a bağlandı");

  String boundary = "----WebKitFormBoundary7MA4YWxkTrZu0gW";
  
  String head = "--" + boundary + "\r\n";
  head += "Content-Disposition: form-data; name=\"chat_id\"\r\n\r\n";
  head += CHAT_ID + "\r\n";
  head += "--" + boundary + "\r\n";
  head += "Content-Disposition: form-data; name=\"photo\"; filename=\"esp32cam.jpg\"\r\n";
  head += "Content-Type: image/jpeg\r\n\r\n";

  String tail = "\r\n--" + boundary + "--\r\n";
  uint32_t totalLen = head.length() + fb->len + tail.length();

  String request = "POST /bot" + BOTtoken + "/sendPhoto HTTP/1.1\r\n";
  request += "Host: api.telegram.org\r\n";
  request += "Content-Type: multipart/form-data; boundary=" + boundary + "\r\n";
  request += "Content-Length: " + String(totalLen) + "\r\n";
  request += "Connection: close\r\n\r\n";

  telegramClient.print(request);
  telegramClient.print(head);

  // Fotoğrafı parça parça gönder
  uint8_t* fbBuf = fb->buf;
  size_t fbLen = fb->len;
  size_t chunkSize = 1024;
  
  Serial.println("Fotoğraf gönderiliyor...");
  for (size_t n = 0; n < fbLen; n += chunkSize) {
    size_t thisChunk = min(chunkSize, fbLen - n);
    telegramClient.write(&fbBuf[n], thisChunk);
    
    if (n % (chunkSize * 10) == 0) {
      Serial.printf("  İlerleme: %d%%\n", (n * 100) / fbLen);
    }
  }
  
  telegramClient.print(tail);
  Serial.println("✅ Fotoğraf gönderildi, cevap bekleniyor...");

  esp_camera_fb_return(fb);

  // HTTP başlıklarını oku
  unsigned long timeout = millis();
  while (telegramClient.connected() && (millis() - timeout < 10000)) {
    if (telegramClient.available()) {
      String line = telegramClient.readStringUntil('\n');
      Serial.println("Header: " + line);
      if (line == "\r") {
        Serial.println("Başlıklar bitti, body okunuyor...");
        break;
      }
    }
  }

  // Response body'yi oku - veri gelene kadar kısa süre bekle
  String response = "";
  timeout = millis();
  while (millis() - timeout < 3000) {  // Maksimum 3 saniye bekle
    while (telegramClient.available()) {
      char c = telegramClient.read();
      response += c;
    }
    if (response.length() > 0 && !telegramClient.available()) {
      // Veri geldi ve artık yok, muhtemelen bitti
      delay(100);  // Son parçalar için kısa bir bekle
      if (!telegramClient.available()) break;
    }
  }

  Serial.println("Telegram cevabı:");
  Serial.println(response);

  // JSON parse et
  if (response.indexOf("\"ok\":true") > 0) {
    Serial.println("✅ Fotoğraf başarıyla gönderildi!");
  } else if (response.indexOf("\"ok\":false") > 0) {
    Serial.println("❌ Telegram API hatası!");
  } else {
    Serial.println("⚠️ Belirsiz cevap");
  }

  telegramClient.stop();
  delay(1000);
}
