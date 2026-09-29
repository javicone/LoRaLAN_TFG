#pragma once
// Configuracion del gateway LoRaLAN para Raspberry Pi.
//
// Los valores se pueden sobrescribir por variable de entorno al arrancar; ver
// README. Los que aparecen aqui son los valores por defecto.

#include <stdint.h>

// --- Conexionado SX1276 <-> Raspberry Pi (numeracion BCM) ---------------
// El HAL de RadioLib para Pi (PiHal) usa lgpio, que numera por BCM.
#define LORA_SPI_CHANNEL   0     // /dev/spidev0.x
#define LORA_SPI_SPEED     2000000
// CS va cableado a BCM 8 (pin fisico 24, CE0), pero lo mueve el driver spidev en
// cada transferencia: el kernel tiene el GPIO reservado y lgpio no puede tocarlo
// ("GPIO busy"). RadioLib hace un unico lgSpiXfer por acceso, asi que basta.
#define LORA_PIN_CS        RADIOLIB_NC
#define LORA_PIN_DIO0      16    // BCM 16 (pin fisico 36) - IRQ
#define LORA_PIN_RST       13    // BCM 13 (pin fisico 33)
// MOSI=BCM10(19), MISO=BCM9(21), SCK=BCM11(23) los gestiona el driver SPI.

// --- Parametros de radio ------------------------------------------------
#define LORA_FREQ_MHZ      869.525
#define LORA_BW_KHZ        125.0
#define LORA_SF            7
#define LORA_CR            5      // 4/5
#define LORA_SYNC_WORD     0x12
#define LORA_TX_POWER_DBM  14     // 25 mW; la sub-banda 869,4-869,65 admite hasta 500 mW ERP
#define LORA_PREAMBLE_LEN  8

// --- Ciclo de trabajo ---------------------------------------------------
// EU868 (ETSI EN 300 220): 1 % en la mayoria de sub-bandas, 10 % en
// 869,4-869,65 MHz, que es la que usa LoRaLAN (869,525 MHz).
#define DUTY_CYCLE_MAX     0.10
#define DUTY_WINDOW_MS     3600000UL

// --- Identidad LoRaLAN --------------------------------------------------
#define LORALAN_NET_ID     0x42
#define LORALAN_GATEWAY_ID 0x00

// --- Tiempos del protocolo ---------------------------------------------
//
// ATENCION: el valor por defecto de la biblioteca LoRaLAN es 2000 ms, que en
// SF7 consume el 2,06 % del ciclo de trabajo y por tanto NO es legal en una
// sub-banda del 1 % (p. ej. 868,1 MHz). Verificado en test_airtime.cpp:
//
//   ToA de un POLL (11 B, SF7)  = 41,22 ms
//   presupuesto del 1 % por hora = 36 000 ms  -> intervalo minimo 4,12 s
//   presupuesto del 10 % por hora = 360 000 ms -> intervalo minimo 0,41 s
//
// Por eso se usa la sub-banda del 10 %. Con 5000 ms el POLL consume el
// 0,82 %: cabe tambien en el 1 %. Si se sube el SF hay que espaciarlo mas
// (SF12: minimo 11,5 s al 10 %): el gateway lo comprueba al arrancar y avisa.
#define POLL_INTERVAL_MS      5000UL
#define TIME_SYNC_INTERVAL_MS 30000UL

// --- Servidor FastAPI ---------------------------------------------------
#define SERVER_HOST        "127.0.0.1"
#define SERVER_PORT        8000
#define HTTP_TIMEOUT_MS    3000

// Rutas del backend (contrato en src/bridge.h)
#define PATH_AUTH          "/auth/device"
#define PATH_FICHAJE       "/gateway/fichaje"
#define PATH_UPLINK        "/gateway/uplink"
#define PATH_COMMANDS      "/gateway/commands"
#define PATH_STATUS        "/gateway/status"

// --- Fichaje: ACK diferido ----------------------------------------------
// El resultado del fichaje viaja en el ACK del PUSH. El nodo espera ese ACK
// appAckTimeoutMs (1200 ms por defecto) antes de retransmitir, asi que el
// gateway responde SIEMPRE antes de FICHAJE_DEADLINE_MS: con el codigo del
// servidor si llego a tiempo, o con REINTENTAR (NACK temporal) si no.
#define FICHAJE_DEADLINE_MS     1000UL
// Timeout de la peticion HTTP del fichaje; por debajo del plazo para que la
// respuesta aun quepa en el ACK.
#define FICHAJE_HTTP_TIMEOUT_MS 800

// Cada cuanto pregunta el gateway por comandos pendientes.
// Sin WebSocket, el gateway decide cuando puede transmitir: este intervalo es
// el que acota el trafico descendente y con el el consumo de ciclo de trabajo.
#define COMMAND_POLL_MS    5000UL

// Cada cuanto reporta su estado (nodos vivos, ciclo de trabajo consumido).
#define STATUS_REPORT_MS   30000UL

// Cola de eventos entre el hilo de radio y el de red. Si se llena, se
// descartan los mas antiguos: es preferible perder telemetria vieja que
// bloquear la radio.
#define UPLINK_QUEUE_MAX   256
