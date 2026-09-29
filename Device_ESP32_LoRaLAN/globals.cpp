#include "globals.h"
#include "config.h"

// ============================================
// DEFINICIONES REALES (aquí existe la variable única)
// ============================================
// Sin WiFi, servidor, certificados ni NTP: el nodo solo habla LoRaLAN con el
// gateway, y la identidad del nodo es LORALAN_NODE_ID (config.h).

// ============================================
// OBJETOS DE SENSORES
// ============================================
Adafruit_BME280 bme;
BH1750 lightMeter;
ld2410 radar;
Adafruit_PN532 nfc(PN532_SS);  // Solo necesita CS, SPI ya inicializado
Adafruit_SSD1306 display(128, 64, &Wire, OLED_RESET_PIN);

// ============================================
// VARIABLES DE ESTADO
// ============================================
SensorStatus sensores;

// Lector NFC
bool nfcReaderDisabled = false;
unsigned long nfcTimeLastCardRead = 0;

// Estado de los relés (true = encendido)
bool relay1Estado = false;
bool relay2Estado = false;
