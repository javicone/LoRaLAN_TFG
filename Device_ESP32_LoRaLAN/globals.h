#ifndef GLOBALS_H
#define GLOBALS_H

#include <Arduino.h>
#include <Adafruit_BME280.h>
#include <BH1750.h>
#include <Adafruit_PN532.h>
#include <ld2410.h>
#include <Adafruit_SSD1306.h>

// ============================================
// ESTRUCTURAS DE DATOS
// ============================================

// Estructura para estado de sensores
struct SensorStatus {
  bool bme280_ok = false;
  bool bh1750_ok = false;
  bool ld2410_ok = false;
  bool pn532_ok = false;
  bool display_ok = false;

  uint8_t bme280_errores = 0;
  uint8_t bh1750_errores = 0;
  uint8_t ld2410_errores = 0;
  uint8_t pn532_errores = 0;
  uint8_t display_errores = 0;
};

struct BME280Data {
  bool valido = false;
  float temperatura = 0.0;
  float humedad = 0.0;
  float presion = 0.0;
};

struct LuzData {
  bool valido = false;
  float lux = 0.0;
};

struct NFCData {
  bool valido = false;
  bool tarjetaDetectada = false;
  String uid = "";
  uint8_t uidBytes[7] = {0};   // el UID en binario, tal como viaja por LoRaLAN
  uint8_t uidLen = 0;
};

struct RadarData {
  bool valido = false;
  int distanciaEstatico = 0;
  int distanciaMovil = 0;
  bool presenciaDetectada = false;
};

// ============================================
// OBJETOS DE SENSORES (definidos en globals.cpp)
// ============================================
extern Adafruit_BME280 bme;
extern BH1750 lightMeter;
extern ld2410 radar;
extern Adafruit_PN532 nfc;
extern Adafruit_SSD1306 display;

// ============================================
// VARIABLES DE ESTADO (definidas en globals.cpp)
// ============================================
extern SensorStatus sensores;

// Lector NFC
extern bool nfcReaderDisabled;
extern unsigned long nfcTimeLastCardRead;

// ============================================
// ESTADOS DE LEDs
// ============================================
enum EstadoLED {
  NORMAL,
  ADVERTENCIA,
  ERROR,
  CRITICO
};

// ============================================
// FUNCIONES DE INDICADORES (implementadas en indicators.ino)
// ============================================
void setupIndicators();
void testIndicadores();
void indicarEstado(EstadoLED estado);
void parpadearLED(int pin, int veces, int duracion_ms);

// Control de los relés (activos en LOW)
void activarRele1(bool estado);
void activarRele2(bool estado);
bool estadoRele1();
bool estadoRele2();
extern bool relay1Estado;
extern bool relay2Estado;

// Control individual de LEDs
void ledVerde(bool estado);
void ledAmarillo(bool estado);
void ledRojo(bool estado);

// Funciones de buzzer
void beep(int frecuencia, int duracion);
void beepCorto();
void beepLargo();
void beepError();

// Secuencias especiales
void secuenciaInicio();
void secuenciaError();
void secuenciaExito();
void apagarTodosLosIndicadores();

// ============================================
// SENSORES (implementados en *_module.ino)
// ============================================
BME280Data leerBME280();
LuzData leerLuz();
RadarData leerRadar();
NFCData leerNFC();

// ============================================
// LoRaLAN (implementado en lora_node.ino)
// ============================================
// Resultado de un fichaje tal como lo muestra el nodo.
enum class ResultadoFichaje : uint8_t { NINGUNO, OK, SIN_CLASE, DENEGADO, DESCONOCIDO, REINTENTAR };

bool setupLoRa();
void tareaRadio(void* pvParameters);            // bucle de radio (core 0)
bool encolarFichaje(const uint8_t* uid, uint8_t uidLen);
bool recogerResultadoFichaje(ResultadoFichaje& resultado, String& uid);
void publicarLecturaSensores(const BME280Data& bme, const LuzData& luz, const RadarData& radar);
bool loraAsociado();
uint8_t loraDireccion();   // direccion LoRaLAN actual (la pedida hasta asociarse)

#endif
