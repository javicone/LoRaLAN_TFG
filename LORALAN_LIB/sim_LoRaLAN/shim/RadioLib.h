#pragma once
// Shim de RadioLib para el simulador: SX1276 falso conectado a un canal virtual.
// Las constantes usan los MISMOS valores que RadioLib real (TypeDef.h),
// para que el comportamiento del protocolo sea identico al del hardware.

#include <stdint.h>
#include <stddef.h>

#define RADIOLIB_ERR_NONE          (0)
#define RADIOLIB_PREAMBLE_DETECTED (-14)   // CAD ocupado en SX127x
#define RADIOLIB_CHANNEL_FREE      (-15)
#define RADIOLIB_LORA_DETECTED     (-702)  // CAD ocupado en SX126x / LR11x0

// Mismo valor que en PhysicalLayer.h de RadioLib.
enum RadioLibIrqType_t {
    RADIOLIB_IRQ_CAD_DONE = 0x07
};

// Implementadas en main.cpp (canal virtual).
int sim_channel_transmit(int radioId, const uint8_t* data, size_t len);
int sim_channel_scan(int radioId);

// Base polimorfica minima, con el mismo nombre que la de RadioLib real:
// el protocolo depende de PhysicalLayer, no del modulo concreto.
class PhysicalLayer {
public:
    virtual ~PhysicalLayer() = default;
    virtual int transmit(uint8_t* data, size_t len) = 0;
    virtual int startReceive() = 0;
    // CAD asincrono, el que usa la libreria.
    virtual int16_t startChannelScan() = 0;
    virtual int16_t checkIrq(RadioLibIrqType_t irq) = 0;
    virtual int16_t getChannelScanResult() = 0;
};

class SX1276 : public PhysicalLayer {
public:
    explicit SX1276(int radioId = 0) : id_(radioId) {}
    int transmit(uint8_t* data, size_t len) override { return sim_channel_transmit(id_, data, len); }
    int startReceive() override { return RADIOLIB_ERR_NONE; } // la entrega la hace el canal virtual

    // El CAD del canal virtual es instantaneo: el resultado se fija al empezar.
    int16_t startChannelScan() override {
        scanResult_ = static_cast<int16_t>(sim_channel_scan(id_));
        scanning_ = true;
        return RADIOLIB_ERR_NONE;
    }
    int16_t checkIrq(RadioLibIrqType_t irq) override {
        return (irq == RADIOLIB_IRQ_CAD_DONE && scanning_) ? 1 : 0;
    }
    int16_t getChannelScanResult() override {
        scanning_ = false;
        return scanResult_;
    }

private:
    int id_;
    bool scanning_ = false;
    int16_t scanResult_ = RADIOLIB_CHANNEL_FREE;
};
