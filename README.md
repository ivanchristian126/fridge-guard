# FridgeGuard (Kelompok 7)

Sistem IoT untuk memantau suhu, kelembapan, dan pintu kulkas secara real-time. Tugas mata kuliah Embedded System.

- Sensor: DHT22 (suhu & kelembapan) + magnetic switch (pintu terbuka/tertutup)
- Aktuator: LED kuning (D19) + LED biru bawaan ESP32 (D2)
- Mikrokontroler: ESP32
- Komunikasi: MQTT lewat broker publik HiveMQ (`broker.hivemq.com`)
- Dashboard: halaman web statis (HTML + JavaScript), di-hosting di Azure Static Web Apps

## Alur data

```
ESP32 --(MQTT, port 1883)--> broker.hivemq.com --(MQTT over WSS, port 8884)--> Dashboard web (browser)
```

Dashboard tidak punya backend. Browser pengunjung langsung subscribe ke broker, jadi website tetap bisa dibuka kapan saja. Data muncul saat ESP32 menyala; kalau ESP32 mati, dashboard menampilkan "Alat: offline".

## Struktur folder

```
dashboard/                         Website dashboard (yang di-deploy ke Azure)
  index.html
  mqtt.min.js                      Library MQTT.js (lisensi MIT, lihat mqtt-LICENSE.md)
firmware/
  FridgeGuard_MQTT/                Versi online: kirim data ke dashboard lewat MQTT
    FridgeGuard_MQTT.ino
    src/PubSubClient/              Library PubSubClient 2.8 (sudah dibundel)
  Arduino_buat_kulkas/             Versi offline (tanpa WiFi)
    Arduino_buat_kulkas.ino
.github/workflows/
  github-pages.yml                 Deploy otomatis dashboard ke GitHub Pages tiap push (gratis, tanpa secret)
  azure-static-web-apps.yml        Deploy otomatis dashboard ke Azure tiap push (butuh token Azure)
```

## Topik MQTT

| Topik | Isi | Retained |
|---|---|---|
| `fridgeguard/kel-7/data` | JSON tiap 1 detik: `suhu`, `lembap`, `pintu`, `alarm`, `buka_detik`, `uptime` | Tidak |
| `fridgeguard/kel-7/pintu` | `TERBUKA` / `TERTUTUP` saat berubah | Ya |
| `fridgeguard/kel-7/alarm` | `ON` / `OFF` saat berubah | Ya |
| `fridgeguard/kel-7/status` | `online` / `offline` (offline otomatis lewat Last Will) | Ya |

## Rangkaian

| Komponen | Pin ESP32 |
|---|---|
| DHT22 (modul `+ out -`) | `+` ke 3V3, `-` ke GND, `out` ke D4 |
| Magnetic switch | kabel 1 ke D18, kabel 2 ke GND |
| LED kuning | D19 ke resistor 220 ohm ke kaki panjang LED, kaki pendek ke GND |
| LED biru | bawaan board (D2) |

Perilaku LED: pintu tertutup = semua mati; pintu terbuka sampai 5 detik = LED kuning nyala; pintu terbuka lebih dari 5 detik = LED kuning + biru kedip (peringatan).

## Upload firmware ke ESP32

1. Buka `firmware/FridgeGuard_MQTT/FridgeGuard_MQTT.ino` di Arduino IDE.
2. Install library "DHT sensor library" dan "Adafruit Unified Sensor" (Adafruit) dari Library Manager. PubSubClient sudah ikut di folder `src/`.
3. Isi `WIFI_SSID` dan `WIFI_PASS` di bagian atas file (ESP32 hanya bisa WiFi 2.4 GHz, paling aman pakai hotspot HP). Jangan commit password asli ke GitHub.
4. Pilih board "ESP32 Dev Module", upload, lalu buka Serial Monitor 115200. Tunggu tulisan `[MQTT] Konek ke broker... OK`.

## Deploy ke GitHub Pages (cara paling gampang)

Dashboard di-deploy otomatis oleh GitHub Actions (`.github/workflows/github-pages.yml`) setiap ada
perubahan di folder `dashboard/`. **Tidak butuh secret atau akun Azure.**

Setup sekali saja (butuh akses admin repo):

1. Buka repo di GitHub: **Settings > Pages**.
2. Bagian **Build and deployment > Source**, pilih **GitHub Actions**, lalu simpan.
3. Buka tab **Actions**, pilih workflow "Deploy dashboard ke GitHub Pages", klik **Run workflow**.
4. Kalau sudah hijau, link website muncul di ringkasan run itu dan di **Settings > Pages**
   (formatnya `https://<user>.github.io/fridge-guard/`).

Setelah itu, setiap push yang mengubah isi `dashboard/` akan otomatis ter-deploy ulang.

### Apakah dashboard di GitHub Pages tetap dapat data dari ESP32?

Ya. GitHub Pages hanya bertugas mengirim file statis (`index.html` + `mqtt.min.js`) ke browser
pengunjung. Pengambilan data tidak lewat GitHub sama sekali: begitu halaman terbuka, **browser
pengunjung sendiri** yang konek ke broker dan subscribe ke topik.

```
ESP32 --(MQTT 1883)--> broker.hivemq.com <--(MQTT over WSS 8884)-- browser pengunjung
                                                                        ^
                            GitHub Pages --(kirim file statis via HTTPS)-+
```

Dua hal yang membuat ini jalan:

- Dashboard sudah memakai `wss://` (WebSocket terenkripsi), bukan `ws://`. GitHub Pages selalu
  HTTPS, dan browser memblokir koneksi `ws://` yang tidak terenkripsi dari halaman HTTPS.
- Topik di firmware dan dashboard sama persis (`fridgeguard/kel-7`), jadi data yang dipublish
  ESP32 langsung terbaca dashboard.

Catatan: broker HiveMQ publik itu terbuka tanpa password, dan situs GitHub Pages bisa dibuka siapa
saja. Untuk tugas kuliah tidak masalah, tapi jangan kirim data sensitif ke topik ini.

## Deploy ke Azure (Static Web Apps)

Alternatif kalau mau pakai Azure. Dashboard di-deploy otomatis oleh GitHub Actions
(`.github/workflows/azure-static-web-apps.yml`) setiap ada perubahan di folder `dashboard/`.
Workflow ini butuh deployment token dari Azure. Kalau cuma mau pakai GitHub Pages, workflow ini
boleh dibiarkan saja (akan gagal sampai secret-nya dipasang) atau filenya dihapus.

Yang punya akun Azure:

1. Azure Portal, buat resource **Static Web App**.
2. Plan type: **Free**. Region bebas.
3. Deployment source: pilih **Other** (bukan GitHub), supaya Azure tidak membuat workflow sendiri yang dobel dengan file di repo ini.
4. Setelah jadi, buka resource-nya, klik **Manage deployment token**, lalu salin token-nya dan kirim ke pemilik repo GitHub lewat jalur pribadi.

Yang punya repo GitHub:

1. Buka repo di GitHub: **Settings > Secrets and variables > Actions > New repository secret**.
2. Name: `AZURE_STATIC_WEB_APPS_API_TOKEN`, Secret: tempel token dari Azure, lalu simpan.
3. Buka tab **Actions**, pilih workflow "Deploy dashboard ke Azure Static Web Apps", klik **Run workflow**. (Kalau run pertama sempat gagal karena secret belum dipasang, itu normal; jalankan ulang setelah secret ada.)
4. Kalau sudah hijau, link website ada di halaman Overview resource Static Web App di Azure (formatnya `https://<nama-acak>.azurestaticapps.net`).

Setelah itu, setiap push yang mengubah isi `dashboard/` akan otomatis ter-deploy ulang.

## Membuka dashboard secara lokal

Double-click `dashboard/index.html` (butuh internet untuk konek ke broker). File `mqtt.min.js` harus tetap satu folder dengan `index.html`.
