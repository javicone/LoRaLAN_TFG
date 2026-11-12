/*********
 Proyecto IoT de registro de presencia y variables ambientales, sobre LoRaLAN.

 El nodo no usa WiFi: habla LoRaLAN con el gateway (Raspberry Pi), que es
 quien se comunica con el servidor por HTTP.

   core 0  tareaRadio()  LoRaLAN: JOIN, fichajes (PUSH), sensores (POLL), relés (CMD)
   core 1  loop()        NFC, sensores, LEDs, buzzer y pantalla
*********/

#include <SPI.h>
#include <Wire.h>
#include <Adafruit_PN532.h>
#include <Adafruit_Sensor.h>
#include <Adafruit_BME280.h>
#include <BH1750.h>
#include <ld2410.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <RadioLib.h>
#include <LoRaLAN.h>

#include "globals.h"
#include "config.h"

// Declaración adelantada
void initSensores();
bool setupDisplay();
void updateDisplay();
void displayMessage(String mensaje, int duracion_ms);
void displayFichajeExito(String uid);
void displayFichajeError();

// Handle de la tarea de radio (core 0)
TaskHandle_t HiloRadio;

// Última tarjeta enviada por radio: la misma no se reenvía hasta MISMA_TARJETA_MS
// (cada envío gasta tiempo de aire; el servidor ya ignora el fichaje repetido).
static uint8_t ultimoUid[7] = {0};
static uint8_t ultimoUidLen = 0;
static unsigned long ultimoUidMs = 0;

// LED de reposo: verde si los cuatro sensores y la asociación LoRa van bien;
// amarillo si no (incluye la radio que no arrancó: nunca llega a asociarse).
static void actualizarLedReposo() {
  const bool todoOk = sensores.bme280_ok && sensores.bh1750_ok &&
                      sensores.ld2410_ok && sensores.pn532_ok && loraAsociado();
  indicarEstado(todoOk ? NORMAL : ADVERTENCIA);
}

void setup() {
  pinMode(ledRojoPin, OUTPUT);
  pinMode(ledVerdePin, OUTPUT);
  pinMode(ledAmarilloPin, OUTPUT);
  pinMode(buzzerPin, OUTPUT);
  Serial.begin(115200);
  Serial.println("\nNodo LoRaLAN");

  secuenciaInicio();
  initSensores();   // incluye setupNFC(), que inicia el bus SPI compartido con la radio

  if (!setupLoRa()) {
    displayMessage("Error radio LoRa", 0);
    // Sin radio el nodo no puede fichar: se sigue para mostrar sensores en pantalla.
  } else {
    xTaskCreatePinnedToCore(
      tareaRadio,   /* Task function. */
      "HiloRadio",  /* name of task. */
      8192,         /* Stack size of task */
      NULL,         /* parameter of the task */
      1,            /* priority of the task */
      &HiloRadio,   /* Task handle */
      0);           /* pin task to core 0 */
  }
  delay(500);
  actualizarLedReposo();
}

// ============================================
// NFC: lectura y envío del fichaje
// ============================================
static void atenderNFC() {
  if (nfcReaderDisabled && millis() - nfcTimeLastCardRead > DELAY_BETWEEN_CARDS) {
    nfcReaderDisabled = false;
  }
  if (nfcReaderDisabled) return;

  NFCData nfc_data = leerNFC();   // polling con timeout corto
  if (!(nfc_data.valido && nfc_data.tarjetaDetectada)) return;

  nfcTimeLastCardRead = millis();
  nfcReaderDisabled = true;

  const bool repetida = nfc_data.uidLen == ultimoUidLen &&
                        memcmp(nfc_data.uidBytes, ultimoUid, ultimoUidLen) == 0 &&
                        millis() - ultimoUidMs < MISMA_TARJETA_MS;
  if (repetida) {
    Serial.println("ℹ️ Misma tarjeta hace poco: no se reenvía");
    beepCorto();
    return;
  }

  if (encolarFichaje(nfc_data.uidBytes, nfc_data.uidLen)) {
    memcpy(ultimoUid, nfc_data.uidBytes, nfc_data.uidLen);
    ultimoUidLen = nfc_data.uidLen;
    ultimoUidMs = millis();
    beepCorto();
    displayMessage("Enviando fichaje...", 0);
    Serial.println("📤 Fichaje encolado: " + nfc_data.uid);
  } else {
    secuenciaError();
    displayMessage("Nodo ocupado", 1500);
  }
}

// ============================================
// Resultado del fichaje (llega en el ACK)
// ============================================
static void atenderResultadoFichaje() {
  ResultadoFichaje resultado;
  String uid;
  if (!recogerResultadoFichaje(resultado, uid)) return;

  switch (resultado) {
    case ResultadoFichaje::OK:
      Serial.println("✅ Asistencia registrada: " + uid);
      secuenciaExito();
      displayFichajeExito(uid);
      break;
    case ResultadoFichaje::SIN_CLASE:
      Serial.println("⚠️ No hay clase programada en este aula ahora");
      ledVerde(false);
      ledAmarillo(true);
      displayMessage("Sin clase ahora", 1500);
      ledAmarillo(false);
      break;
    case ResultadoFichaje::DENEGADO:
      Serial.println("❌ Acceso denegado: la clase es de otro profesor");
      secuenciaError();
      displayMessage("Clase de otro profesor", 2000);
      break;
    case ResultadoFichaje::DESCONOCIDO:
      Serial.println("❌ Tarjeta o aula no registrada");
      secuenciaError();
      displayMessage("Tarjeta no registrada", 2000);
      break;
    case ResultadoFichaje::REINTENTAR:
      // NACK temporal: el nodo lo reenviará solo; se avisa una vez.
      Serial.println("⏳ Sin respuesta del servidor: se reenviará");
      ledVerde(false);
      ledAmarillo(false);
      parpadearLED(ledAmarilloPin, 3, 150);
      displayMessage("Guardado, se reenviara", 2000);
      ultimoUidLen = 0;   // permitir volver a pasar la tarjeta
      break;
    default:
      break;
  }
}

void loop() {
  atenderNFC();
  atenderResultadoFichaje();
  actualizarLedReposo();   // tras un resultado, el LED vuelve a su color de reposo

  // Sensores: se leen aquí (core 1) y la radio envía la última lectura en el POLL.
  static unsigned long ultimaLecturaSensores = 0;
  if (ultimaLecturaSensores == 0 || millis() - ultimaLecturaSensores > SENSOR_READ_INTERVAL) {
    ultimaLecturaSensores = millis();
    publicarLecturaSensores(leerBME280(), leerLuz(), leerRadar());
  }

  updateDisplay();
}
