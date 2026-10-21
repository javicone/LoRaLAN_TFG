#pragma once

#include <Arduino.h>
#include <RadioLib.h>
#include "LoRaLANPacket.h"
#include "LoRaLANManager.h"
#include "LoRaLANCrypto.h"

// Base comun de LoRaLANGateway y LoRaLANNode. No se instancia directamente.
//
// Reune lo que ambos roles hacen igual: recibir y autenticar tramas, sellarlas
// con el MIC, transmitir, confirmar con ACK y las politicas de reintento. Cada
// rol aporta solo lo que le es propio a traves de tres metodos virtuales.
class LoRaLANDevice {
public:
    // Parametros comunes a los dos roles. Los Config de cada rol heredan de aqui.
    struct BaseConfig {
        uint8_t netId = 0;
        const uint8_t* appKey = nullptr;
        uint8_t txMaxRetries = 3;          // reintentos de radio por trama
        uint32_t txTimeoutMs = 2500UL;
        uint8_t appMaxRetries = 3;         // reintentos por falta de ACK (PUSH, POLL_RESPONSE, CMD)
        uint32_t appAckTimeoutMs = 1200UL;
    };

    // Callback para payloads recibidos (ya descifrados).
    using EventCallback = void (*)(const LoRaLANFrame& frame, const uint8_t* payload, uint8_t payloadLen);

    // Tick de servicio: temporizadores, reintentos y tareas periodicas del rol.
    void process();

    // Entrega una trama recibida por la radio. Devuelve true si se acepto.
    bool handleReceived(const uint8_t* buffer, size_t len);

    // Define APPKEY para JOIN/KDF.
    void setAppKey(const uint8_t key[16]);

    // Politicas de reintento.
    void setTxRetryPolicy(uint8_t maxRetries, uint32_t timeoutMs);
    void setAppRetryPolicy(uint8_t maxRetries, uint32_t ackTimeoutMs);

protected:
    // Enlaza con cualquier radio de RadioLib: se depende de PhysicalLayer.
    explicit LoRaLANDevice(PhysicalLayer& radio);
    ~LoRaLANDevice() = default;   // protegido: no se destruye via puntero base

    // Lo que cada rol hace distinto.
    // Clave de sesion con el interlocutor peerId, o nullptr si no hay sesion.
    virtual const uint8_t* sessionKeyFor(uint8_t peerId) const = 0;
    // Trama autenticada (y que no es ACK). Devuelve false si se rechaza.
    // Si el rol responde el ACK por su cuenta (ACK diferido), pone
    // ackHandled_ = true y no se envia el automatico.
    virtual bool onFrame(const LoRaLANFrame& frame) = 0;
    // Tareas periodicas del rol.
    virtual void onProcess() = 0;

    void applyBaseConfig(const BaseConfig& config, uint8_t localId);
    void fillBaseConfig(BaseConfig& config) const;

    // Clave con la que se calcula o verifica el MIC de una trama:
    //   - Asociacion (JOIN_*): APPKEY, porque aun no hay clave de sesion.
    //   - Difusion (dstId 0xFF): APPKEY, la unica que comparten todos los nodos.
    //     Autentica "alguien con la APPKEY", no "el gateway": un nodo
    //     comprometido podria falsificar un TIME_SYNC.
    //   - Resto: clave de sesion del interlocutor (sessionKeyFor).
    // nullptr = sin clave: la trama ni se emite ni se acepta.
    const uint8_t* micKeyFor(uint8_t msgType, uint8_t peerId, uint8_t dstId) const;

    // Resultado de escuchar el canal antes de transmitir (CAD).
    enum class ChannelState : uint8_t { FREE, BUSY, ERROR };

    // CAD por software: no necesita DIO1. Deja la radio de nuevo en recepcion.
    ChannelState checkChannel();

    // Sella con el MIC y transmite una trama sin espera de ACK.
    bool sendRaw(const uint8_t* buffer, uint16_t len, const __FlashStringHelper* label);
    bool sendAck(uint8_t dstId, uint16_t seq, const uint8_t* payload = nullptr, uint8_t payloadLen = 0);

    // Anti-repeticion: true si seq es nueva (y avanza lastSeq).
    static bool acceptSeq(uint16_t seq, uint16_t& lastSeq, bool& haveSeq);

    PhysicalLayer& radio_;
    LoRaLANManager manager_;
    BaseConfig base_{};          // base_.appKey apunta a appKey_
    uint8_t appKey_[16] = {0};
    uint8_t localId_ = 0;        // gatewayId o nodeId
    bool ackHandled_ = false;    // lo pone onFrame() si el rol responde el ACK
};
