ZEVRON — ESP32-C3 Personal Desk Companion
ZEVRON adalah robot pendamping meja (desk companion) berbasis ESP32-C3 Super Mini, layar OLED SSD1306 0,96 inci (128 × 64), satu sensor sentuh TTP223, dan buzzer. Proyek ini menggabungkan ekspresi mata animasi, interaksi satu sentuhan, navigasi Google Maps melalui Chronos BLE, mode mimpi, dan mini-game retro. Casing kotak dirancang menggunakan FreeCAD.
Status proyek: Prototipe / pengembangan aktif. README ini merujuk pada firmware ZEVRON_DREAM_MODE_MINIGAMES_FINAL.ino dan macro casing Zevron_Kotak_FreeCAD.FCMacro. Keduanya belum dinyatakan lolos pengujian kompilasi dan hardware secara menyeluruh.

Fitur utama
- RoboEyes: wajah dan ekspresi animasi di OLED, dengan pilihan personality.
- Dream Mode: mulai mengantuk setelah 5 menit tanpa interaksi; tidur setelah 8 menit total (5 + 3 menit); menampilkan mimpi setiap 30 detik. Sentuhan membangunkan Zevron.
- Chronos BLE: koneksi ponsel diaktifkan atau dimatikan secara manual; default OFF saat boot.
- Google Maps Auto Navigation: saat data navigasi aktif diterima dari aplikasi Chronos, layar otomatis beralih ke petunjuk arah. Membutuhkan koneksi BLE dan data navigasi dari aplikasi ponsel.
- Mini Games: Dino Runner (melompati rintangan) dan Fishing Zevron (melempar kail dengan timing yang tepat).
- Alien Signal / Alien Log: tampilan dan catatan bertema sinyal misterius. Pada varian Dream Mode ini, pemicu pesan alien random saat idle dinonaktifkan agar tidak mengganggu tidur.
- System Monitor, Clock, Weather, Sound FX, Brightness, Light Mode, Deep Sleep manual, dan Zevron Info.
- Wi-Fi on-demand: untuk pengaturan koneksi, cuaca, dan pembaruan firmware; tidak harus menyala terus-menerus.
- OTA firmware: pemeriksaan rilis terbaru dan pembaruan melalui GitHub, sesuai konfigurasi di source code.
Tidak termasuk: ESP-NOW / penerima pesan KAEL dan fitur Tamagotchi (Happiness/Pet Status). Keduanya telah dihapus dari varian firmware ini.
Perangkat keras
Komponen	Keterangan
Mikrokontroler	ESP32-C3 Super Mini
Display	OLED SSD1306 I²C, 0,96 inci, 128 × 64, alamat 0x3C
Input	TTP223 touch sensor, 1 unit
Audio	Buzzer sesuai rangkaian yang digunakan
Daya	USB atau sumber daya yang telah diregulasi sesuai kebutuhan board; konfigurasi baterai belum ditetapkan di README ini
Enclosure	Casing kotak 3D hasil desain FreeCAD


Pemetaan pin firmware
Perangkat	Pin ESP32-C3
OLED SDA	GPIO 20
OLED SCL	GPIO 21
TTP223 OUT	GPIO 5
Buzzer	GPIO 6
OLED VCC / GND	Sesuai spesifikasi modul / ground bersama


Perhatian: Pastikan level tegangan TTP223 aman untuk GPIO 3,3 V ESP32-C3. Periksa kembali pinout board fisik sebelum menyambungkan komponen. Jangan menghubungkan sel baterai Li-ion langsung ke pin yang tidak dirancang untuk tegangan sel tersebut.

Library Arduino
Firmware mencantumkan dependensi berikut:
- Adafruit GFX Library
- Adafruit SSD1306
- ChronosESP32
- FluxGarage RoboEyes (FluxGarage_RoboEyes.h)
- WiFiManager
- ArduinoJson
- Library bawaan ESP32 Arduino core: WiFi, Wire, Preferences, HTTPClient, WiFiClientSecure, Update, dan komponen ESP-IDF/FreeRTOS yang tersedia pada versi core yang kompatibel.
Versi library dan ESP32 Arduino core yang tepat perlu disesuaikan dan diuji bersama firmware ini.
Instalasi dan upload firmware
1. Instal Arduino IDE dan dukungan board ESP32 by Espressif Systems.
2. Pasang library yang tercantum di atas melalui Library Manager atau sumber resminya.
3. Buka ZEVRON_DREAM_MODE_MINIGAMES_FINAL.ino.
4. Pilih board ESP32-C3 yang sesuai dengan board fisik, serta port serial yang benar.
5. Jalankan Verify / Compile. Selesaikan error library atau kompatibilitas jika muncul.
6. Upload ke board, lalu buka Serial Monitor untuk melihat status boot dan koneksi.
Pengaturan yang mungkin perlu disesuaikan
Di bagian awal source code terdapat pengaturan:
#define OLED_WIDTH 128
#define OLED_HEIGHT 64
#define OLED_ADDR 0x3C
#define OLED_SDA 20
#define OLED_SCL 21
#define TOUCH_PIN 5
#define BUZZER_PIN 6
Firmware juga memiliki pengaturan koordinat cuaca/waktu yang saat ini ditulis untuk Pangkalpinang (UTC+7), serta endpoint rilis GitHub:
const char* GITHUB_RELEASE_API =
    "https://api.github.com/repos/zaldyprtm/zevron-desk-companion/releases/latest";
Ubah pengaturan tersebut bila perangkat dipakai di lokasi lain atau repositori rilis firmware berubah.
Cara menggunakan Zevron
Navigasi touch dari layar utama
Gesture	Fungsi dalam firmware
3× tap	Clock
4× tap	Weather
5× tap	Google Maps / Navigation
8× tap	Alien Log
9× tap atau lebih	Settings


Di Settings, 1× tap berpindah menu dan 2× tap memilih. Pilih Chronos BLE untuk mengaktifkan atau menonaktifkan BLE secara manual. Pilih Games untuk membuka daftar mini-game.
Mini-game
Dino Runner
- Tap untuk melompat.
- Hindari rintangan yang bergerak dari kanan.
- Skor bertambah ketika rintangan berhasil dilewati.
Fishing Zevron
- Tap untuk melempar kail.
- Cocokkan timing lemparan dengan posisi ikan.
- Permainan berakhir setelah lima lemparan gagal.
Di menu Games: 1× tap mengganti pilihan, 2× tap memulai. Dalam permainan: tap untuk aksi atau mencoba lagi setelah game over. Long press dimaksudkan untuk keluar ke menu Games; pengaturan timing touch dan perilakunya perlu diuji di perangkat nyata.
Skor terbaik pada implementasi mini-game ini disimpan dalam variabel RAM, sehingga tidak dijamin bertahan setelah restart.

Dream Mode
Kondisi	Waktu tanpa touch
Normal / Awake	0–5 menit
Sleepy	Mulai menit ke-5
Sleeping	Mulai menit ke-8
Dream vignette	Setiap 30 detik selama tidur


Dream Mode merupakan animasi tidur OLED, bukan deep sleep mikrokontroler. BLE yang telah diaktifkan dan layanan lain yang diperlukan tetap dapat bekerja. Navigasi Google Maps memiliki prioritas tampilan saat aktif.
Casing 3D — FreeCAD
File macro: Zevron_Kotak_FreeCAD.FCMacro.
Dimensi prototipe
Parameter	Nilai
Lebar (X)	55 mm
Tinggi (Z)	45 mm
Kedalaman (Y)	30 mm
Tebal dinding	2 mm
Bukaan OLED pada macro saat ini	27 × 14 mm
Posisi awal bukaan OLED	Terpusat horizontal, dimulai pada Z = 19 mm
Slot USB samping	10 × 5 mm (posisi sementara)
Lubang buzzer	4 lubang kecil pada panel depan
Tutup belakang	Bagian terpisah dengan bibir press-fit percobaan


Penting: Bukaan OLED 27 × 14 mm di macro merupakan ukuran awal, bukan jaminan pas untuk seluruh modul OLED 0,96 inci. Ukur area aktif, kaca, PCB, lubang pemasangan, konektor, dan tinggi komponen sebenarnya menggunakan jangka sorong. Bila hanya ingin menampilkan piksel aktif, bukaan yang lebih kecil mungkin lebih rapi, tetapi harus dipastikan tidak menutup piksel. Dudukan PCB internal dan toleransi cetak juga masih perlu disempurnakan.
Membuka macro di FreeCAD
1. Buka FreeCAD.
2. Masuk ke Macro → Macros.
3. Buat atau buka macro .FCMacro tersebut.
4. Jalankan macro untuk membuat CaseBody dan BackLid.
5. Simpan proyek sebagai .FCStd agar bisa diedit kembali.
6. Pilih komponen casing dan tutup secara terpisah, lalu File → Export → STL Mesh untuk kebutuhan 3D printing.
7. Periksa orientasi, ketebalan dinding, toleransi, dan clearance menggunakan slicer sebelum mencetak.
Macro menghasilkan geometri awal dengan operasi Part; belum merupakan model parametrik Part Design lengkap dengan semua dudukan komponen dan baut.
Struktur file yang disarankan
ZEVRON/
├── README.md
├── firmware/
│   └── ZEVRON_DREAM_MODE_MINIGAMES_FINAL.ino
└── enclosure/
    └── Zevron_Kotak_FreeCAD.FCMacro
Catatan pengembangan
- Penggunaan millis() membantu animasi dan permainan tetap responsif, tetapi tidak otomatis berarti konsumsi listrik minimum.
- BLE tetap OFF saat boot dan diaktifkan secara manual.
- ESP-NOW telah dihapus; tidak ada mode penerima pesan KAEL pada varian ini.
- Fitur cuaca, OTA, dan Chronos memerlukan konfigurasi atau konektivitas yang sesuai.
- Uji firmware melalui Verify, uji touch fisik, uji navigasi, dan uji casing sebelum menganggap proyek siap dipakai rutin.
- Jangan menyertakan kata sandi Wi-Fi, token, atau informasi pribadi saat mengunggah source code ke repositori publik.
Kredit
ZEVRON — Personal Desk Companion
Project by ZDYVOID. Dibangun menggunakan ekosistem ESP32, Arduino, FreeCAD, dan library open-source pihak ketiga. Hak cipta dan lisensi masing-masing dependensi mengikuti proyek asalnya.
