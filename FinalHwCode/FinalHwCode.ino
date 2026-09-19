#include <SPI.h>
#include <MFRC522.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <DHT.h>

// ---------------- RC522 (NodeMCU pins) ----------------
#define RFID_SS_PIN   D8
#define RFID_RST_PIN  D3
MFRC522 mfrc522(RFID_SS_PIN, RFID_RST_PIN);

// ---------------- OLED ----------------
#define SCREEN_WIDTH   128
#define SCREEN_HEIGHT  64
#define OLED_RESET     -1
#define SCREEN_ADDRESS 0x3C
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);

// ---------------- DHT22 ----------------
#define DHTPIN   D4
#define DHTTYPE  DHT22
DHT dht(DHTPIN, DHTTYPE);

// ---------------- LED alert ----------------
#define LED_PIN D0
#define TEMP_MAX 30.0
#define TEMP_MIN 15.0

// ---------------- Medicine data ----------------
struct Medicine {
  byte uid[4];
  String name;
  String expiry;
  int quantity;
  int minStock;
  int dispensedToday;   // NEW: counts removals, used for ML feature later
};

Medicine medicines[] = {
  {{0x97, 0x03, 0x02, 0x07}, "Paracetamol", "12/2026", 15, 5, 0},
  {{0x67, 0x72, 0x65, 0x06}, "Amoxicillin", "03/2027", 8,  5, 0},
  {{0x11, 0x22, 0x33, 0x44}, "Cetirizine",  "07/2026", 20, 5, 0}
};
const int NUM_MEDICINES = sizeof(medicines) / sizeof(medicines[0]);

unsigned long lastDHTRead = 0;
const unsigned long DHT_INTERVAL = 2000;
float currentTemp = 32, currentHumidity = 44;
bool dhtValid = false;

byte lastUID[10];
byte lastUIDSize = 0;

enum DisplayMode { HOME_SCREEN, MEDICINE_SCREEN, UNKNOWN_SCREEN };
DisplayMode currentDisplay = HOME_SCREEN;

void setup() {
  Serial.begin(115200);
  delay(500);
  Serial.println("SMC_READY");   // marker Python script waits for

  Wire.begin(D2, D1);
  if (!display.begin(SSD1306_SWITCHCAPVCC, SCREEN_ADDRESS)) {
    Serial.println("ERROR: OLED init failed");
  }
  display.clearDisplay(); display.setTextSize(1); display.setTextColor(SSD1306_WHITE);

  SPI.begin();
  mfrc522.PCD_Init();

  dht.begin();
  delay(2000);

  pinMode(LED_PIN, OUTPUT);
  digitalWrite(LED_PIN, LOW);

  readDHT();
  showHomeScreen();

  // Tell the dashboard the starting stock for every medicine so the
  // web page has something to show before any tag has been scanned.
  for (int i = 0; i < NUM_MEDICINES; i++) {
    Serial.print("INIT,");
    Serial.print(medicines[i].name); Serial.print(",");
    Serial.print(medicines[i].quantity); Serial.print(",");
    Serial.println(medicines[i].minStock);
  }
}

void loop() {
  if (millis() - lastDHTRead >= DHT_INTERVAL) {
    lastDHTRead = millis();
    readDHT();
    checkTempAlert();
    // Stream live temp/humidity continuously so the dashboard updates
    // even when nobody is scanning a tag (previously only sent
    // alongside a DATA line on a dispense event).
    if (dhtValid) {
      Serial.print("ENV,");
      Serial.print(currentTemp); Serial.print(",");
      Serial.println(currentHumidity);
    }
    if (currentDisplay == HOME_SCREEN) showHomeScreen();
  }

  if (!mfrc522.PICC_IsNewCardPresent()) return;
  if (!mfrc522.PICC_ReadCardSerial()) return;

  bool sameTag = isSameAsLastUID();
  bool found = false;

  for (int i = 0; i < NUM_MEDICINES; i++) {
    if (compareUID(mfrc522.uid.uidByte, medicines[i].uid, mfrc522.uid.size)) {
      found = true;
      if (!sameTag && medicines[i].quantity > 0) {
        medicines[i].quantity--;
        medicines[i].dispensedToday++;
        // CSV line the Python script parses: DATA,name,qty,minStock,temp,humidity
        Serial.print("DATA,");
        Serial.print(medicines[i].name); Serial.print(",");
        Serial.print(medicines[i].quantity); Serial.print(",");
        Serial.print(medicines[i].minStock); Serial.print(",");
        Serial.print(currentTemp); Serial.print(",");
        Serial.println(currentHumidity);
      }
      showMedicineScreen(medicines[i]);
      if (medicines[i].quantity <= medicines[i].minStock) {
        digitalWrite(LED_PIN, HIGH); delay(300); digitalWrite(LED_PIN, LOW);
      }
      break;
    }
  }
  if (!found) showUnknownScreen();

  saveCurrentUID();
  mfrc522.PICC_HaltA();
  mfrc522.PCD_StopCrypto1();
  delay(1500);
  currentDisplay = HOME_SCREEN;
  showHomeScreen();
  clearLastUID();
}

void readDHT() {
  float h = dht.readHumidity(), t = dht.readTemperature();
  if (isnan(h) || isnan(t)) { dhtValid = false; return; }
  currentTemp = t; currentHumidity = h; dhtValid = true;
}

void checkTempAlert() {
  if (!dhtValid) return;
  if (currentTemp > TEMP_MAX || currentTemp < TEMP_MIN) digitalWrite(LED_PIN, HIGH);
  else digitalWrite(LED_PIN, LOW);
}

void showHomeScreen() {
  currentDisplay = HOME_SCREEN;
  display.clearDisplay(); display.setCursor(0,0);
  display.println("Smart Medicine Cabinet");
  display.print("Temp: "); display.print(dhtValid?currentTemp:0,1); display.println(" C");
  display.print("Hum: ");  display.print(dhtValid?currentHumidity:0,1); display.println(" %");
  display.println("Scan a tag...");
  display.display();
}

void showMedicineScreen(Medicine &m) {
  currentDisplay = MEDICINE_SCREEN;
  display.clearDisplay(); display.setCursor(0,0);
  display.println(m.name);
  display.print("Expiry: "); display.println(m.expiry);
  display.print("Qty: "); display.println(m.quantity);
  display.println(m.quantity <= m.minStock ? "LOW STOCK!" : "Status: OK");
  display.display();
}

void showUnknownScreen() {
  currentDisplay = UNKNOWN_SCREEN;
  display.clearDisplay(); display.setCursor(0,0);
  display.println("Unknown Tag!");
  display.display();
}

bool compareUID(byte *a, byte *b, byte size) {
  if (size != 4) return false;
  for (byte i=0;i<4;i++) if (a[i]!=b[i]) return false;
  return true;
}
bool isSameAsLastUID() {
  if (lastUIDSize != mfrc522.uid.size) return false;
  for (byte i=0;i<lastUIDSize;i++) if (lastUID[i]!=mfrc522.uid.uidByte[i]) return false;
  return true;
}
void saveCurrentUID() {
  lastUIDSize = mfrc522.uid.size;
  for (byte i=0;i<lastUIDSize;i++) lastUID[i]=mfrc522.uid.uidByte[i];
}
void clearLastUID() { lastUIDSize = 0; }