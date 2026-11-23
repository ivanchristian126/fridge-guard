// =====================================================
// FridgeGuard - versi MQTT (kirim data ke dashboard)
// Sensor  : DHT22 (suhu & kelembapan) + magnetic switch (pintu)
// Aktuator: LED kuning + LED biru bawaan ESP32 (D2)
// Library : "DHT sensor library" + "Adafruit Unified Sensor" (Adafruit)
//           PubSubClient 2.8 sudah ikut di folder src/ (tidak perlu install)
// Board   : ESP32 Dev Module | Serial Monitor: 115200
// =====================================================
//
// RANGKAIAN (sama seperti versi offline):
// DHT22 (modul "+ out -"): + -> 3V3 | - -> GND | out -> D4
// Magnetic switch        : kabel 1 -> D18 | kabel 2 -> GND
// LED kuning             : D19 -> resistor 220 ohm -> kaki panjang | kaki pendek -> GND
// LED biru               : bawaan board (D2)
//
// MQTT (broker publik HiveMQ):
// <TOPIK>/data   -> JSON tiap 1 detik
// <TOPIK>/pintu  -> "TERBUKA" / "TERTUTUP" saat berubah (retained)
// <TOPIK>/alarm  -> "ON" / "OFF" saat berubah (retained)
// <TOPIK>/status -> "online" / "offline" (retained, offline otomatis lewat LWT)
// =====================================================

#include <WiFi.h>
#include "src/PubSubClient/PubSubClient.h"   // library sudah dibundel di folder src, tidak perlu install
#include <DHT.h>

// ---- WiFi (ESP32 hanya bisa WiFi 2.4 GHz) ----
const char* WIFI_SSID = "NAMA_HOTSPOT";     // ganti dengan nama WiFi/hotspot (2.4 GHz)
const char* WIFI_PASS = "PASSWORD_HOTSPOT"; // ganti dengan password WiFi/hotspot

// ---- MQTT ----
const char* MQTT_HOST = "broker.hivemq.com";
const uint16_t MQTT_PORT = 1883;
#define TOPIK "fridgeguard/kel-7"   // harus sama persis dengan di dashboard

// ---- Pin ----
#define DHT_PIN   4
#define DHT_TYPE  DHT22
#define REED_PIN  18
#define LED_PIN   19
#define LED_BIRU  2    // LED biru bawaan ESP32

// Lama pintu boleh terbuka sebelum peringatan (ms)
const unsigned long BATAS_PINTU_TERBUKA = 5000;

// Kecepatan kedip LED saat peringatan (ms)
const unsigned long INTERVAL_KEDIP = 250;

// Interval kirim data ke dashboard (ms)
const unsigned long INTERVAL_KIRIM = 1000;

// Interval baca DHT22 (ms) - DHT22 tidak bisa lebih cepat dari 2 detik,
// jadi di antara dua bacaan yang dikirim adalah nilai terakhir
const unsigned long INTERVAL_DHT = 2000;

// Magnet didekatkan = pintu TERTUTUP -> false
// (ganti true hanya kalau terbaca kebalik)
const bool REED_TERBALIK = false;

DHT dht(DHT_PIN, DHT_TYPE);
WiFiClient wifiClient;
PubSubClient mqtt(wifiClient);

char clientId[32];
unsigned long waktuBacaDHT = 0;
unsigned long waktuKirim = 0;
float suhuTerakhir = NAN;
float lembapTerakhir = NAN;
unsigned long waktuPintuDibuka = 0;
unsigned long waktuCobaMqtt = 0;
bool pintuTerbukaSebelumnya = false;
bool alarmSebelumnya = false;
bool wifiSebelumnya = false;

bool bacaPintuTerbuka() {
  // INPUT_PULLUP: magnet dekat -> switch nyambung -> LOW -> pintu tertutup
  bool terbuka = (digitalRead(REED_PIN) == HIGH);
  return REED_TERBALIK ? !terbuka : terbuka;
}

// Kirim ke MQTT kalau sedang terhubung (kalau tidak, dilewati saja)
void kirim(const char* topik, const char* isi, bool retained) {
  if (mqtt.connected()) mqtt.publish(topik, isi, retained);
}

// Coba konek ke broker tiap 5 detik, tanpa menghentikan logika LED
void cekKoneksi() {
  bool wifiOk = (WiFi.status() == WL_CONNECTED);
  if (wifiOk != wifiSebelumnya) {
    wifiSebelumnya = wifiOk;
    if (wifiOk) {
      Serial.print("[WIFI] Terhubung, IP: ");
      Serial.println(WiFi.localIP());
    } else {
      Serial.println("[WIFI] Terputus, mencoba ulang...");
    }
  }
  if (!wifiOk || mqtt.connected()) return;
  if (millis() - waktuCobaMqtt < 5000) return;
  waktuCobaMqtt = millis();

  Serial.print("[MQTT] Konek ke broker... ");
  // LWT: kalau ESP32 putus tiba-tiba, broker kirim "offline" ke TOPIK/status
  if (mqtt.connect(clientId, TOPIK "/status", 1, true, "offline")) {
    Serial.println("OK");
    kirim(TOPIK "/status", "online", true);
    kirim(TOPIK "/pintu", pintuTerbukaSebelumnya ? "TERBUKA" : "TERTUTUP", true);
    kirim(TOPIK "/alarm", alarmSebelumnya ? "ON" : "OFF", true);
  } else {
    Serial.printf("gagal (kode %d), coba lagi 5 detik\n", mqtt.state());
  }
}

void setup() {
  Serial.begin(115200);
  pinMode(REED_PIN, INPUT_PULLUP);
  pinMode(LED_PIN, OUTPUT);
  pinMode(LED_BIRU, OUTPUT);
  digitalWrite(LED_PIN, LOW);
  digitalWrite(LED_BIRU, LOW);
  dht.begin();

  delay(500);
  Serial.println();
  Serial.println("=== FridgeGuard: DHT22 + Magnetic Switch + 2 LED + MQTT ===");

  // Tes LED: kuning & biru kedip 3x saat baru nyala
  for (int i = 0; i < 3; i++) {
    digitalWrite(LED_PIN, HIGH); digitalWrite(LED_BIRU, HIGH); delay(200);
    digitalWrite(LED_PIN, LOW);  digitalWrite(LED_BIRU, LOW);  delay(200);
  }
  Serial.println("Kalau LED kuning & biru kedip 3x -> LED OK.");

  pintuTerbukaSebelumnya = bacaPintuTerbuka();
  if (pintuTerbukaSebelumnya) waktuPintuDibuka = millis();
  Serial.println(pintuTerbukaSebelumnya ? "[PINTU] awal: TERBUKA" : "[PINTU] awal: TERTUTUP");

  // ID unik dari MAC address, supaya tidak bentrok dengan alat lain di broker publik
  uint64_t mac = ESP.getEfuseMac();
  snprintf(clientId, sizeof(clientId), "fridgeguard-%08lX", (unsigned long)(mac & 0xFFFFFFFF));

  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASS);
  Serial.printf("[WIFI] Menghubungkan ke %s ...\n", WIFI_SSID);

  mqtt.setServer(MQTT_HOST, MQTT_PORT);
  mqtt.setBufferSize(512);
}

void loop() {
  // ---------- Koneksi WiFi & MQTT ----------
  cekKoneksi();
  mqtt.loop();

  // ---------- Magnetic switch (dengan debounce) ----------
  bool pintuTerbuka = bacaPintuTerbuka();
  if (pintuTerbuka != pintuTerbukaSebelumnya) {
    delay(50);  // debounce
    pintuTerbuka = bacaPintuTerbuka();
    if (pintuTerbuka != pintuTerbukaSebelumnya) {
      pintuTerbukaSebelumnya = pintuTerbuka;
      if (pintuTerbuka) waktuPintuDibuka = millis();
      Serial.println(pintuTerbuka ? "[PINTU] TERBUKA" : "[PINTU] TERTUTUP");
      kirim(TOPIK "/pintu", pintuTerbuka ? "TERBUKA" : "TERTUTUP", true);
    }
  }

  // ---------- Peringatan ----------
  bool alarm = pintuTerbuka && (millis() - waktuPintuDibuka > BATAS_PINTU_TERBUKA);
  if (alarm != alarmSebelumnya) {
    alarmSebelumnya = alarm;
    if (alarm) {
      Serial.printf("[ALARM] Pintu terbuka > %lu detik -> LED kuning + biru kedip\n", BATAS_PINTU_TERBUKA / 1000);
    } else {
      Serial.println("[ALARM] Selesai");
    }
    kirim(TOPIK "/alarm", alarm ? "ON" : "OFF", true);
  }

  // ---------- LED ----------
  bool fasaKedip = (millis() / INTERVAL_KEDIP) % 2 == 0;
  if (!pintuTerbuka) {
    // Tertutup: semua mati
    digitalWrite(LED_PIN, LOW);
    digitalWrite(LED_BIRU, LOW);
  } else if (!alarm) {
    // Baru dibuka: kuning nyala terus, biru mati
    digitalWrite(LED_PIN, HIGH);
    digitalWrite(LED_BIRU, LOW);
  } else {
    // Peringatan: kuning & biru kedip bareng
    digitalWrite(LED_PIN, fasaKedip ? HIGH : LOW);
    digitalWrite(LED_BIRU, fasaKedip ? HIGH : LOW);
  }

  // ---------- DHT22 tiap 2 detik ----------
  if (millis() - waktuBacaDHT >= INTERVAL_DHT) {
    waktuBacaDHT = millis();
    suhuTerakhir = dht.readTemperature();
    lembapTerakhir = dht.readHumidity();
    if (isnan(suhuTerakhir) || isnan(lembapTerakhir)) {
      Serial.println("[DHT22] Gagal baca! Cek kabel DATA/VCC/GND.");
    } else {
      Serial.printf("[DHT22] Suhu: %.1f C | Kelembapan: %.1f %%\n", suhuTerakhir, lembapTerakhir);
    }
  }

  // ---------- Kirim data ke dashboard tiap 1 detik ----------
  if (millis() - waktuKirim >= INTERVAL_KIRIM) {
    waktuKirim = millis();

    char suhuStr[12], lembapStr[12];
    if (isnan(suhuTerakhir) || isnan(lembapTerakhir)) {
      strcpy(suhuStr, "null");
      strcpy(lembapStr, "null");
    } else {
      snprintf(suhuStr, sizeof(suhuStr), "%.1f", suhuTerakhir);
      snprintf(lembapStr, sizeof(lembapStr), "%.1f", lembapTerakhir);
    }

    unsigned long bukaDetik = pintuTerbuka ? (millis() - waktuPintuDibuka) / 1000 : 0;
    char data[200];
    snprintf(data, sizeof(data),
             "{\"suhu\":%s,\"lembap\":%s,\"pintu\":\"%s\",\"alarm\":%s,\"buka_detik\":%lu,\"uptime\":%lu}",
             suhuStr, lembapStr,
             pintuTerbuka ? "TERBUKA" : "TERTUTUP",
             alarm ? "true" : "false",
             bukaDetik, millis() / 1000);
    kirim(TOPIK "/data", data, false);
    if (mqtt.connected()) {
      Serial.print("[MQTT] Kirim: ");
      Serial.println(data);
    }
  }
}
