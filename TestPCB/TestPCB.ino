// TestPCB: prueba uno a uno los componentes de la PCB del nodo.
// Monitor serie a 115200 baudios con "Nueva línea". Al arrancar pasa
// todas las pruebas y saca un resumen; después se repite cualquiera
// desde el menú. Las pruebas visuales o sonoras piden confirmar con s/n.
//
// Bibliotecas: las mismas que el firmware del nodo (RadioLib, Adafruit
// PN532/BME280/SSD1306/GFX, BH1750, ld2410). No usa LoRaLAN.

#include <SPI.h>
#include <Wire.h>
#include <RadioLib.h>
#include <Adafruit_PN532.h>
#include <Adafruit_BME280.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <BH1750.h>
#include <ld2410.h>

// Pines: copia de Device_ESP32_LoRaLAN/config.h (mantener iguales)
#define RELAY_PIN_1   26
#define RELAY_PIN_2   32
#define BUZZER_PIN    33
#define LED_ROJO      25
#define LED_VERDE     27
#define LED_AMARILLO  14
#define I2C_SDA       21
#define I2C_SCL       22
#define RADAR_RX      16
#define RADAR_TX      17
#define SPI_SCK       18
#define SPI_MISO      19
#define SPI_MOSI      23
#define PN532_SS       5
#define PN532_IRQ     35
#define LORA_CS       15
#define LORA_DIO0      4
#define LORA_RST      13

#define ADDR_OLED   0x3C
#define ADDR_BME    0x76
#define ADDR_BH1750 0x23

// Arriba del todo: el IDE genera prototipos que usan este tipo
struct Prueba { char tecla; const char* nombre; bool (*fn)(); int res; };  // res: -1 sin pasar

SX1276 radio = new Module(LORA_CS, LORA_DIO0, LORA_RST, RADIOLIB_NC);
Adafruit_PN532 nfc(PN532_SS);
Adafruit_BME280 bme;
Adafruit_SSD1306 oled(128, 64, &Wire, -1);
BH1750 luz;
ld2410 radar;

// ---------------------------------------------------------------
// Utilidades
// ---------------------------------------------------------------

// Espera 's' o 'n' del monitor serie. Sin respuesta en 20 s cuenta como fallo.
bool confirma(const char* pregunta) {
  while (Serial.available()) Serial.read();
  Serial.printf("   ¿%s? (s/n): ", pregunta);
  uint32_t t0 = millis();
  while (millis() - t0 < 20000) {
    if (Serial.available()) {
      char c = tolower(Serial.read());
      if (c == 's' || c == 'n') { Serial.println(c); return c == 's'; }
    }
    delay(10);
  }
  Serial.println("(sin respuesta)");
  return false;
}

bool hayI2C(uint8_t addr) {
  Wire.beginTransmission(addr);
  return Wire.endTransmission() == 0;
}

// ---------------------------------------------------------------
// Pruebas
// ---------------------------------------------------------------

bool testI2C() {
  Serial.println("   Escaneando bus I2C (SDA 21, SCL 22)...");
  int n = 0;
  for (uint8_t a = 1; a < 127; ++a) {
    if (hayI2C(a)) { Serial.printf("   - 0x%02X\n", a); ++n; }
  }
  bool ok = hayI2C(ADDR_OLED) && hayI2C(ADDR_BME) && hayI2C(ADDR_BH1750);
  if (!ok) Serial.printf("   Esperados 0x%02X (OLED), 0x%02X (BME280), 0x%02X (BH1750)\n",
                         ADDR_OLED, ADDR_BME, ADDR_BH1750);
  if (n == 0) Serial.println("   Ningún dispositivo: revisa SDA/SCL, pull-ups y 3V3");
  return ok;
}

bool testBME() {
  if (!bme.begin(ADDR_BME)) { Serial.println("   No responde en 0x76 (¿0x77? SDO a VCC)"); return false; }
  float t = bme.readTemperature(), h = bme.readHumidity(), p = bme.readPressure() / 100.0F;
  Serial.printf("   T=%.1f °C  H=%.1f %%  P=%.1f hPa\n", t, h, p);
  // Rango de interior razonable; un BMP280 (sin humedad) da H=0 o NaN
  return t > 0 && t < 50 && h > 5 && h < 100 && p > 850 && p < 1100;
}

bool testBH1750() {
  if (!luz.begin(BH1750::ONE_TIME_HIGH_RES_MODE, ADDR_BH1750, &Wire)) {
    Serial.println("   No responde en 0x23 (ADDR a GND)"); return false;
  }
  delay(200);
  float a = luz.readLightLevel();
  Serial.printf("   Luz = %.1f lx. Tapa el sensor...\n", a);
  delay(2500);
  luz.configure(BH1750::ONE_TIME_HIGH_RES_MODE);
  delay(200);
  float b = luz.readLightLevel();
  Serial.printf("   Luz tapado = %.1f lx\n", b);
  // ponytail: umbral fijo; si en la sala hay poca luz, apuntar con el móvil
  return a >= 0 && b >= 0 && b < a * 0.7f;
}

bool testOLED() {
  if (!oled.begin(SSD1306_SWITCHCAPVCC, ADDR_OLED)) { Serial.println("   No responde en 0x3C"); return false; }
  oled.clearDisplay();
  oled.drawRect(0, 0, 128, 64, SSD1306_WHITE);
  oled.setTextSize(2);
  oled.setTextColor(SSD1306_WHITE);
  oled.setCursor(10, 12);
  oled.print("TEST PCB");
  oled.setTextSize(1);
  oled.setCursor(10, 40);
  oled.print("Marco + texto OK?");
  oled.display();
  return confirma("Se ve el marco y el texto en la pantalla");
}

bool testPN532() {
  nfc.begin();
  uint32_t v = nfc.getFirmwareVersion();
  if (!v) { Serial.println("   Sin respuesta por SPI (CS 5). Revisa interruptores en modo SPI"); return false; }
  Serial.printf("   PN5%02X firmware %d.%d\n", (v >> 24) & 0xFF, (v >> 16) & 0xFF, (v >> 8) & 0xFF);
  nfc.SAMConfig();
  Serial.printf("   IRQ (GPIO34) en reposo = %d (debe ser 1)\n", digitalRead(PN532_IRQ));
  Serial.println("   Acerca una tarjeta (10 s)...");
  uint8_t uid[7], len;
  uint32_t t0 = millis();
  while (millis() - t0 < 10000) {
    if (nfc.readPassiveTargetID(PN532_MIFARE_ISO14443A, uid, &len, 200)) {
      Serial.print("   UID:");
      for (uint8_t i = 0; i < len; ++i) Serial.printf(" %02X", uid[i]);
      Serial.println();
      return true;
    }
  }
  Serial.println("   No se leyó ninguna tarjeta");
  return false;
}

bool testLoRa() {
  // begin() reinicia por RST y lee el registro de versión (0x12) por SPI
  int st = radio.begin(869.525, 125.0, 7, 5, 0x12, 14, 8);
  if (st != RADIOLIB_ERR_NONE) {
    Serial.printf("   begin() = %d (-2 = chip no encontrado: SPI, CS 15 o RST 13)\n", st);
    return false;
  }
  Serial.println("   SX1276 detectado por SPI");
  // transmit() espera TX_DONE en DIO0: si DIO0 no está conectado, da timeout.
  // El gateway descarta este paquete (no es LoRaLAN válido).
  st = radio.transmit("TESTPCB");
  if (st != RADIOLIB_ERR_NONE) {
    Serial.printf("   transmit() = %d (-5 = timeout: revisa DIO0 en GPIO4)\n", st);
    return false;
  }
  radio.startReceive();
  delay(50);
  float rssi = radio.getRSSI(false);
  Serial.printf("   TX OK. RSSI de ruido = %.0f dBm (con antena, entre -125 y -90)\n", rssi);
  radio.sleep();
  return rssi < -60;  // ponytail: un valor alto indica lectura absurda o interferencia muy cercana
}

bool testRadar() {
  Serial2.begin(256000, SERIAL_8N1, RADAR_RX, RADAR_TX);
  if (!radar.begin(Serial2, false)) {
    Serial.println("   No responde por UART2 (cruzar RX/TX: GPIO16 ← TX radar, GPIO17 → RX radar)");
    return false;
  }
  Serial.printf("   LD2410 firmware v%d.%d.%lX\n", radar.firmware_major_version,
                radar.firmware_minor_version, (unsigned long)radar.firmware_bugfix_version);
  Serial.println("   Muévete delante del radar (5 s)...");
  uint32_t t0 = millis();
  bool visto = false;
  while (millis() - t0 < 5000) {
    radar.read();
    if (radar.movingTargetDetected() && !visto) {
      Serial.printf("   Movimiento a %d cm\n", radar.movingTargetDistance());
      visto = true;
    }
  }
  if (!visto) Serial.println("   No detectó movimiento");
  return visto;
}

bool testLED(int pin, const char* nombre) {
  digitalWrite(pin, HIGH);
  char q[48];
  snprintf(q, sizeof q, "Está encendido el LED %s", nombre);
  bool ok = confirma(q);
  digitalWrite(pin, LOW);
  return ok;
}
bool testLedRojo()     { return testLED(LED_ROJO, "rojo"); }
bool testLedVerde()    { return testLED(LED_VERDE, "verde"); }
bool testLedAmarillo() { return testLED(LED_AMARILLO, "amarillo"); }

bool testBuzzer() {
  for (int f : {800, 1000, 2000}) { tone(BUZZER_PIN, f, 200); delay(300); }
  return confirma("Han sonado tres pitidos");
}

bool testRele(int pin, const char* nombre) {
  for (int i = 0; i < 3; ++i) {  // activo en LOW
    digitalWrite(pin, LOW);  delay(400);
    digitalWrite(pin, HIGH); delay(400);
  }
  char q[48];
  snprintf(q, sizeof q, "Ha conmutado tres veces el %s", nombre);
  return confirma(q);
}
bool testRele1() { return testRele(RELAY_PIN_1, "relé 1"); }
bool testRele2() { return testRele(RELAY_PIN_2, "relé 2"); }

// ---------------------------------------------------------------
// Menú y resumen
// ---------------------------------------------------------------

Prueba pruebas[] = {
  {'1', "Bus I2C",       testI2C,         -1},
  {'2', "BME280",        testBME,         -1},
  {'3', "BH1750",        testBH1750,      -1},
  {'4', "OLED SSD1306",  testOLED,        -1},
  {'5', "PN532 NFC",     testPN532,       -1},
  {'6', "SX1276 LoRa",   testLoRa,        -1},
  {'7', "LD2410 radar",  testRadar,       -1},
  {'8', "LED rojo",      testLedRojo,     -1},
  {'9', "LED verde",     testLedVerde,    -1},
  {'a', "LED amarillo",  testLedAmarillo, -1},
  {'b', "Buzzer",        testBuzzer,      -1},
  {'c', "Relé 1",        testRele1,       -1},
  {'d', "Relé 2",        testRele2,       -1},
};
const int N = sizeof pruebas / sizeof pruebas[0];

void ejecuta(Prueba& p) {
  Serial.printf("\n[%c] %s\n", p.tecla, p.nombre);
  p.res = p.fn() ? 1 : 0;
  Serial.printf("   => %s\n", p.res ? "OK" : "FALLO");
}

void resumen() {
  int ok = 0;
  Serial.println("\n========== RESUMEN ==========");
  for (auto& p : pruebas) {
    Serial.printf(" [%c] %-14s %s\n", p.tecla, p.nombre,
                  p.res < 0 ? "-" : p.res ? "OK" : "FALLO");
    ok += p.res == 1;
  }
  Serial.printf(" %d/%d correctas\n", ok, N);
  Serial.println("=============================");
  Serial.println("Tecla = repetir una prueba, 't' = todas, 'r' = resumen");
}

void setup() {
  Serial.begin(115200);
  delay(1500);

  // Dos esclavos en el mismo SPI: ambos CS en alto antes de tocar el bus
  pinMode(PN532_SS, OUTPUT); digitalWrite(PN532_SS, HIGH);
  pinMode(LORA_CS, OUTPUT);  digitalWrite(LORA_CS, HIGH);
  pinMode(PN532_IRQ, INPUT);

  for (int pin : {LED_ROJO, LED_VERDE, LED_AMARILLO, BUZZER_PIN}) { pinMode(pin, OUTPUT); digitalWrite(pin, LOW); }
  for (int pin : {RELAY_PIN_1, RELAY_PIN_2}) { pinMode(pin, OUTPUT); digitalWrite(pin, HIGH); }  // apagados

  SPI.begin(SPI_SCK, SPI_MISO, SPI_MOSI);
  Wire.begin(I2C_SDA, I2C_SCL);

  Serial.println("\n===== TEST PCB NODO LoRaLAN =====");
  for (auto& p : pruebas) ejecuta(p);
  resumen();
}

void loop() {
  if (!Serial.available()) return;
  char c = tolower(Serial.read());
  if (c == 't') { for (auto& p : pruebas) ejecuta(p); resumen(); return; }
  if (c == 'r') { resumen(); return; }
  for (auto& p : pruebas) if (p.tecla == c) { ejecuta(p); resumen(); }
}
