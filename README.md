# Kran Air Otomatis — ESP32 + HC-SR04 + Solenoid Valve

Kran air yang membuka dan menutup sendiri. Sensor ultrasonik HC-SR04
mengukur jarak tanganmu; begitu tangan mendekat kurang dari 15 cm, relay
menyalakan katup solenoid 12V dan air mengalir — tanpa menyentuh kran.
Begitu tangan pergi, kran menutup. Kalau kran menyala terus 30 detik
(misalnya sensor tertutup benda), kran menutup paksa — anti-banjir.

Video penjelasan (60 detik):
https://www.youtube.com/shorts/a5_g42C46RY

## Komponen

| Bagian | Keterangan |
|---|---|
| ESP32 DevKit | Otak alat |
| HC-SR04 | Sensor jarak ultrasonik (TRIG / ECHO) |
| Relay 1 channel | Saklar katup solenoid (aktif LOW) |
| Solenoid valve 12V (NC) | Katup air, buka saat relay ON |
| Adaptor 12V 1A | Catu daya solenoid (terpisah) |
| LED hijau | Menyala saat kran terbuka |

## Wiring

| Dari | Ke |
|---|---|
| HC-SR04 TRIG | ESP32 GPIO5 |
| HC-SR04 ECHO | ESP32 GPIO18 |
| HC-SR04 VCC | ESP32 VIN 5V |
| Relay IN | ESP32 GPIO26 |
| Relay VCC | ESP32 3V3 |
| LED hijau | ESP32 GPIO32 (seri 220 Ω ke GND) |
| Relay NO | Solenoid valve (satu kabel) |
| Relay COM | − adaptor 12V |
| Solenoid valve (kabel lain) | + adaptor 12V |
| Semua GND ESP32 | Digabung (rail GND) |

GND adaptor 12V **tidak** digabung ke GND ESP32 — hanya lewat kontak
relay (COM). Solenoid 12V **jangan** dinyalakan dari pin 5V ESP32,
arusnya tidak cukup.

Skema lengkap: [schematic.png](schematic.png)

## Cara kerja

1. ESP32 mengukur jarak tiap 100 ms; hasil difilter median dari 3 bacaan
   supaya satu bacaan liar tidak membuka kran.
2. Jarak median < 15 cm → relay ON, katup solenoid membuka, air mengalir,
   LED hijau menyala, notifikasi Telegram terkirim (bila `USE_TELEGRAM` 1,
   debounce 30 detik).
3. Tangan pergi (jarak ≥ 15 cm) → relay OFF, kran menutup.
4. Kran menyala terus 30 detik → tutup paksa (anti-banjir).
5. Echo timeout 30 ms — pengukuran tidak pernah hang bila tidak ada gema.

## Konfigurasi (di awal sketch)

- `USE_TELEGRAM` (1) — kirim notifikasi Telegram saat kran dibuka.
  Set 0 untuk tanpa Telegram (tanpa WiFi).
- `WIFI_SSID` / `WIFI_PASS` — WiFi rumahmu.
- `BOT_TOKEN` / `CHAT_ID` — token bot dan chat ID Telegram.
- `JARAK_BUKA_CM` (15) — tangan lebih dekat dari ini = kran buka.
- `INTERVAL_UKUR_MS` (100) — jeda antar pengukuran jarak.
- `JUMLAH_SAMPEL_MEDIAN` (3) — jumlah bacaan untuk filter median.
- `ECHO_TIMEOUT_US` (30000) — timeout echo 30 ms.
- `BUKA_MAKS_MS` (30000) — kran tutup paksa setelah 30 detik.
- `TELEGRAM_DEBOUNCE_MS` (30000) — jeda minimum antar notifikasi.

## Library yang dibutuhkan

| Library | Versi | Keterangan |
|---|---|---|
| UniversalTelegramBot | 1.3.0 | Notifikasi Telegram |
| ArduinoJson | 6.21.5 | Dipakai UniversalTelegramBot (WAJIB v6 — v7 tidak dipakai di sini) |

Semua library di atas juga tersedia sebagai ZIP di folder [`libraries/`](libraries/)
— install lewat Arduino IDE: Sketch → Include Library → Add .ZIP Library.

## Compile

```sh
arduino-cli compile --fqbn esp32:esp32:esp32 KranAir
```

Terverifikasi 20 Nov 2026: sketch 1.066.257 byte (81% flash),
RAM 50.528 byte (15%), ESP32 core 4.0.0.

## Lisensi

MIT — lihat [LICENSE](LICENSE).
