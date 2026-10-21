#pragma once

#include <stdint.h>
#include <stddef.h>
#include <string.h>

#if defined(__has_include)
#  if __has_include(<mbedtls/aes.h>)
#    include <mbedtls/aes.h>
#    define LORALAN_HAS_MBEDTLS_AES 1
#  else
#    define LORALAN_HAS_MBEDTLS_AES 0
#  endif
#  if __has_include(<mbedtls/cmac.h>)
#    include <mbedtls/cmac.h>
#    define LORALAN_HAS_MBEDTLS_CMAC 1
#  else
#    define LORALAN_HAS_MBEDTLS_CMAC 0
#  endif
#else
#  define LORALAN_HAS_MBEDTLS_AES 0
#  define LORALAN_HAS_MBEDTLS_CMAC 0
#endif

namespace loralan_crypto {

// Deriva una session key por nodo: AES-CMAC(APPKEY, "LLK" | netId | nodeId | DevNonce).
//
// Antes se usaba CRC32. El CRC es lineal: con la clave de un nodo se calculaba
// la de cualquier otro sin conocer la APPKEY, y los 128 bits solo aportaban 32
// de secreto. CMAC no tiene esa estructura.
//
// El prefijo "LLK" separa este uso de la APPKEY del CMAC de las tramas JOIN,
// que tambien se firman con ella.
//
// FALLA EN CERRADO: sin CMAC devuelve false y no hay sesion.
inline bool deriveSessionKey(const uint8_t appKey[16],
                             uint8_t netId,
                             uint8_t nodeId,
                             uint16_t devNonce,
                             uint8_t outKey[16]) {
#if !LORALAN_HAS_MBEDTLS_CMAC
    (void)appKey;
    (void)netId;
    (void)nodeId;
    (void)devNonce;
    (void)outKey;
    return false;
#else
    if (appKey == nullptr || outKey == nullptr) {
        return false;
    }

    const uint8_t msg[7] = {
        'L', 'L', 'K', netId, nodeId,
        static_cast<uint8_t>((devNonce >> 8) & 0xFF),
        static_cast<uint8_t>(devNonce & 0xFF)
    };

    const mbedtls_cipher_info_t* info =
        mbedtls_cipher_info_from_type(MBEDTLS_CIPHER_AES_128_ECB);
    return info != nullptr &&
           mbedtls_cipher_cmac(info, appKey, 128, msg, sizeof(msg), outKey) == 0;
#endif
}

// Codigo de autenticacion de mensaje: CMAC-AES-128 truncado a 4 bytes.
//
// Sustituye al CRC-32 sin clave que se usaba antes como "MIC". Un CRC detecta
// corrupciones del canal pero no autentica: cualquiera que conozca el formato
// puede recalcularlo. Con CMAC, producir un valor valido exige la clave.
//
// Se trunca a 4 bytes por compatibilidad con el formato de trama existente.
// Es el mismo tamano que emplea LoRaWAN: da 2^-32 de probabilidad de que una
// falsificacion al azar cuele, suficiente para una LAN donde ademas cada
// intento cuesta un tiempo en el aire nada despreciable.
//
// FALLA EN CERRADO: si mbedTLS no ofrece CMAC, devuelve false y la trama no se
// construye ni se valida. Nunca cae de vuelta al CRC, porque eso seria una
// degradacion silenciosa de seguridad.
inline bool cmacAes128(const uint8_t key[16],
                       const uint8_t* data,
                       size_t length,
                       uint8_t outMic[4]) {
#if !LORALAN_HAS_MBEDTLS_CMAC
    (void)key;
    (void)data;
    (void)length;
    (void)outMic;
    return false;
#else
    if (key == nullptr || outMic == nullptr) {
        return false;
    }
    if (length > 0 && data == nullptr) {
        return false;
    }

    const mbedtls_cipher_info_t* info =
        mbedtls_cipher_info_from_type(MBEDTLS_CIPHER_AES_128_ECB);
    if (info == nullptr) {
        return false;
    }

    uint8_t full[16];
    if (mbedtls_cipher_cmac(info, key, 128, data, length, full) != 0) {
        return false;
    }

    memcpy(outMic, full, 4);   // truncado a los 4 primeros bytes
    return true;
#endif
}

// Comparacion en tiempo constante: comparar MICs con memcmp permitiria deducir
// byte a byte un valor valido midiendo el tiempo de rechazo.
inline bool micEquals(const uint8_t a[4], const uint8_t b[4]) {
    uint8_t diff = 0;
    for (uint8_t i = 0; i < 4; ++i) {
        diff = static_cast<uint8_t>(diff | (a[i] ^ b[i]));
    }
    return diff == 0;
}

// Construye el nonce CTR con netId/srcId/seq.
inline void buildNonce(uint8_t nonce[16], uint8_t netId, uint8_t srcId, uint16_t seq) {
    memset(nonce, 0, 16);
    nonce[0] = netId;
    nonce[1] = srcId;
    nonce[2] = static_cast<uint8_t>(seq & 0xFF);
    nonce[3] = static_cast<uint8_t>(seq >> 8);
}

// Cifra/descifra AES-128-CTR.
inline bool aes128CtrCrypt(const uint8_t key[16],
                           const uint8_t nonce[16],
                           const uint8_t* input,
                           uint8_t* output,
                           size_t length) {
#if !LORALAN_HAS_MBEDTLS_AES
    (void)key;
    (void)nonce;
    (void)input;
    (void)output;
    (void)length;
    return false;
#else
    if (key == nullptr || nonce == nullptr) {
        return false;
    }

    if (length > 0 && (input == nullptr || output == nullptr)) {
        return false;
    }

    mbedtls_aes_context aes;
    mbedtls_aes_init(&aes);

    int status = mbedtls_aes_setkey_enc(&aes, key, 128);
    if (status != 0) {
        mbedtls_aes_free(&aes);
        return false;
    }

    unsigned char nonceCounter[16];
    unsigned char streamBlock[16] = {0};
    size_t ncOffset = 0;

    memcpy(nonceCounter, nonce, 16);
    status = mbedtls_aes_crypt_ctr(
        &aes,
        length,
        &ncOffset,
        nonceCounter,
        streamBlock,
        input,
        output
    );

    mbedtls_aes_free(&aes);
    return status == 0;
#endif
}

} // namespace loralan_crypto
