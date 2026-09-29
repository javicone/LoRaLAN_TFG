# 🔌 TABLA COMPLETA DE PINES ESP32 - CONTROL ASISTENCIA

## 📊 TABLA MAESTRA DE PINES

```
┌─────────────────────────────────────────────────────────────────────────┐
│                    ASIGNACIÓN COMPLETA ESP32 WROOM-32                   │
└─────────────────────────────────────────────────────────────────────────┘
```

| GPIO | Pin Nombre | Función | Componente | Tipo | Notas |
|-----|-----------|---------|-----------|------|-------|
| 5   | D5 (GPIO5) | CS (Chip Select) | PN532 NFC | SPI Output | Pull-up requerido |
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

---

## 🗺️ MAPA VISUAL DE PINES ESP32

```
                    ┌─────────────────────┐
                    │    ESP32 WROOM-32   │
                    │                     │
         D1 (TX0)   ├─────────────────────┤   ← Serial Monitor
         D3 (RX0)   │  3V3 [USB 5V] GND   │
                    │                     │
       GND          │   36  39  34  35    │
       D23 (MOSI)   │   4   2   15  13    │
       D22 (SCL)    │   14  27  26  25    │
       D21 (SDA)    │   33  32  5   18    │
       D19 (MISO)   │   17  16  4   0     │
       D18 (CLK)    │                     │
                    │  GND GND 3V3 3V3    │
                    └─────────────────────┘

LEYENDA DE COLORES:
🟢 GPIO Entrada/Salida Digital
🔴 GPIO PWM
🟣 GPIO Especial (UART, SPI, I2C)
⚫ GND (Tierra)
🟡 Poder (3.3V)
```

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

---

## 🔗 DIAGRAMAS DE CONEXIÓN POR PROTOCOLO

### 1️⃣ BUS I2C (GPIO 21 y 22)
```
                    ESP32
                     
          GPIO 21 (SDA)
              ║
    ┌─────────╫─────────┬──────────┬──────────┐
    ║         ║         ║          ║          ║
  10kΩ      10kΩ      3.3V       GND        
    ║         ║         ║          ║
   SDA       SCL    Común Común   
    │         │      ├─┼──────────┼──────────┤
    │         │      │ │          │          │
┌───┴──────┬──┴──┬   │ │          │          │
│  BME280   │    │   │ │          │          │
│ 0x76      │    │   │ │          │          │
│ Temp/Hum  │    │   │ │          │          │
└───┬──────┴──┬──┘   │ │          │          │
    │ SDA  SCL│      │ │          │          │
    │    │    │      │ │          │          │
    │    │    ├──────┘ │          │          │
    │    │    │        │          │          │
    │ ┌──┴────┴───┐    │          │          │
    │ │ BH1750    │    │          │          │
    │ │ Luz       │    │          │          │
    │ └──┬────┬───┘    │          │          │
    │    │    │        │          │          │
    │    └─┬──┴────────┘          │          │
    │      │                      │          │
    │ ┌────┴──────┐               │          │
    │ │   OLED    │               │          │
    │ │  0x3C     │               │          │
    │ │ 128x64px  │               │          │
    │ │           │               │          │
    │ └────┬──┬───┘               │          │
    │      │  │                   │          │
    ├──────┘  │                   │          │
    │         │                   │          │
   3.3V     GND                  GND         │
                                            │
                                           GND
```

### 2️⃣ BUS SPI (GPIO 18, 19, 23 - COMPARTIDO entre PN532 y SX1276)
```
              ESP32
               
         GPIO 18 (CLK)────────┬────────────┐
         GPIO 23 (MOSI)───┬───┤            │
         GPIO 19 (MISO)─┬─┤   │            │
                        │ │   │            │
        ┌───────────────┘ │   │            │
        │              ┌──┴─┬─┴────┐       │
        │              │    │      │       │
        │            PN532  │      │   SX1276
        │            NFC    │      │   LoRa
        │                   │      │
        │  CLK: GPIO18 ─────┤      │───────→ CLK: GPIO18
        │  DIN: GPIO23 ─────┤      │───────→ DIN: GPIO23
        │  OUT: GPIO19 ←─────┤      │←────── OUT: GPIO19
        │  CE: GPIO 5  ──→   │      │
        │  IRQ: GPIO34 ←─    │      │───────→ CS: GPIO 15
        │                    │      │───────→ RST: GPIO 13
        │  VCC: 3.3V ⚠️      │      │───────→ DIO0: GPIO 4
        │  GND: GND  ────────┴──────┴────────→ VCC: 3.3V ⚠️
        │                              GND: GND
        └──────────────────────────────────────────────────────

⚠️  IMPORTANTE:
    - PN532 y SX1276 comparten CLK, MOSI, MISO (GPIO 18,23,19)
    - Cada uno tiene su propio CS (Chip Select) exclusivo
    - PN532: CS en GPIO 5
    - SX1276: CS en GPIO 15, RST en GPIO 13, DIO0 en GPIO 4
    - Ambos requieren EXACTAMENTE 3.3V (NO 5V)
```

### 3️⃣ UART Serial2 (GPIO 16, 17)
```
        ESP32               LD2410 Radar
        
    GPIO 16 (RX2) ←─────── TX
    GPIO 17 (TX2) ───────→ RX
    
    Velocidad: 256000 baud
    Detección: 0-600 cm estático
              0-600 cm dinámico
```

### 4️⃣ SALIDAS DIGITALES Y ACTUADORES
```
    GPIO 33 ──────────→ Buzzer (PWM)
    GPIO 26 ──────────→ Relé 1 (activo LOW)
    GPIO 32 ──────────→ Relé 2 (activo LOW)
    GPIO 25 ──────────→ LED rojo
    GPIO 27 ──────────→ LED verde
    GPIO 14 ──────────→ LED amarillo
    
    Todas → GND (Tierra común)
    
    NOTA: el SX1276 se movió a CS=15, RST=13, DIO0=4 para conservar
          los LEDs en 25/27/14 (firmware Device_ESP32_LoRaLAN)
```

---

## 📋 CONFIGURACIÓN DE PINES POR PROTOCOLO

### I2C Configuration
```cpp
// Wire I2C Bus
SDA = GPIO 21
SCL = GPIO 22
Frecuencia: 100 kHz (Standard)

// Inicialización:
Wire.begin(OLED_SDA_PIN, OLED_SCL_PIN);
```

### SPI Configuration
```cpp
// SPI Bus (COMPARTIDO entre PN532 y SX1276)
SCLK = GPIO 18 (Clock)
MOSI = GPIO 23 (Master Out Slave In)
MISO = GPIO 19 (Master In Slave Out)
CS_PN532  = GPIO 5  (Chip Select PN532)
CS_SX1276 = GPIO 15 (Chip Select SX1276)
RST_SX1276 = GPIO 13 (Reset SX1276)
DIO0_SX1276 = GPIO 4 (Interrupt SX1276)

// Inicialización SPI:
SPI.begin(PN532_SCK, PN532_MISO, PN532_MOSI, PN532_SS);

// Configuración LoRa (RadioLib, config.h):
SX1276 radio = new Module(LORA_CS, LORA_DIO0, LORA_RST, RADIOLIB_NC);
// donde:
// LORA_CS = 15
// LORA_RST = 13
// LORA_DIO0 = 4
```

### UART Configuration
```cpp
// Serial2 (Segunda UART)
RX = GPIO 16
TX = GPIO 17
Velocidad: 256000 baud
Inicialización:
Serial2.begin(256000, SERIAL_8N1, RADAR_RX_PIN, RADAR_TX_PIN);
```

---

## 🔋 ESQUEMA DE POTENCIA

```
┌─────────────────────────────────────────────────────┐
│           Fuente de Alimentación                    │
│           USB 5V o Batería Li-ion                   │
│                                                     │
│  ┌────────────────────────────────────────────┐    │
│  │        Regulador de Voltaje 5V → 3.3V      │    │
│  │  (Necesario si se usa USB 5V)             │    │
│  └────────────────────────────────────────────┘    │
│           ║                                         │
│        3.3V                                         │
│           ║                                         │
│    ┌──────┴──────┐                                 │
│    │   +3.3V BUS │                                 │
│    └──────┬──────┘                                 │
│           │                                         │
│    ┌──────┴───────────────────┬─────────────────┐  │
│    │                          │                 │  │
│ ESP32                    Capacitor             Capacitor
│ 3.3V                     100µF                 100µF
│    │                          │                 │  │
│    ├─────────→ BME280    ┌────┴────┐    ┌─────┴──┐
│    │                     │ BH1750  │    │  OLED  │
│    ├─────────→ PN532     └────┬────┘    └─────┬──┘
│    │                          │               │    │
│    │                          GND             GND   │
│    │                                               │
│    └─────────→ LEDs (c/u con 220Ω)               │
│    │                                               │
│    └─────────→ Buzzer + Control Relés            │
│                                                     │
│ GND ────────────┬───────────────────────────────┐  │
│                 │                               │  │
│              Tierra Común                        │  │
│              (BUS GND CENTRAL)                   │  │
└─────────────────────────────────────────────────┘
```

---

## ✅ LISTA DE VERIFICACIÓN TÉCNICA

### Antes de Encender
- [ ] Verificar alimentación 3.3V en todos los pines VCC
- [ ] Confirmar GND común en todos los componentes
- [ ] PN532: ⚠️ Verificar exactamente 3.3V (medidor)
- [ ] Revisar no hay puentes de cobre en la placa
- [ ] I2C: Pines SDA (21) y SCL (22) unidos correctamente
- [ ] SPI: Pines CLK (18), MOSI (23), MISO (19), CS (5) correctos
- [ ] UART2: RX (16) ↔ TX (17) cruzados correctamente
- [ ] LEDs: Ánodos a GPIO, Cátodos a GND (con resistencias)
- [ ] Relés: GPIO → Pin de entrada del módulo relé
- [ ] Buzzer: GPIO 33 → Positivo, GND → Negativo

### Después de Encender
- [ ] Serial Monitor: Ver inicialización de componentes
- [ ] LEDs: Secuencia de inicio (rojo → amarillo → verde)
- [ ] OLED: Mostrar pantalla de bienvenida
- [ ] Sensores: Reportar "✅" en resumen
- [ ] PN532: Confirmar detección de firmware
- [ ] LD2410: Confirmar versión de firmware
- [ ] No hay "❌ FALLO" en la inicialización

---

## 🚀 TABLA DE VELOCIDADES Y CONFIGURACIONES

| Interfaz | Parámetro | Valor | Unidad |
|----------|-----------|-------|--------|
| I2C | Frecuencia | 100 | kHz |
| I2C | SDA Pin | 21 | GPIO |
| I2C | SCL Pin | 22 | GPIO |
| SPI | Clock | 1-10 | MHz |
| SPI | CLK Pin | 18 | GPIO |
| SPI | MOSI Pin | 23 | GPIO |
| SPI | MISO Pin | 19 | GPIO |
| SPI PN532 | CS Pin | 5 | GPIO |
| SPI SX1276 | CS Pin | 15 | GPIO |
| SPI SX1276 | RST Pin | 13 | GPIO |
| SPI SX1276 | DIO0 Pin | 4 | GPIO |
| LoRa | Frecuencia | 869,525 | MHz |
| LoRa | Spreading Factor | 7-12 | - |
| LoRa | Bandwidth | 125 | kHz |
| UART2 | Baud Rate | 256000 | bps |
| UART2 | RX Pin | 16 | GPIO |
| UART2 | TX Pin | 17 | GPIO |

---

## 📝 NOTAS IMPORTANTES

1. **PN532 Alimentación:** Usar EXACTAMENTE 3.3V. No conectar a 5V.
2. **SX1276 Alimentación:** Usar EXACTAMENTE 3.3V. No conectar a 5V.
3. **SPI Compartido:** PN532 y SX1276 comparten CLK, MOSI, MISO pero tienen CS diferentes.
4. **I2C Bus:** Compartido por 3 sensores. Las direcciones no deben conflictuar.
5. **SPI Velocidad:** PN532 soporta hasta 10 MHz, SX1276 hasta 10 MHz. Configurado a 1 MHz para estabilidad.
6. **Relés Activos en LOW:** GPIO HIGH = APAGADO, GPIO LOW = ENCENDIDO.
7. **LoRa Configuración:** 869,525 MHz (sub-banda EU868 de 869,4-869,65 MHz, 10 % de ciclo de trabajo), SF7, BW 125kHz.
8. **LEDs y radio:** LEDs en GPIO 25 (rojo), 27 (verde) y 14 (amarillo); SX1276 en CS=15, RST=13, DIO0=4.

---

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

---

## 📞 REFERENCIA RÁPIDA

```
╔════════════════════════════════════════════════════╗
║           PINOUT RÁPIDO ESP32 v2.0                ║
╠════════════════════════════════════════════════════╣
║ SENSORES I2C (Bus Compartido):                    ║
║   SDA (GPIO 21) / SCL (GPIO 22)                   ║
║   Componentes: BME280, BH1750, OLED               ║
║                                                   ║
║ SENSORES SPI (Bus Compartido):                    ║
║   CLK(18) MOSI(23) MISO(19)                       ║
║   PN532 NFC:      CS(5) IRQ(34)                   ║
║   SX1276 LoRa:    CS(15) RST(13) DIO0(4)          ║
║                                                   ║
║ SENSOR SERIAL (LD2410 Radar):                     ║
║   RX(16) / TX(17) @ 256000 baud                   ║
║                                                   ║
║ ACTUADORES:                                       ║
║   Buzzer(33) / Relé1(26) / Relé2(32)             ║
║                                                   ║
║ COMUNICACIÓN LoRa (SX1276):                       ║
║   869,525 MHz, SF7, BW 125kHz                     ║
║   3.3V SOLAMENTE                                  ║
╚════════════════════════════════════════════════════╝
```

---

**Documento:** Tabla Completa de Pines ESP32 v2.0  
**Proyecto:** Control Asistencia IoT UEX  
**Actualización:** Febrero 2026  
**Versión:** 2.0 - Agregado módulo SX1276 LoRa, LEDs removidos
