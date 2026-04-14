
#include "esp_camera.h"
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <WiFiManager.h>
#include <time.h>
#include "soc/soc.h"
#include "soc/rtc_cntl_reg.h"

// ─────────────────────────────────────────
//  CONFIG — À MODIFIER
// ─────────────────────────────────────────
const char* SUPABASE_URL  = "https://zxznkslfndrxxgjluafo.supabase.co";
const char* SUPABASE_KEY  = "eyJhbGciOiJIUzI1NiIsInR5cCI6IkpXVCJ9.eyJpc3MiOiJzdXBhYmFzZSIsInJlZiI6Inp4em5rc2xmbmRyeHhnamx1YWZvIiwicm9sZSI6ImFub24iLCJpYXQiOjE3NzYwMjc5NTAsImV4cCI6MjA5MTYwMzk1MH0.8ULd1mfq6MYeW7vjHjIbAy498W7SfyxQ0r1LgWv1-Q0";
const char* PLANT_NAME    = "Champignon";       // nom du dossier dans le bucket
const int   INTERVAL_MIN  = 5;             // intervalle en minutes

// ─────────────────────────────────────────
//  PINS AI THINKER ESP32-CAM
// ─────────────────────────────────────────
#define PWDN_GPIO_NUM     32
#define RESET_GPIO_NUM    -1
#define XCLK_GPIO_NUM      0
#define SIOD_GPIO_NUM     26
#define SIOC_GPIO_NUM     27
#define Y9_GPIO_NUM       35
#define Y8_GPIO_NUM       34
#define Y7_GPIO_NUM       39
#define Y6_GPIO_NUM       36
#define Y5_GPIO_NUM       21
#define Y4_GPIO_NUM       19
#define Y3_GPIO_NUM       18
#define Y2_GPIO_NUM        5
#define VSYNC_GPIO_NUM    25
#define HREF_GPIO_NUM     23
#define PCLK_GPIO_NUM     22
#define FLASH_GPIO         4

// ─────────────────────────────────────────

bool initCamera() {

  
  // Cycle power — obligatoire pour OV3660
  pinMode(PWDN_GPIO_NUM, OUTPUT);
  digitalWrite(PWDN_GPIO_NUM, HIGH);
  delay(500);           // plus long
  digitalWrite(PWDN_GPIO_NUM, LOW);
  delay(500);           // plus long

  camera_config_t config;
  config.ledc_channel = LEDC_CHANNEL_0;
  config.ledc_timer   = LEDC_TIMER_0;
  config.pin_d0       = Y2_GPIO_NUM;
  config.pin_d1       = Y3_GPIO_NUM;
  config.pin_d2       = Y4_GPIO_NUM;
  config.pin_d3       = Y5_GPIO_NUM;
  config.pin_d4       = Y6_GPIO_NUM;
  config.pin_d5       = Y7_GPIO_NUM;
  config.pin_d6       = Y8_GPIO_NUM;
  config.pin_d7       = Y9_GPIO_NUM;
  config.pin_xclk     = XCLK_GPIO_NUM;
  config.pin_pclk     = PCLK_GPIO_NUM;
  config.pin_vsync    = VSYNC_GPIO_NUM;
  config.pin_href     = HREF_GPIO_NUM;
  config.pin_sscb_sda = SIOD_GPIO_NUM;
  config.pin_sscb_scl = SIOC_GPIO_NUM;
  config.pin_pwdn     = PWDN_GPIO_NUM;
  config.pin_reset    = RESET_GPIO_NUM;
  config.xclk_freq_hz = 20000000;       // 15MHz obligatoire pour OV3660
  config.pixel_format = PIXFORMAT_JPEG;
  config.frame_size = FRAMESIZE_QXGA;  // 2048x1536 — OV5640 5MP
  config.jpeg_quality = 12;
  /* 1 buffer : marge heap/PSRAM pour TLS (PUT ~70ko) — fb_count=2 peut provoquer -3 après idle */
  config.fb_count     = 1;
  config.fb_location  = CAMERA_FB_IN_PSRAM;

  esp_err_t err = esp_camera_init(&config);
  if (err != ESP_OK) {
    Serial.printf("❌ Camera init échouée: 0x%x\n", err);
    return false;
  }

  // ⚠️ PAS de set_* ici — les réglages OV3660 échouent avec esp32 v2.x
  // On laisse les paramètres par défaut du capteur
  sensor_t* s = esp_camera_sensor_get();
  if (s == NULL) {
    Serial.println("❌ Capteur non trouvé");
    return false;
  }
  Serial.printf("📷 Capteur PID: 0x%02X\n", s->id.PID);
  Serial.println("✅ Caméra OK");
  return true;
}


// Réseaux connus à tester en priorité
static const char* KNOWN_SSIDS[] = { "Salon", "Salon_Guest" };
static const char* KNOWN_PASS    = "BaguetteEtFromage";
static const int   KNOWN_COUNT   = 2;

bool tryKnownNetworks() {
  for (int n = 0; n < KNOWN_COUNT; n++) {
    Serial.printf("📡 Essai '%s'", KNOWN_SSIDS[n]);
    WiFi.begin(KNOWN_SSIDS[n], KNOWN_PASS);
    for (int i = 0; i < 24 && WiFi.status() != WL_CONNECTED; i++) {
      delay(500);
      Serial.print(".");
    }
    if (WiFi.status() == WL_CONNECTED) {
      WiFi.setSleep(false);
      Serial.printf("\n✅ Connecté — IP: %s RSSI: %d dBm\n",
        WiFi.localIP().toString().c_str(), WiFi.RSSI());
      return true;
    }
    WiFi.disconnect(true);
    delay(300);
    Serial.println(" ✗");
  }
  return false;
}

bool connectWiFi() {
  // 1) Essaie les réseaux connus
  if (tryKnownNetworks()) return true;

  // 2) Fallback : portail WiFiManager (AP "ESP32-CAM-Setup")
  Serial.println("📡 Ouverture portail WiFiManager...");
  WiFiManager wm;
  wm.setConfigPortalTimeout(180);
  if (wm.startConfigPortal("ESP32-CAM-Setup")) {
    WiFi.setSleep(false);
    Serial.printf("✅ Configuré — IP: %s\n", WiFi.localIP().toString().c_str());
    return true;
  }
  Serial.println("❌ WiFi échoué");
  return false;
}

bool reconnectWiFi() {
  // Essaie d'abord une reconnexion rapide (identifiants mémorisés)
  Serial.print("📡 Reconnexion rapide");
  WiFi.reconnect();
  for (int i = 0; i < 12 && WiFi.status() != WL_CONNECTED; i++) {
    delay(500);
    Serial.print(".");
  }
  if (WiFi.status() == WL_CONNECTED) {
    Serial.printf("\n✅ Reconnecté — IP: %s\n", WiFi.localIP().toString().c_str());
    return true;
  }
  // Sinon retente les réseaux connus
  WiFi.disconnect(true);
  delay(300);
  Serial.println(" ✗");
  if (tryKnownNetworks()) return true;
  Serial.println("❌ Reconnexion impossible — reboot");
  ESP.restart();
  return false;
}

void syncTime() {
  configTime(3600, 3600, "pool.ntp.org");
  struct tm timeinfo;
  Serial.print("⏰ NTP");
  for (int i = 0; i < 15 && !getLocalTime(&timeinfo); i++) {
    delay(500);
    Serial.print(".");
  }
  Serial.printf("\n✅ %02d/%02d/%04d %02d:%02d\n",
    timeinfo.tm_mday, timeinfo.tm_mon+1, timeinfo.tm_year+1900,
    timeinfo.tm_hour, timeinfo.tm_min);
}

void insertPhotoMeta(const char* path, time_t ts, size_t sz) {
  WiFiClientSecure client;
  client.setInsecure();
  client.setTimeout(30000);

  HTTPClient http;
  String url = String(SUPABASE_URL) + "/rest/v1/timelapse_photos";
  http.begin(client, url);
  http.addHeader("Content-Type", "application/json");
  http.addHeader("apikey", SUPABASE_KEY);
  http.addHeader("Authorization", String("Bearer ") + SUPABASE_KEY);

  char iso[30];
  strftime(iso, sizeof(iso), "%Y-%m-%dT%H:%M:%S", localtime(&ts));

  String body = "{\"plant_name\":\"" + String(PLANT_NAME) + "\","
    "\"taken_at\":\"" + String(iso) + "\","
    "\"storage_path\":\"" + String(path) + "\","
    "\"file_size_bytes\":" + String(sz) + "}";

  Serial.printf("   Meta HTTP %d\n", http.POST(body));
  http.end();
}

static bool uploadJpegWithRetries(uint8_t* data, size_t len, const String& uploadUrl, int& outCode) {
  const int kMaxTries = 3;
  for (int attempt = 1; attempt <= kMaxTries; attempt++) {
    if (WiFi.status() != WL_CONNECTED) {
      reconnectWiFi();
    }

    WiFiClientSecure client;
    client.setInsecure();
    client.setTimeout(45000);

    HTTPClient http;
    http.begin(client, uploadUrl);
    http.addHeader("Content-Type", "image/jpeg");
    http.addHeader("Authorization", String("Bearer ") + SUPABASE_KEY);
    http.setTimeout(45000);

    outCode = http.PUT(data, len);
    http.end();

    if (outCode == 200 || outCode == 201) {
      return true;
    }
    Serial.printf("   ⚠️ Upload tentative %d → HTTP %d\n", attempt, outCode);
    if (attempt < kMaxTries) {
      delay(2000);
      if (WiFi.status() != WL_CONNECTED) {
        WiFi.reconnect();
        delay(2000);
      }
    }
  }
  return false;
}

bool captureAndUpload() {
  time_t now;
  time(&now);

  // Capture directe — pas de frame de chauffe (cause de blocage sur OV3660)
  camera_fb_t* fb = esp_camera_fb_get();
  if (!fb) {
    Serial.println("❌ Capture échouée");
    return false;
  }

  Serial.printf("📸 %zu bytes (%dx%d)\n", fb->len, fb->width, fb->height);

  char filename[80];
  snprintf(filename, sizeof(filename), "%s/%ld.jpg", PLANT_NAME, now);

  size_t size = fb->len;
  uint8_t* jpeg = fb->buf;
  size_t jpegLen = fb->len;

  String uploadUrl = String(SUPABASE_URL) + "/storage/v1/object/timelapse/" + filename;

  int code = -1;
  bool ok = uploadJpegWithRetries(jpeg, jpegLen, uploadUrl, code);
  esp_camera_fb_return(fb);

  Serial.printf("☁️  Upload HTTP %d\n", code);

  if (ok) {
    insertPhotoMeta(filename, now, size);
    Serial.println("✅ Photo uploadée !");
    return true;
  }
  Serial.printf("❌ Échec upload (HTTP %d)\n", code);
  return false;
}

unsigned long lastCapture = 0;
const unsigned long INTERVAL_MS = (unsigned long)INTERVAL_MIN * 60 * 1000;

void setup() {
  Serial.begin(115200);
  delay(500);

  // AJOUTE CES DEUX LIGNES ICI — désactive le brownout detector
  WRITE_PERI_REG(RTC_CNTL_BROWN_OUT_REG, 0);

  Serial.println("\n🌱 ESP32-CAM Timelapse v3");
  Serial.printf("   Intervalle : %d min | Plante : %s\n", INTERVAL_MIN, PLANT_NAME);

  pinMode(FLASH_GPIO, OUTPUT);
  digitalWrite(FLASH_GPIO, LOW);

  if (!initCamera()) {
    Serial.println("🔄 Retry dans 3s...");
    delay(3000);
    if (!initCamera()) {
      Serial.println("❌ Abandon — redémarrage dans 10s");
      delay(10000);
      ESP.restart();
    }
  }

  if (!connectWiFi()) { delay(10000); ESP.restart(); }

  syncTime();

  Serial.println("\n📷 Première photo...");
  captureAndUpload();
  lastCapture = millis();
}

void loop() {
  if (WiFi.status() != WL_CONNECTED) reconnectWiFi();

  unsigned long now = millis();
  if (now - lastCapture >= INTERVAL_MS) {
    lastCapture = now;
    if (WiFi.status() == WL_CONNECTED) {
      Serial.println("\n--- Capture ---");
      captureAndUpload();
    } else {
      Serial.println("⚠️ WiFi absent — capture ignorée, reconnexion...");
      reconnectWiFi();
    }
  }

  static unsigned long lastPrint = 0;
  if (now - lastPrint >= 60000) {
    lastPrint = now;
    unsigned long rem = (INTERVAL_MS - (now - lastCapture)) / 60000;
    Serial.printf("⏱  Prochaine dans ~%lu min\n", rem + 1);
  }

  delay(1000);
}
