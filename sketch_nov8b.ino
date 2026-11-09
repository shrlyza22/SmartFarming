#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <DHT.h>

// ================== WIFI & GOOGLE SHEETS ==================
// ESP32 hanya bisa WiFi 2.4 GHz
const char* WIFI_SSID     = "ciseke";
const char* WIFI_PASSWORD = "qwertyuiop";

// URL Web App dari Apps Script (berakhiran /exec)
const char* URL_SHEETS = "https://script.google.com/macros/s/AKfycbxDFIOt9TDm71rk37RO9WJfUJ2cN6iPz10bB-dA839e8GeyitB3lvZDUZ5PAmRVeF8J/exec";

const unsigned long INTERVAL_SHEETS = 8000;  // kirim ke spreadsheet tiap 8 detik

// ================== PIN ==================
#define DHT_PIN    4
#define SOIL_PIN   34
#define RELAY_PIN  26

// ================== MODE TAMPILAN ==================
// false = laporan rapi di Serial Monitor
// true  = angka saja, untuk grafik di Tools > Serial Plotter
const bool MODE_PLOTTER = false;

DHT dht(DHT_PIN, DHT11);

// ================== KALIBRASI SENSOR TANAH ==================
// Lihat "ADC mentah" di Serial Monitor:
//   probe di udara    -> isi NILAI_KERING
//   probe dicelup air -> isi NILAI_BASAH
const int NILAI_KERING = 3000;   // Capacitive Soil Moisture v2.0, probe di udara
const int NILAI_BASAH  = 1300;   // probe dicelup air
const int TOLERANSI    = 400;

// ================== BATAS PENYIRAMAN (%) ==================
const int BATAS_SIRAM = 40;   // di bawah ini pompa nyala
const int BATAS_STOP  = 60;   // di atas ini pompa mati

// ================== PENGAMAN POMPA ==================
const unsigned long MAKS_NYALA_MS = 15UL * 1000;  // maks 15 detik per siraman
const unsigned long JEDA_MIN_MS   = 20UL * 1000;  // jeda min 20 detik antar siraman

// Relay active LOW: LOW = nyala
const int RELAY_ON  = LOW;
const int RELAY_OFF = HIGH;

const unsigned long INTERVAL_BACA  = 2000;  // DHT11 & laporan tiap 2 detik
const unsigned long INTERVAL_TANAH = 5000;   // sensor tanah dibaca tiap 5 detik

// ================== DATA SENSOR ==================
float suhu = 0, lembapUdara = 0;
bool  dhtOk = false;
bool  dhtPernahOk = false;
int   dhtGagalBeruntun = 0;

enum StatusTanah { TANAH_OK, TANAH_NOL, TANAH_MAKS, TANAH_LUAR_KALIBRASI };
int adcTanah = 0;
int persenTanah = 0;
StatusTanah statusTanah = TANAH_NOL;

bool pompaNyala = false;
unsigned long waktuPompaNyala = 0;
unsigned long waktuPompaMati  = 0;
unsigned long waktuBacaTerakhir  = 0;
unsigned long waktuTanahTerakhir = 0;
unsigned long waktuSheetsTerakhir = 0;
int jumlahSiram = 0;
int jumlahTerkirim = 0;

// ============================================================
//                         FUNGSI
// ============================================================

void relayNyala() {
  pinMode(RELAY_PIN, OUTPUT);
  digitalWrite(RELAY_PIN, LOW);   // tarik ke GND, relay aktif
}

void relayMati() {
  pinMode(RELAY_PIN, INPUT);      // lepas pin, modul relay tarik ke 5V, relay pasti mati
}

void setPompa(bool nyala) {
  if (nyala == pompaNyala) return;
  pompaNyala = nyala;
  if (nyala) relayNyala();
  else       relayMati();
  if (nyala) {
    waktuPompaNyala = millis();
    jumlahSiram++;
  } else {
    waktuPompaMati = millis();
  }
}

void bacaUdara() {
  float t = dht.readTemperature();
  float h = dht.readHumidity();
  if (isnan(t) || isnan(h)) {
    dhtOk = false;
    dhtGagalBeruntun++;
  } else {
    suhu = t;
    lembapUdara = h;
    dhtOk = true;
    dhtPernahOk = true;
    dhtGagalBeruntun = 0;
  }
}

void bacaTanah() {
  long total = 0;
  for (int i = 0; i < 10; i++) {
    total += analogRead(SOIL_PIN);
    delay(5);
  }
  adcTanah = total / 10;

  int batasBawah = min(NILAI_KERING, NILAI_BASAH) - TOLERANSI;
  int batasAtas  = max(NILAI_KERING, NILAI_BASAH) + TOLERANSI;

  if (adcTanah <= 50)            statusTanah = TANAH_NOL;
  else if (adcTanah >= 4050)     statusTanah = TANAH_MAKS;
  else if (adcTanah < batasBawah || adcTanah > batasAtas)
                                 statusTanah = TANAH_LUAR_KALIBRASI;
  else                           statusTanah = TANAH_OK;

  persenTanah = map(adcTanah, NILAI_KERING, NILAI_BASAH, 0, 100);
  persenTanah = constrain(persenTanah, 0, 100);
}

void kontrolPompa(unsigned long sekarang) {
  if (statusTanah == TANAH_NOL || statusTanah == TANAH_MAKS) {
    setPompa(false);   // sensor tidak tersambung, jangan siram
  } else if (!pompaNyala && persenTanah < BATAS_SIRAM &&
             sekarang - waktuPompaMati >= JEDA_MIN_MS) {
    setPompa(true);    // tanah kering, siram
  } else if (pompaNyala && persenTanah > BATAS_STOP) {
    setPompa(false);   // tanah sudah basah
  }
}

String formatWaktu(unsigned long ms) {
  unsigned long detik = ms / 1000;
  char buf[12];
  sprintf(buf, "%02lu:%02lu:%02lu", detik / 3600, (detik / 60) % 60, detik % 60);
  return String(buf);
}

String pesanErrorTanah() {
  switch (statusTanah) {
    case TANAH_NOL:
      return "ERROR - ADC 0: sensor tidak dapat listrik atau AOUT tidak di pin 34";
    case TANAH_MAKS:
      return "ERROR - ADC maksimal: kabel AOUT nyentuh 3V3 atau sensor rusak";
    case TANAH_LUAR_KALIBRASI:
      return "PERIKSA - di luar kalibrasi, sesuaikan NILAI_KERING/NILAI_BASAH";
    default:
      return "OK";
  }
}

void tampilLaporan(unsigned long sekarang) {
  Serial.println();
  Serial.println("=========== MONITORING TANAMAN ===========");
  Serial.print  ("Waktu jalan  : "); Serial.println(formatWaktu(sekarang));

  Serial.println("----------------- UDARA ------------------");
  if (dhtOk) {
    Serial.print("Suhu         : "); Serial.print(suhu, 1);        Serial.println(" C");
    Serial.print("Kelembapan   : "); Serial.print(lembapUdara, 0); Serial.println(" %");
    Serial.println("Status DHT11 : OK");
  } else {
    if (dhtPernahOk) {
      Serial.print("Suhu         : "); Serial.print(suhu, 1);        Serial.println(" C (data lama)");
      Serial.print("Kelembapan   : "); Serial.print(lembapUdara, 0); Serial.println(" % (data lama)");
    } else {
      Serial.println("Suhu         : --");
      Serial.println("Kelembapan   : --");
    }
    Serial.print("Status DHT11 : GAGAL dibaca ("); Serial.print(dhtGagalBeruntun);
    Serial.println("x berturut-turut) - cek kabel DATA ke pin 4");
  }

  Serial.println("----------------- TANAH ------------------");
  Serial.print("ADC mentah   : "); Serial.println(adcTanah);
  if (statusTanah == TANAH_OK || statusTanah == TANAH_LUAR_KALIBRASI) {
    Serial.print("Kelembapan   : "); Serial.print(persenTanah); Serial.println(" %");
  } else {
    Serial.println("Kelembapan   : --");
  }
  Serial.print("Status       : "); Serial.println(pesanErrorTanah());

  Serial.println("------------- GOOGLE SHEETS --------------");
  Serial.print("WiFi         : ");
  Serial.println(WiFi.status() == WL_CONNECTED ? "Tersambung" : "Putus");
  Serial.print("Terkirim     : "); Serial.print(jumlahTerkirim); Serial.println(" baris");
  Serial.println("==========================================");
}

// ================== WIFI & GOOGLE SHEETS ==================

void sambungWiFi() {
  Serial.print("Menyambung ke WiFi ");
  Serial.print(WIFI_SSID);
  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  unsigned long mulai = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - mulai < 15000) {
    delay(500);
    Serial.print(".");
  }
  if (WiFi.status() == WL_CONNECTED) {
    Serial.print("\nWiFi tersambung, IP: ");
    Serial.println(WiFi.localIP());
  } else {
    Serial.println("\nWiFi GAGAL - cek nama/password, pastikan 2.4 GHz");
  }
}

void kirimKeSheets() {
  if (WiFi.status() != WL_CONNECTED) {
    if (!MODE_PLOTTER) Serial.println(">> Sheets: WiFi belum tersambung, coba sambung ulang");
    WiFi.reconnect();
    return;
  }

  // Data sensor tidak valid tidak dikirim, supaya dataset tidak berisi nilai palsu
  if (!dhtOk || statusTanah == TANAH_NOL || statusTanah == TANAH_MAKS) {
    if (!MODE_PLOTTER) Serial.println(">> Sheets: data sensor belum valid, tidak dikirim");
    return;
  }

  String url = String(URL_SHEETS) +
               "?suhu="  + String(suhu, 1) +
               "&udara=" + String(lembapUdara, 0) +
               "&tanah=" + String(persenTanah);

  WiFiClientSecure client;
  client.setInsecure();   // lewati cek sertifikat (cukup untuk tugas/prototipe)

  HTTPClient http;
  http.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);  // Google me-redirect, wajib diikuti
  http.setTimeout(10000);

  if (!MODE_PLOTTER) Serial.print(">> Kirim ke Google Sheets... ");
  if (http.begin(client, url)) {
    int kode = http.GET();
    if (kode == 200) {
      jumlahTerkirim++;
      if (!MODE_PLOTTER) {
        Serial.print("BERHASIL (total ");
        Serial.print(jumlahTerkirim);
        Serial.println(" baris)");
      }
    } else if (!MODE_PLOTTER) {
      Serial.print("GAGAL, kode HTTP ");
      Serial.println(kode);
    }
    http.end();
  } else if (!MODE_PLOTTER) {
    Serial.println("GAGAL membuka koneksi");
  }
}

void tampilPlotter() {
  Serial.print("Suhu:");         Serial.print(suhu, 1);
  Serial.print(",UdaraPersen:"); Serial.print(lembapUdara, 0);
  Serial.print(",TanahPersen:");
  Serial.print((statusTanah == TANAH_OK || statusTanah == TANAH_LUAR_KALIBRASI) ? persenTanah : 0);
  Serial.print(",Pompa:");       Serial.println(pompaNyala ? 100 : 0);
}

// ============================================================
//                      SETUP & LOOP
// ============================================================

void setup() {
  relayMati();
  Serial.begin(115200);
  dht.begin();
  waktuPompaMati = millis() - JEDA_MIN_MS;  // boleh langsung siram saat baru menyala

  delay(1000);
  if (!MODE_PLOTTER) {
    Serial.println();
    Serial.println("Sistem penyiram otomatis dimulai...");
    Serial.println("Data diperbarui setiap 2 detik.");
  }

  sambungWiFi();
  waktuSheetsTerakhir = millis() - INTERVAL_SHEETS + 10000;  // kirim pertama 10 detik setelah nyala
}

void loop() {
  unsigned long sekarang = millis();

  // Pengaman: pompa tidak boleh nyala terlalu lama
  if (pompaNyala && sekarang - waktuPompaNyala >= MAKS_NYALA_MS) {
    setPompa(false);
    if (!MODE_PLOTTER) Serial.println(">> Pompa dimatikan: batas waktu nyala tercapai");
  }

  // Sensor tanah dibaca terus-menerus, pompa langsung bereaksi
  if (sekarang - waktuTanahTerakhir >= INTERVAL_TANAH) {
    waktuTanahTerakhir = sekarang;
    bacaTanah();
    kontrolPompa(sekarang);
  }

  // Kirim data ke Google Sheets tiap 30 detik
  if (sekarang - waktuSheetsTerakhir >= INTERVAL_SHEETS) {
    waktuSheetsTerakhir = sekarang;
    kirimKeSheets();
  }

  // DHT11 dan laporan tiap 2 detik
  if (sekarang - waktuBacaTerakhir < INTERVAL_BACA) return;
  waktuBacaTerakhir = sekarang;

  bacaUdara();

  if (MODE_PLOTTER) tampilPlotter();
  else              tampilLaporan(sekarang);
}
