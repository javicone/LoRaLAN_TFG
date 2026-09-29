# LoRaLAN Protocol Development Skill

A specialized skill for developing with the LoRaLAN protocol - a deterministic, low-latency local area network protocol optimized for LoRa physical layer communication with ESP32 microcontrollers and SX1276/SX1278 transceivers.

## Skill Overview

This skill provides comprehensive guidance for developing LoRaLAN applications, understanding the protocol architecture, and implementing both gateway and node devices using ESP32 microcontrollers with LoRa transceivers.

## Protocol Fundamentals

### What is LoRaLAN?

LoRaLAN is a custom MAC layer protocol designed for local area networks that require:
- **Deterministic behavior** - Unlike LoRaWAN's ALOHA approach
- **Low latency** - Sub-100ms Time on Air (ToA)
- **High security** - AES-128-CTR encryption with MIC integrity
- **Real-time capabilities** - Hybrid TDM/CAD MAC layer
- **Local deployment** - Star topology for campus/building environments

### Key Features

- **Star Topology**: Single gateway (ESP32), multiple nodes (up to 32)
- **Hybrid MAC**: TDM for routine traffic + CAD for critical events
- **Security**: Encrypt-then-MAC with session key derivation
- **Addressing**: 8-bit node IDs with network isolation
- **Payload**: Up to 64 bytes with 7-byte header
- **Frequency**: 868/915 MHz ISM bands with duty cycle compliance

## Hardware Requirements

### Supported Microcontrollers
- **ESP32**: Primary platform with dual-core architecture
  - Core 1: LoRaLAN protocol engine
  - Core 0: Communication bridge (WiFi/Ethernet/MQTT)
- **Other Arduino-compatible**: Basic support available

### LoRa Transceivers
- **SX1276**: Primary supported transceiver
- **SX1278**: Compatible with same driver
- **SX1262/SX1268**: Future support planned

### Typical Wiring (ESP32 + SX1276)
```
ESP32    SX1276
-----    ------
GPIO 15  -> CS (NSS)
GPIO 4   -> DIO0 (IRQ)
GPIO 13  -> RST (Reset)
GPIO 23  -> MOSI
GPIO 19  -> MISO
GPIO 18  -> SCK
3.3V     -> VCC
GND      -> GND
```

## Protocol Architecture

### Network Roles

#### Gateway (ESP32 Dual-Core)
- **Role**: Central coordinator and bridge
- **Address**: Always 0x00
- **Functions**:
  - Node authentication and session key management
  - TDM scheduling and polling
  - Time synchronization (BEACON frames)
  - Bridge to TCP/IP networks (WiFi/Ethernet)
  - MQTT/WebSocket/HTTP client capabilities

#### Nodes (Microcontrollers + LoRa)
- **Role**: End devices (sensors, actuators, user interfaces)
- **Address**: 0x01-0xFE (assigned during JOIN)
- **Functions**:
  - Sensor data collection
  - PUSH event transmission (critical events)
  - POLL response (routine metrics)
  - Local encryption and authentication

### Frame Structure

```
Header = 7 bytes on air. Byte 0 packs version, type and priority:

 byte 0 (control)           byte 1  byte 2  byte 3  byte 4  byte 5  byte 6
┌─────┬────────┬──────┐    ┌───────┬───────┬───────┬───────┬───────┬───────┐ ┌──────────┬─────┐
│ Ver │  Type  │ Prio │    │ Flags │ NetID │ SrcID │ DstID │  SEQ  │  Len  │ │ Payload  │ MIC │
│ 2b  │   4b   │  2b  │    │  8b   │  8b   │  8b   │  8b   │  8b   │  8b   │ │ 0-64 B   │ 4 B │
└─────┴────────┴──────┘    └───────┴───────┴───────┴───────┴───────┴───────┘ └──────────┴─────┘

byte0 = (version << 6) | (type << 2) | priority     e.g. PUSH, v1, HIGH -> 0x46
Priority: 0 LOW, 1 MEDIUM, 2 HIGH (3 reserved, rejected)
Flags: ACK_REQUIRED 0x01, IS_RETRY 0x02 (other bits reserved, rejected)
Frame size: 11-75 bytes. Payload AES-128-CTR, MIC = truncated AES-CMAC.
```

### Message Types

1. **PUSH_EVENT** (0x01): Asynchronous critical events (High Priority)
2. **ACK** (0x02): Acknowledgments (High Priority)
3. **POLL** (0x03): Gateway-initiated polling (Low Priority)
4. **POLL_RESPONSE** (0x04): Node response to poll (Low Priority)
5. **TIME_SYNC** (0x06): Clock synchronization beacons (Medium Priority)
6. **JOIN_REQUEST** (0x07): Node authentication request (Medium Priority)
7. **JOIN_ACCEPT** (0x08): Gateway authentication response (Medium Priority)
8. **LEAVE** (0x09): Clean node departure (Medium Priority)
9. **CMD** (0x0A): Encrypted gateway -> node command (High Priority)

## Development Workflow

### 1. Basic Gateway Setup

```cpp
#include <RadioLib.h>
#include <LoRaLAN.h>

// Hardware configuration
SX1276 radio = new Module(15, 4, 13, RADIOLIB_NC); // CS, DIO0, RST, DIO1
LoRaLANProtocol protocol(radio);

// Pre-shared application key (128-bit)
const uint8_t APPKEY[16] = {
    0x42,0x19,0xA7,0x5C,0xD3,0x88,0x11,0xE4,
    0x2F,0x90,0x6A,0xC1,0x7B,0x54,0x0D,0xBE
};

void setup() {
    Serial.begin(115200);
    
    // Initialize radio (868 MHz, 125 kHz BW, SF7, CR 4/5)
    int state = radio.begin(869.525, 125.0, 7, 5, 0x12, 17, 8, 0);
    if (state != RADIOLIB_ERR_NONE) {
        Serial.print("Radio init failed: "); Serial.println(state);
        return;
    }
    
    // Configure LoRaLAN gateway
    LoRaLANProtocol::GatewayConfig cfg;
    cfg.netId = 0x42;           // Network ID
    cfg.gatewayId = 0x00;       // Gateway address
    cfg.appKey = APPKEY;        // Pre-shared key
    
    protocol.begin(LoRaLANProtocol::Role::GATEWAY, &cfg, nullptr);
    protocol.setPollIntervalMs(2000);     // Poll nodes every 2 seconds
    protocol.setTimeSyncIntervalMs(60000); // Sync time every minute
    
    Serial.println("Gateway initialized");
}

void loop() {
    // Process protocol stack
    protocol.process();
    
    // Handle any received packets
    if (radio.getIrqStatus() & RADIOLIB_SX127X_IRQ_RX_DONE) {
        // Read received packet
        size_t len = radio.getPacketLength();
        uint8_t buffer[256];
        int state = radio.readData(buffer, len);
        
        if (state == RADIOLIB_ERR_NONE) {
            protocol.handleReceived(buffer, len);
        }
    }
    
    delay(10);
}
```

### 2. Basic Node Setup

```cpp
#include <RadioLib.h>
#include <LoRaLAN.h>

SX1276 radio = new Module(15, 4, 13, RADIOLIB_NC);
LoRaLANProtocol protocol(radio);

const uint8_t APPKEY[16] = {
    0x42,0x19,0xA7,0x5C,0xD3,0x88,0x11,0xE4,
    0x2F,0x90,0x6A,0xC1,0x7B,0x54,0x0D,0xBE
};

void setup() {
    Serial.begin(115200);
    
    // Initialize radio
    int state = radio.begin(869.525, 125.0, 7, 5, 0x12, 17, 8, 0);
    if (state != RADIOLIB_ERR_NONE) {
        Serial.print("Radio init failed: "); Serial.println(state);
        return;
    }
    
    // Configure LoRaLAN node
    LoRaLANProtocol::NodeConfig cfg;
    cfg.netId = 0x42;           // Same network as gateway
    cfg.nodeId = 0x10;          // Unique node address
    cfg.gatewayId = 0x00;       // Gateway address
    cfg.nodeType = 0x01;        // Node type identifier
    cfg.appKey = APPKEY;        // Pre-shared key
    
    protocol.begin(LoRaLANProtocol::Role::NODE, nullptr, &cfg);
    protocol.setJoinRetryMs(5000);  // Retry JOIN every 5 seconds
    
    // Configure Channel Activity Detection
    protocol.setCadPolicy(
        true,   // Enable CAD
        40,     // Base backoff (ms)
        5,      // Max exponential backoff
        50,     // Jitter percentage
        60,     // Backoff on error (ms)
        true    // CAD on JOIN
    );
    
    Serial.println("Node initialized - attempting to join network");
}

void loop() {
    protocol.process();
    
    // Handle received packets
    if (radio.getIrqStatus() & RADIOLIB_SX127X_IRQ_RX_DONE) {
        size_t len = radio.getPacketLength();
        uint8_t buffer[256];
        int state = radio.readData(buffer, len);
        
        if (state == RADIOLIB_ERR_NONE) {
            protocol.handleReceived(buffer, len);
        }
    }
    
    // Send PUSH events when joined
    if (protocol.isJoined()) {
        // Example: Send sensor data
        uint8_t sensorData[] = {0x01, 0x23, 0x45, 0x67}; // Temperature, humidity, etc.
        protocol.queuePushEvent(sensorData, sizeof(sensorData), true); // Require ACK
        
        delay(30000); // Send every 30 seconds
    } else {
        Serial.println("Waiting to join network...");
        delay(1000);
    }
}
```

### 3. Advanced Gateway with Bridge Functionality

For dual-core ESP32 implementation with network bridging:

```cpp
#include <WiFi.h>
#include <WebSocketsClient.h>
#include <PubSubClient.h>

// Core 1: LoRaLAN Protocol Task
void loralanTask(void *parameter) {
    // Initialize LoRaLAN protocol on Core 1
    // Handle radio interrupts and protocol processing
    
    while (true) {
        protocol.process();
        // Handle radio I/O
        vTaskDelay(pdMS_TO_TICKS(10));
    }
}

// Core 0: Communication Bridge Task  
void bridgeTask(void *parameter) {
    // Initialize WiFi/Ethernet
    WiFi.begin(ssid, password);
    
    // Initialize WebSocket/MQTT clients
    WebSocketsClient webSocket;
    PubSubClient mqttClient(wifiClient);
    
    while (true) {
        // Forward LoRaLAN packets to TCP/IP networks
        // Handle HTTP/WebSocket/MQTT communication
        vTaskDelay(pdMS_TO_TICKS(100));
    }
}

void setup() {
    // Create tasks for both cores
    xTaskCreatePinnedToCore(loralanTask, "LoRaLAN", 8192, NULL, 2, NULL, 1);
    xTaskCreatePinnedToCore(bridgeTask, "Bridge", 8192, NULL, 1, NULL, 0);
}
```

## Configuration Parameters

### Radio Configuration
```cpp
// Standard LoRa PHY parameters for LoRaLAN
radio.begin(
    869.525,      // Frequency (MHz) - EU868 sub-banda 869,4-869,65 (10 %)
    125.0,      // Bandwidth (kHz)
    7,          // Spreading Factor
    5,          // Coding Rate (4/5)
    0x12,       // Sync Word
    17,         // Output Power (dBm)
    8,          // Preamble Length
    0           // Gain (0 = auto)
);
```

### Protocol Timing
```cpp
// Gateway timing configuration
protocol.setPollIntervalMs(2000);        // TDM cycle interval
protocol.setTimeSyncIntervalMs(60000);    // BEACON transmission

// Node timing configuration
protocol.setJoinRetryMs(5000);            // JOIN attempt interval
protocol.setTxRetryPolicy(3, 1000);       // 3 retries, 1 second apart
```

### Security Configuration
```cpp
// Application key (shared secret)
const uint8_t APPKEY[16] = {
    0x42,0x19,0xA7,0x5C,0xD3,0x88,0x11,0xE4,
    0x2F,0x90,0x6A,0xC1,0x7B,0x54,0x0D,0xBE
};

// Network isolation
const uint8_t NETWORK_ID = 0x42;  // Prevents cross-network interference
```

## Troubleshooting Guide

### Common Issues

#### 1. Node Cannot Join Network
**Symptoms**: JOIN_REQUEST timeouts, no JOIN_ACCEPT received
**Solutions**:
- Verify APPKEY matches between gateway and node
- Check network ID consistency
- Ensure radio parameters are identical
- Verify GPIO connections and power supply
- Check frequency regulations for your region

#### 2. High Packet Loss
**Symptoms**: Missing ACKs, failed PUSH events
**Solutions**:
- Reduce poll interval to lower duty cycle
- Implement proper CAD backoff parameters
- Check for interference sources
- Verify antenna placement and orientation
- Monitor RSSI and SNR values

#### 3. Time Synchronization Issues
**Symptoms**: Nodes missing poll slots, timing drift
**Solutions**:
- Increase BEACON frequency
- Implement crystal oscillator compensation
- Add timing margin in slot calculations
- Use hardware timers for precision

#### 4. Security Failures
**Symptoms**: MIC validation errors, authentication failures
**Solutions**:
- Verify AES implementation on both sides
- Check sequence number handling
- Ensure proper nonce generation
- Validate key derivation process

### Debug Tools

```cpp
// Enable debug logging
#define LORALAN_DEBUG 1

// Monitor protocol state
Serial.print("Node joined: "); Serial.println(protocol.isJoined());
Serial.print("Session key valid: "); Serial.println(protocol.hasSessionKey());
Serial.print("Last RSSI: "); Serial.println(radio.getRSSI());
Serial.print("Last SNR: "); Serial.println(radio.getSNR());

// Packet statistics
LoRaLANProtocol::Stats stats = protocol.getStatistics();
Serial.print("TX packets: "); Serial.println(stats.txPackets);
Serial.print("RX packets: "); Serial.println(stats.rxPackets);
Serial.print("Failed transmissions: "); Serial.println(stats.txFailed);
```

## Best Practices

### 1. Power Management
```cpp
// Node sleep implementation
if (!protocol.hasPendingTasks()) {
    esp_sleep_enable_timer_wakeup(POLL_INTERVAL_US);
    esp_light_sleep_start();
}
```

### 2. Error Handling
```cpp
// Robust transmission handling
bool success = protocol.queuePushEvent(data, len, true);
if (!success) {
    // Queue full or network issues
    // Implement local storage or backoff
}
```

### 3. Network Management
```cpp
// Gateway node management
protocol.setNodeTimeout(30000);  // Mark nodes inactive after 30s
protocol.forceNodeRejoin(nodeId); // Force specific node to rejoin
```

### 4. Performance Optimization
```cpp
// Minimize processing overhead
protocol.setProcessingMode(LoRaLANProtocol::OPTIMIZED);
protocol.enableHardwareTimers(true);
protocol.setCryptoAcceleration(true);
```

## Use Cases and Applications

### 1. Building Automation
- HVAC sensor networks
- Access control systems  
- Smart lighting control
- Energy monitoring

### 2. Industrial IoT
- Machine monitoring
- Process control
- Asset tracking
- Predictive maintenance

### 3. Campus Networks
- Student ID systems
- Environmental monitoring
- Security surveillance
- Emergency communications

### 4. Smart Agriculture
- Soil monitoring
- Irrigation control
- Weather stations
- Livestock tracking

## Advanced Features

### 1. Custom Message Types
```cpp
// Define custom application messages
#define MSG_SENSOR_DATA    0x10
#define MSG_ACTUATOR_CMD   0x11
#define MSG_CONFIG_UPDATE  0x12

// Implement custom handlers
protocol.setCustomMessageHandler(handleCustomMessage);
```

### 2. Mesh Network Extension
```cpp
// Multi-hop routing (future enhancement)
protocol.enableMeshRouting(true);
protocol.setMaxHops(3);
```

### 3. Frequency Hopping
```cpp
// Channel agility for interference avoidance
uint32_t channels[] = {868100000, 868300000, 868500000};
protocol.setChannelPlan(channels, 3);
```

## Regulatory Compliance

### EU868 Band
- **Frequency**: 863-870 MHz
- **Max Power**: 25 mW (14 dBm)
- **Duty Cycle**: 1% (868.0-868.6 MHz), 0.1% (868.7-869.2 MHz), 10% (869.4-869.65 MHz, usada por LoRaLAN: 869.525)
- **Bandwidth**: 125 kHz typical

### US915 Band
- **Frequency**: 902-928 MHz
- **Max Power**: 30 dBm
- **Duty Cycle**: No restrictions
- **Bandwidth**: 125/500 kHz

### Duty Cycle Management
```cpp
// Calculate duty cycle compliance
float dutyCapacity = protocol.calculateDutyCycleUsage();
if (dutyCapacity > 0.09) {  // Stay under 10%
    protocol.reducePollFrequency();
}
```

## Future Enhancements

1. **Over-the-Air Updates**: Firmware updates via LoRaLAN
2. **Advanced Routing**: Multi-hop mesh capabilities  
3. **QoS Extensions**: Enhanced priority management
4. **Energy Harvesting**: Ultra-low power node support
5. **Cloud Integration**: Native IoT platform connectors

## Resources and References

- **RadioLib Documentation**: https://github.com/jgromes/RadioLib
- **ESP32 Technical Reference**: https://docs.espressif.com/
- **LoRa Alliance Specifications**: https://lora-alliance.org/
- **AES Encryption Standards**: NIST SP 800-38A
- **ISM Band Regulations**: ETSI EN 300 220, FCC Part 15

---

This skill provides comprehensive guidance for developing robust LoRaLAN applications with ESP32 and LoRa transceivers, covering everything from basic setup to advanced network deployment scenarios.