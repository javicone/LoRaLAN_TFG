#ifndef CONFIG_H
#define CONFIG_H

// ============================================
// LoRaLAN: identidad y radio
// ============================================
// Debe coincidir con el gateway (controlAsistenciaMigrado/gatewayLoRaLAN/src/config.h
// y la APP_KEY de su main.cpp).

// Id del nodo en la red LoRaLAN (1..254), distinto en cada placa. En el
// servidor este nodo es "node-NNN" (p. ej. 2 -> node-002).
#define LORALAN_NODE_ID     2
#define LORALAN_NET_ID      0x42
#define LORALAN_GATEWAY_ID  0x00
#define LORALAN_NODE_TYPE   0x01   // nodo de aula

#define LORA_FREQ_MHZ       869.525
#define LORA_BW_KHZ         125.0
#define LORA_SF             7
#define LORA_CR             5      // 4/5
#define LORA_SYNC_WORD      0x12
#define LORA_TX_POWER_DBM   14
#define LORA_PREAMBLE_LEN   8

// Clave de aplicacion (JOIN y derivacion de la clave de sesion).
// ponytail: compilada en el firmware; cargarla de NVS si cada aula necesita la suya.
static const uint8_t LORALAN_APP_KEY[16] = {
    0x42, 0x19, 0xA7, 0x5C, 0xD3, 0x88, 0x11, 0xE4,
    0x2F, 0x90, 0x6A, 0xC1, 0x7B, 0x54, 0x0D, 0xBE
};

// ============================================
// CONSTANTES DE TIEMPO
// ============================================
#define SENSOR_READ_INTERVAL    10000UL  // lectura de sensores para la respuesta al POLL
#define DELAY_BETWEEN_CARDS      3000UL  // pausa del lector NFC tras una lectura
#define MISMA_TARJETA_MS        60000UL  // la misma tarjeta no se reenvia por radio antes de esto
#define FICHAJE_REINTENTO_MS    30000UL  // espera tras un NACK REINTENTAR
#define FICHAJES_EN_COLA_MAX         8   // fichajes pendientes de reenviar (en RAM)

// ============================================
// PINES
// ============================================
#define RELAY_PIN_1         26
#define RELAY_PIN_2         32
#define BUZZER_PIN          33

#define LED_ROJO_PIN        25
#define LED_VERDE_PIN       27
#define LED_AMARILLO_PIN    14

#define OLED_SDA_PIN        21
#define OLED_SCL_PIN        22
#define OLED_RESET_PIN      -1  // -1 si no tiene pin RESET (compartido con ESP32)
// Dirección I2C de la pantalla (típicamente 0x3C o 0x3D)
#define OLED_I2C_ADDRESS    0x3C

// Constantes de sensores
#define SEALEVELPRESSURE_HPA (1013.25)
#define RADAR_RX_PIN 16
#define RADAR_TX_PIN 17

// Bus SPI compartido por el PN532 y el SX1276 (cada uno con su CS)
#define PN532_SCK           (18)
#define PN532_MOSI          (23)
#define PN532_MISO          (19)
#define PN532_SS            (5)
#define PN532_IRQ           (34)

// SX1276. Movido a 4/13/15: el diagrama antiguo lo ponia en 14/25/27, que
// son los LEDs. GPIO15 es pin de arranque; como CS esta en alto en reposo.
#define LORA_CS             15
#define LORA_DIO0           4
#define LORA_RST            13

// ============================================
// COMPATIBILIDAD (para código antiguo que use const int)
// ============================================
const int relayPin1 = RELAY_PIN_1;
const int relayPin2 = RELAY_PIN_2;
const int buzzerPin = BUZZER_PIN;
const int ledRojoPin = LED_ROJO_PIN;
const int ledVerdePin = LED_VERDE_PIN;
const int ledAmarilloPin = LED_AMARILLO_PIN;

#endif // CONFIG_H
