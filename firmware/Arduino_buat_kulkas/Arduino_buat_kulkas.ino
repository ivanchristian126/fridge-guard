// =====================================================
// FridgeGuard - 2 sensor + 1 aktuator (versi offline)
// Sensor  : DHT22 (suhu & kelembapan) + magnetic switch (pintu)
// Aktuator: LED kuning + LED biru bawaan ESP32 (D2)
// Library : "DHT sensor library" + "Adafruit Unified Sensor" (Adafruit)
// Board   : ESP32 Dev Module | Serial Monitor: 115200
// =====================================================
//
// RANGKAIAN (semua di sisi pin 3V3, board ESP32 30 pin):
// DHT22 (modul "+ out -"):
//   +   -> pin 3V3
//   -   -> GND (titik GND yang terbukti nyambung)
//   out -> D4
// Magnetic switch:
//   kabel 1 -> D18 | kabel 2 -> GND (bebas, tidak ada +/-)
// LED (lewat resistor 220 ohm):
//   D19 -> resistor -> kaki panjang LED | kaki pendek LED -> GND
//   (resistor harus nyebrang kolom, contoh: kabel D19 & resistor di kolom 15,
//    resistor & kaki panjang LED di kolom 10)
// LED biru: bawaan board ESP32 (D2), tidak perlu dirangkai
// Catatan breadboard: jalur power kiri-kanan terputus di tengah (kolom ~30).
//   Pakai GND di belahan yang sama dengan ESP32, atau jembatani pakai jumper.
//
// PERILAKU LED:
// Pintu tertutup                -> semua LED mati
// Pintu terbuka (<= 5 detik)    -> LED kuning nyala terus
// Pintu terbuka (> 5 detik)     -> PERINGATAN: LED kuning + LED biru kedip bareng
//                                  sampai pintu ditutup
// =====================================================

#include <DHT.h>

// ---- Pin ----
#define DHT_PIN   4
#define DHT_TYPE  DHT22
#define REED_PIN  18
#define LED_PIN   19
#define LED_BIRU  2    // LED biru bawaan ESP32

// Lama pintu boleh terbuka sebelum LED mulai kedip (ms)
const unsigned long BATAS_PINTU_TERBUKA = 5000;

// Kecepatan kedip LED saat alarm (ms)
const unsigned long INTERVAL_KEDIP = 250;

// Interval baca DHT22 (ms) - DHT22 minimal 2 detik
const unsigned long INTERVAL_DHT = 2000;

// Magnet didekatkan = pintu TERTUTUP -> false
// (ganti true hanya kalau terbaca kebalik)
const bool REED_TERBALIK = false;

DHT dht(DHT_PIN, DHT_TYPE);

unsigned long waktuBacaDHT = 0;
unsigned long waktuPintuDibuka = 0;
bool pintuTerbukaSebelumnya = false;
bool alarmSebelumnya = false;

bool bacaPintuTerbuka() {
  // INPUT_PULLUP: magnet dekat -> switch nyambung -> LOW -> pintu tertutup
  bool terbuka = (digitalRead(REED_PIN) == HIGH);
  return REED_TERBALIK ? !terbuka : terbuka;
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
  Serial.println("=== FridgeGuard: DHT22 + Magnetic Switch + 2 LED ===");

  // Tes LED: kuning & biru kedip 3x saat baru nyala
  for (int i = 0; i < 3; i++) {
    digitalWrite(LED_PIN, HIGH); digitalWrite(LED_BIRU, HIGH); delay(200);
    digitalWrite(LED_PIN, LOW);  digitalWrite(LED_BIRU, LOW);  delay(200);
  }
  Serial.println("Kalau LED kuning & biru kedip 3x -> LED OK.");

  pintuTerbukaSebelumnya = bacaPintuTerbuka();
  if (pintuTerbukaSebelumnya) waktuPintuDibuka = millis();
  Serial.println(pintuTerbukaSebelumnya ? "[PINTU] awal: TERBUKA" : "[PINTU] awal: TERTUTUP");
}

void loop() {
  // ---------- Magnetic switch (dengan debounce) ----------
  bool pintuTerbuka = bacaPintuTerbuka();
  if (pintuTerbuka != pintuTerbukaSebelumnya) {
    delay(50);  // debounce
    pintuTerbuka = bacaPintuTerbuka();
    if (pintuTerbuka != pintuTerbukaSebelumnya) {
      pintuTerbukaSebelumnya = pintuTerbuka;
      if (pintuTerbuka) waktuPintuDibuka = millis();
      Serial.println(pintuTerbuka ? "[PINTU] TERBUKA" : "[PINTU] TERTUTUP");
    }
  }

  // ---------- LED ----------
  bool alarm = pintuTerbuka && (millis() - waktuPintuDibuka > BATAS_PINTU_TERBUKA);

  if (alarm && !alarmSebelumnya) {
    Serial.printf("[ALARM] Pintu terbuka > %lu detik -> LED kuning + biru kedip\n", BATAS_PINTU_TERBUKA / 1000);
  }
  alarmSebelumnya = alarm;

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

  // ---------- DHT22 ----------
  if (millis() - waktuBacaDHT >= INTERVAL_DHT) {
    waktuBacaDHT = millis();
    float suhu = dht.readTemperature();
    float lembap = dht.readHumidity();
    if (isnan(suhu) || isnan(lembap)) {
      Serial.println("[DHT22] Gagal baca! Cek kabel DATA/VCC/GND.");
    } else {
      Serial.printf("[DHT22] Suhu: %.1f C | Kelembapan: %.1f %%\n", suhu, lembap);
    }
  }
}