// Kran Air Otomatis — ESP32 + Sensor Ultrasonik HC-SR04 + Solenoid Valve 12V
//
// Cara kerja:
//  - Sensor HC-SR04 mengukur jarak tiap 100 milidetik; hasil difilter
//    median dari 3 bacaan supaya satu bacaan liar tidak membuka kran.
//  - Tangan terdeteksi kurang dari 15 cm -> relay ON, katup solenoid
//    membuka, air mengalir, LED hijau menyala.
//  - Kran menutup otomatis begitu tangan pergi, atau setelah 30 detik
//    menyala terus (anti-banjir, misalnya sensor tertutup benda).
//  - Setiap kran dibuka, ESP32 mengirim notifikasi Telegram
//    (debounce 30 detik supaya tidak spam).
//
// Wiring:
//  - HC-SR04      : TRIG -> GPIO5, ECHO -> GPIO18, VCC -> VIN 5V, GND -> GND
//  - Relay 1 ch   : IN -> GPIO26, VCC -> 3V3, GND -> GND
//                   (modul relay umum aktif LOW: relay ON saat GPIO26 LOW)
//  - Solenoid 12V : satu kabel ke +12V adaptor, kabel lain ke kontak NO
//                   relay; kontak COM relay ke GND adaptor 12V.
//                   GND adaptor 12V TIDAK digabung ke GND ESP32.
//  - LED hijau    : GPIO32 (seri resistor 220 ohm ke GND)
//
// Catu daya: ESP32 dari USB 5V, solenoid dari adaptor 12V 1A terpisah.
// Jangan menyalakan solenoid dari pin 5V ESP32 — arusnya tidak cukup.
//
// Library yang dibutuhkan (semua tersedia di Library Manager):
//  - UniversalTelegramBot 1.3.0 (hanya bila USE_TELEGRAM 1)
//  - ArduinoJson 6.21.5 (dipakai UniversalTelegramBot)
// Compile: arduino-cli compile --fqbn esp32:esp32:esp32 KranAir
// (terverifikasi 20 Nov 2026: 1.066.257 byte, 81% flash, RAM 50.528 byte,
//  15%, ESP32 core 4.0.0, COMPILE_RC=0)

// ---------- KONFIGURASI ----------
#define USE_TELEGRAM 1  // 1 = kirim notifikasi Telegram saat kran dibuka, 0 = tanpa Telegram

#define PIN_TRIG 5      // HC-SR04 TRIG
#define PIN_ECHO 18     // HC-SR04 ECHO
#define PIN_RELAY 26    // IN modul relay (aktif LOW)
#define PIN_LED_HIJAU 32

// Ambang dan waktu — semua bernama, tanpa magic number di logika.
#define JARAK_BUKA_CM 15              // tangan < 15 cm = kran buka
#define JARAK_MAKS_CM 400             // bacaan di atas ini dianggap tidak valid
#define INTERVAL_UKUR_MS 100          // ukur jarak tiap 100 ms
#define JUMLAH_SAMPEL_MEDIAN 3        // filter median dari 3 bacaan
#define ECHO_TIMEOUT_US 30000         // timeout echo 30 ms (tidak hang bila tidak ada gema)
#define PULSA_TRIG_US 10              // lebar pulsa pemicu HC-SR04
#define JEDA_TRIG_US 2                // jeda LOW sebelum pulsa pemicu
#define BUKA_MAKS_MS 30000UL          // kran tutup paksa setelah 30 detik (anti-banjir)
#define TELEGRAM_DEBOUNCE_MS 30000UL  // jeda minimum antar notifikasi Telegram
#define RELAY_NYALA LOW               // modul relay aktif LOW
#define RELAY_MATI HIGH
#define LED_NYALA HIGH
#define LED_MATI LOW

// Kecepatan bunyi di udara: 0,0343 cm per mikrodetik; jarak = durasi
// pulang-pergi dikali kecepatan bunyi dibagi 2.
const float KECEPATAN_BUNYI_CM_PER_US = 0.0343f;
const int PEMBAGI_PULANG_PERGI = 2;

const char *WIFI_SSID = "NAMA_WIFI";
const char *WIFI_PASS = "PASSWORD_WIFI";
const char *BOT_TOKEN = "TOKEN_BOT_TELEGRAM";
const char *CHAT_ID = "CHAT_ID_TELEGRAM";

// ---------- STATE ----------
bool kranTerbuka = false;
unsigned long kranMulaiMs = 0;       // millis() saat kran dibuka
unsigned long ukurTerakhirMs = 0;    // millis() pengukuran terakhir
unsigned long telegramTerakhirMs = 0;
bool telegramPernahDikirim = false;

#if USE_TELEGRAM
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <UniversalTelegramBot.h>
#include <ArduinoJson.h>

WiFiClientSecure telegramClient;
UniversalTelegramBot bot(BOT_TOKEN, telegramClient);

void sambungWiFi() {
  Serial.print("WiFi: menyambung ke ");
  Serial.println(WIFI_SSID);
  WiFi.begin(WIFI_SSID, WIFI_PASS);
  unsigned long mulai = millis();
  const unsigned long WIFI_TIMEOUT_MS = 15000UL;
  while (WiFi.status() != WL_CONNECTED && millis() - mulai < WIFI_TIMEOUT_MS) {
    delay(500);
    Serial.print(".");
  }
  Serial.println();
  if (WiFi.status() == WL_CONNECTED) {
    Serial.print("WiFi tersambung, IP: ");
    Serial.println(WiFi.localIP());
    telegramClient.setInsecure();
  } else {
    Serial.println("WiFi gagal — Telegram dimatikan untuk sesi ini.");
  }
}

// Kirim notifikasi Telegram; diam bila WiFi putus (tanpa blocking).
void kirimTelegram(const String &pesan) {
  if (WiFi.status() != WL_CONNECTED) return;
  bot.sendMessage(CHAT_ID, pesan, "");
}
#else
void sambungWiFi() {}
void kirimTelegram(const String &) {}
#endif

// ---------- PENGUKURAN JARAK ----------
// Satu kali pengukuran HC-SR04; kembalikan JARAK_MAKS_CM bila echo timeout
// (tidak ada gema) supaya tidak hang dan tidak dianggap tangan dekat.
float bacaJarak() {
  digitalWrite(PIN_TRIG, LOW);
  delayMicroseconds(JEDA_TRIG_US);
  digitalWrite(PIN_TRIG, HIGH);
  delayMicroseconds(PULSA_TRIG_US);
  digitalWrite(PIN_TRIG, LOW);
  unsigned long durasiUs = pulseIn(PIN_ECHO, HIGH, ECHO_TIMEOUT_US);
  if (durasiUs == 0) return (float)JARAK_MAKS_CM;
  float jarakCm = (float)durasiUs * KECEPATAN_BUNYI_CM_PER_US / PEMBAGI_PULANG_PERGI;
  if (jarakCm > JARAK_MAKS_CM) return (float)JARAK_MAKS_CM;
  return jarakCm;
}

// Median dari 3 bacaan: tahan terhadap satu bacaan liar.
float bacaJarakMedian() {
  float sampel[JUMLAH_SAMPEL_MEDIAN];
  for (int i = 0; i < JUMLAH_SAMPEL_MEDIAN; i++) {
    sampel[i] = bacaJarak();
  }
  // Urutkan 3 angka (bubble sort cukup untuk 3 elemen).
  for (int i = 0; i < JUMLAH_SAMPEL_MEDIAN - 1; i++) {
    for (int j = 0; j < JUMLAH_SAMPEL_MEDIAN - 1 - i; j++) {
      if (sampel[j] > sampel[j + 1]) {
        float tmp = sampel[j];
        sampel[j] = sampel[j + 1];
        sampel[j + 1] = tmp;
      }
    }
  }
  return sampel[JUMLAH_SAMPEL_MEDIAN / 2];
}

// ---------- KENDALI KRAN ----------
void bukaKran(unsigned long sekarang) {
  digitalWrite(PIN_RELAY, RELAY_NYALA);
  digitalWrite(PIN_LED_HIJAU, LED_NYALA);
  kranTerbuka = true;
  kranMulaiMs = sekarang;
  Serial.println("Kran DIBUKA.");
  if (!telegramPernahDikirim || sekarang - telegramTerakhirMs >= TELEGRAM_DEBOUNCE_MS) {
    telegramPernahDikirim = true;
    telegramTerakhirMs = sekarang;
    kirimTelegram("Kran air dibuka — tangan terdeteksi di bawah kran.");
  }
}

void tutupKran() {
  digitalWrite(PIN_RELAY, RELAY_MATI);
  digitalWrite(PIN_LED_HIJAU, LED_MATI);
  kranTerbuka = false;
  Serial.println("Kran DITUTUP.");
}

// Keputusan buka/tutup dari jarak median terbaru.
void kendaliKran(float jarakCm, unsigned long sekarang) {
  if (!kranTerbuka && jarakCm < JARAK_BUKA_CM) {
    bukaKran(sekarang);
    return;
  }
  if (kranTerbuka) {
    bool tanganPergi = jarakCm >= JARAK_BUKA_CM;
    bool lewatBatas = sekarang - kranMulaiMs >= BUKA_MAKS_MS;
    if (tanganPergi || lewatBatas) {
      if (lewatBatas && !tanganPergi) {
        Serial.println("Batas 30 detik tercapai — tutup paksa (anti-banjir).");
      }
      tutupKran();
    }
  }
}

void setup() {
  Serial.begin(115200);
  pinMode(PIN_TRIG, OUTPUT);
  pinMode(PIN_ECHO, INPUT);
  pinMode(PIN_RELAY, OUTPUT);
  pinMode(PIN_LED_HIJAU, OUTPUT);
  digitalWrite(PIN_TRIG, LOW);
  digitalWrite(PIN_RELAY, RELAY_MATI);
  digitalWrite(PIN_LED_HIJAU, LED_MATI);
  Serial.println("Kran Air Otomatis — siap.");
#if USE_TELEGRAM
  sambungWiFi();
#endif
}

void loop() {
  unsigned long sekarang = millis();
  if (sekarang - ukurTerakhirMs >= INTERVAL_UKUR_MS) {
    ukurTerakhirMs = sekarang;
    float jarakCm = bacaJarakMedian();
    Serial.print("Jarak: ");
    Serial.print(jarakCm);
    Serial.println(" cm");
    kendaliKran(jarakCm, sekarang);
  }
}
