# 🔌 TABLA COMPLETA DE PINES ESP32 - CONTROL ASISTENCIA

## 📊 TABLA MAESTRA DE PINES

```
┌─────────────────────────────────────────────────────────────────────────┐
│                    ASIGNACIÓN COMPLETA ESP32 WROOM-32                   │
└─────────────────────────────────────────────────────────────────────────┘
```

| GPIO | Pin Nombre | Función | Componente | Tipo | Notas |
|-----|-----------|---------|-----------|------|-------|
| 5   | D5 (GPIO5) | CS (Chip Select) | PN532 NFC | SPI Output 
| 4   | D4 (GPIO4) | DIO0 (Interrupt) | SX1276 LoRa | Input | Interrupción LoRa (el firmware lee RX_DONE por SPI) |
| 13  | D13 (GPIO13) | RST (Reset) | SX1276 LoRa | Output | Reset del módulo LoRa |
| 14  | D14 (GPIO14) | LED amarillo | Indicador | Output | |
| 15  | D15 (GPIO15) | CS (Chip Select) | SX1276 LoRa | SPI Output | Pin de arranque: en alto en reposo, correcto como CS |
| 16  | D16 (GPIO16) | RX Serial2 | LD2410 Radar | Serial Input | UART2 RX |
| 17  | D17 (GPIO17) | TX Serial2 | LD2410 Radar | Serial Output | UART2 TX |
| 18  | D18 (GPIO18) | CLK (SPI Clock) | PN532 NFC + SX1276 | SPI Clock | Reloj SPI compartido |
| 19  | D19 (GPIO19) | MISO (SPI) | PN532 NFC + SX1276 | SPI Input | Slave Output compartido |
| 21  | D21 (GPIO21) | SDA (I2C Data) | BME280, BH1750, OLED | I2C SDA | Bus compartido |
| 22  | D22 (GPIO22) | SCL (I2C Clock) | BME280, BH1750, OLED | I2C SCL | Bus compartido |
| 23  | D23 (GPIO23) | MOSI (SPI) | PN532 NFC + SX1276 | SPI Output | Master Output compartido |
| 25  | D25 (GPIO25) | LED rojo | Indicador | Output | |
| 26  | D26 (GPIO26) | Relé 1 | Control domótico | Output | Activo en LOW |
| 27  | D27 (GPIO27) | LED verde | Indicador | Output | |
| 32  | D32 (GPIO32) | Relé 2 | Control domótico | Output | Activo en LOW |
| 33  | D33 (GPIO33) | Buzzer | Indicador Sonoro | PWM Output | PWM variable |
| 34  | D34 (GPIO34) | IRQ (NFC) | PN532 NFC | Input Only | Interrupción (entrada) |

### Tabla de Pines Utilizados

```
┌────────────────────────────────────────────────────────────────┐
│  LADO IZQUIERDO                   LADO DERECHO                │
├────────────────────────────────────────────────────────────────┤
│ GND              ─── GND           3V3 ─── 3V3                 │
│ D23 (MOSI)       ─── SPI COMPARTIDO D1 (TX0) ─── Serial Monitor│
│ D22 (SCL I2C)    ─── BME/BH/OLED   D3 (RX0) ─── Serial Monitor│
│ D21 (SDA I2C)    ─── BME/BH/OLED   D4 (DIO0)──── SX1276 LoRa  │
│ D19 (MISO)       ─── SPI COMPARTIDO D16 (RX2)─── LD2410       │
│ D18 (CLK)        ─── SPI COMPARTIDO D17 (TX2)─── LD2410       │
│ D5 (CS PN532)    ─── PN532         D2 ─── [Libre]             │
│ D26 (Relé 1)     ─── Relé 1        D15 (CS)───── SX1276 LoRa  │
│ D33 (Buzzer)     ─── Buzzer        D13 (RST)──── SX1276 LoRa  │
│ D32 (Relé 2)     ─── Relé 2        D14 ─── LED amarillo       │
│ D34 (IRQ NFC)    ─── PN532 IRQ     D25 ─── LED rojo           │
│ GND              ─── GND           D27 ─── LED verde          │
│                                    GND ─── GND                │
│                                    3V3 ─── 3V3                │
└────────────────────────────────────────────────────────────────┘
```



## 🍓 GATEWAY: SX1276 ↔ RASPBERRY PI

Conexionado del gateway LoRaLAN (`controlAsistenciaMigrado/gatewayLoRaLAN`). Verificado en hardware con `test_radio`.
Numeración **BCM** (la que usa `lgpio`) y pin físico del conector de 40 pines.

| SX1276 | BCM | Pin físico | Función | Notas |
|--------|-----|-----------|---------|-------|
| NSS / CS | 8 (CE0) | 24 | Chip Select | Lo mueve el driver `spidev`, no RadioLib (`LORA_PIN_CS = RADIOLIB_NC`) |
| MOSI | 10 | 19 | SPI0 MOSI | |
| MISO | 9 | 21 | SPI0 MISO | |
| SCK | 11 | 23 | SPI0 SCLK | |
| DIO0 | 16 | 36 | IRQ TxDone / RxDone | Sin él, `transmit()` da timeout |
| RST | 13 | 33 | Reset | |
| VCC | — | 1 o 17 | 3.3V | ⚠️ NO 5V |
| GND | — | 6, 9, 20… | Masa | Común con la Pi |

```
   Raspberry Pi (conector 40 pines)          SX1276
   ─────────────────────────────────         ──────
   Pin 17  3V3 ──────────────────────────→  VCC
   Pin 19  BCM10 MOSI ───────────────────→  MOSI
   Pin 21  BCM9  MISO ←──────────────────   MISO
   Pin 23  BCM11 SCLK ───────────────────→  SCK
   Pin 24  BCM8  CE0 ────────────────────→  NSS
   Pin 25  GND ──────────────────────────   GND
   Pin 33  BCM13 ────────────────────────→  RESET
   Pin 36  BCM16 ←───────────────────────   DIO0
```

**Configuración de radio** (`src/config.h`, debe coincidir con los nodos ESP32):
869.525 MHz · SF7 · BW 125 kHz · CR 4/5 · sync word 0x12 · 14 dBm · preámbulo 8 · SPI 2 MHz en `/dev/spidev0.0`

**Notas:**
1. **SPI activado:** `sudo raspi-config` → Interface Options → SPI. Debe existir `/dev/spidev0.0`.
2. **"GPIO busy" en el pin 8:** el kernel reserva CE0 para SPI; por eso RadioLib recibe CS = `RADIOLIB_NC`.
3. **Diagnóstico rápido:** leer el registro de versión (0x42) debe devolver `0x12`. `0x00` = MISO/alimentación/GND; `0xFF` = NSS o chip en reset.
4. **Antena conectada antes de transmitir:** transmitir sin antena daña el amplificador.

