#include "esp_camera.h"
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <UniversalTelegramBot.h>
#include <ArduinoJson.h>

#define CAMERA_MODEL_AI_THINKER
#include "camera_pins.h"

// WiFi bilgileri
const char* ssid = "ssss";
const char* password = "ssss";

// Telegram bilgileri
String BOTtoken = "ssss";
String CHAT_ID  = "ssss";

WiFiClientSecure client;
UniversalTelegramBot bot(BOTtoken, client);

void setup() {
  Serial.begin(115200);
  Serial.println();

  // PSRAM kontrolü
  if (psramFound()) {
    Serial.println("✅ PSRAM bulundu.");
  } else {
    Serial.println("⚠️ PSRAM bulunamadı! (kamera buffer’ı RAM’de tutulacak)");
  }

  // Kamera konfigürasyonu
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
  config.frame_size = FRAMESIZE_VGA;  // Düşük çözünürlük stabil çalışır
  config.pixel_format = PIXFORMAT_JPEG;
  config.jpeg_quality = 12;            // Daha az RAM kullanımı
  config.fb_count = 1;                 // 1 buffer kullan
  config.fb_location = psramFound() ? CAMERA_FB_IN_PSRAM : CAMERA_FB_IN_DRAM;

  // Kamera başlat
  esp_err_t err = esp_camera_init(&config);
  if (err != ESP_OK) {
    Serial.printf("❌ Kamera başlatılamadı: 0x%x\n", err);
    return;
  }

  // WiFi bağlan
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
  sendPhotoTelegram();
  delay(30000);
}

void sendPhotoTelegram() {
  Serial.println("📸 Fotoğraf çekiliyor...");
  camera_fb_t* fb = esp_camera_fb_get();

  if (!fb) {
    Serial.println("❌ Kamera yakalama başarısız!");
    return;
  }

  Serial.printf("Fotoğraf boyutu: %u byte\n", fb->len);
  Serial.printf("Free heap: %u, PSRAM: %u\n", ESP.getFreeHeap(), ESP.getFreePsram());

  WiFiClientSecure client;
  client.setInsecure();

  if (!client.connect("api.telegram.org", 443)) {
    Serial.println("❌ Telegram sunucusuna bağlanılamadı!");
    esp_camera_fb_return(fb);
    return;
  }

  String head = "--RandomBoundary\r\nContent-Disposition: form-data; name=\"chat_id\"\r\n\r\n" +
                CHAT_ID +
                "\r\n--RandomBoundary\r\nContent-Disposition: form-data; name=\"photo\"; filename=\"snapshot.jpg\"\r\nContent-Type: image/jpeg\r\n\r\n";

  String tail = "\r\n--RandomBoundary--\r\n";
  uint32_t totalLen = head.length() + fb->len + tail.length();

  client.printf("POST /bot%s/sendPhoto HTTP/1.1\r\n", BOTtoken.c_str());
  client.println("Host: api.telegram.org");
  client.println("Content-Type: multipart/form-data; boundary=RandomBoundary");
  client.printf("Content-Length: %d\r\n\r\n", totalLen);
  client.print(head);

  client.write(fb->buf, fb->len);
  client.print(tail);

  esp_camera_fb_return(fb);

  // Yanıtı oku
  while (client.connected()) {
    String line = client.readStringUntil('\n');
    if (line == "\r") break;
  }

  String response = client.readString();
  Serial.println("Telegram cevabı:");
  Serial.println(response);

  delay(2000); // Telegram flood engelini önle
}
