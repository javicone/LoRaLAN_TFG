// Test de radio SX1276 en Raspberry Pi, sin LoRaLAN: solo RadioLib.
//
//   sudo ./test_radio        -> transmite "hola pi #N" cada 10 s
//   sudo ./test_radio rx     -> escucha e imprime lo recibido con RSSI/SNR
//
// Usa los mismos pines y parametros de radio que el gateway (src/config.h),
// asi que si esto funciona, la capa fisica del gateway tambien.

#include <RadioLib.h>
#include "hal/RPi/PiHal.h"
#include "config.h"

#include <cstdio>
#include <cstring>
#include <unistd.h>

// 10 s entre envios: ~50 ms de ToA en SF7 -> 0,5 % de ciclo de trabajo (limite de la sub-banda: 10 %).
static const unsigned TX_INTERVAL_S = 10;

int main(int argc, char** argv) {
    bool rxMode = argc > 1 && strcmp(argv[1], "rx") == 0;

    PiHal* hal = new PiHal(LORA_SPI_CHANNEL, LORA_SPI_SPEED);
    SX1276 radio = new Module(hal, LORA_PIN_CS, LORA_PIN_DIO0, LORA_PIN_RST, RADIOLIB_NC);

    printf("SX1276 %.3f MHz SF%d BW%.0f CR4/%d sync 0x%02X... ",
           LORA_FREQ_MHZ, LORA_SF, LORA_BW_KHZ, LORA_CR, LORA_SYNC_WORD);
    fflush(stdout);
    int state = radio.begin(LORA_FREQ_MHZ, LORA_BW_KHZ, LORA_SF, LORA_CR,
                            LORA_SYNC_WORD, LORA_TX_POWER_DBM, LORA_PREAMBLE_LEN);
    if (state != RADIOLIB_ERR_NONE) {
        printf("ERROR %d\n", state);
        if (state == RADIOLIB_ERR_CHIP_NOT_FOUND)
            printf("El chip no responde por SPI: revisa MOSI/MISO/SCK/CS, 3V3, GND y RST,\n"
                   "y que SPI este activado (ls /dev/spidev0.0).\n");
        return 1;
    }
    printf("OK\n");

    if (rxMode) {
        printf("Escuchando (Ctrl+C para salir)...\n");
        uint8_t buf[256];
        for (;;) {
            state = radio.receive(buf, 0);
            if (state == RADIOLIB_ERR_RX_TIMEOUT) continue;
            if (state != RADIOLIB_ERR_NONE) { printf("RX error %d\n", state); continue; }
            size_t len = radio.getPacketLength();
            printf("RX %zu B: ", len);
            for (size_t i = 0; i < len; i++) putchar(buf[i] >= 0x20 && buf[i] < 0x7F ? buf[i] : '.');
            printf("  | RSSI %.1f dBm  SNR %.1f dB\n", radio.getRSSI(), radio.getSNR());
        }
    }

    for (unsigned n = 0;; n++) {
        char msg[32];
        snprintf(msg, sizeof(msg), "hola pi #%u", n);
        state = radio.transmit(msg);
        if (state == RADIOLIB_ERR_NONE)
            printf("TX OK: %s\n", msg);
        else if (state == RADIOLIB_ERR_TX_TIMEOUT)
            printf("TX timeout: el chip no avisa de fin de envio, revisa DIO0 (BCM %d)\n", LORA_PIN_DIO0);
        else
            printf("TX error %d\n", state);
        sleep(TX_INTERVAL_S);
    }
}
