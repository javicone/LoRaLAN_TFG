#include "globals.h"

// ============================================
// FUNCIONES NFC (Polling directo - más estable en ESP32)
// ============================================

// Tiempo de espera corto para polling (ms)
#define NFC_POLL_TIMEOUT 100

bool setupNFC() {
  Serial.println("🔧 Inicializando PN532 (SPI) con diagnóstico...");
  
  // ✅ Inicializar SPI con pines explícitos
  SPI.begin(PN532_SCK, PN532_MISO, PN532_MOSI, PN532_SS);

  delay(1000);

  // Intentar inicializar varias veces
  const int max_attempts = 4;
  bool ok = false;
  
  for (int attempt = 1; attempt <= max_attempts; ++attempt) {
    Serial.printf("📡 Intento PN532 #%d/%d\n", attempt, max_attempts);
    
    nfc.begin();
    delay(500);

    uint32_t versiondata = nfc.getFirmwareVersion();

    if (versiondata) {
      Serial.print("✅ PN53x detectado! Version: 0x");
      Serial.println(versiondata, HEX);
      Serial.print("   Chip: PN5");
      Serial.println((versiondata >> 24) & 0xFF, HEX);
      Serial.print("   Firmware: ");
      Serial.print((versiondata >> 16) & 0xFF, DEC);
      Serial.print(".");
      Serial.println((versiondata >> 8) & 0xFF, DEC);
      
      // Configurar para leer tarjetas MIFARE
      nfc.SAMConfig();
      
      // ✅ Reducir reintentos para polling rápido (no bloquea)
      nfc.setPassiveActivationRetries(0x01);
      
      sensores.pn532_ok = true;
      sensores.pn532_errores = 0;
      ok = true;
      break;
    } else {
      Serial.println("❌ No se encontró la placa PN53x en este intento.");
      delay(200);
    }
  }

  if (!ok) {
    Serial.println("❌ ERROR CRÍTICO: PN53x NO detectado tras 4 intentos.");
    Serial.println("📋 Diagnóstico:");
    Serial.printf("   - PN532_SS:   GPIO %d\n", PN532_SS);
    Serial.printf("   - PN532_SCK:  GPIO %d\n", PN532_SCK);
    Serial.printf("   - PN532_MISO: GPIO %d\n", PN532_MISO);
    Serial.printf("   - PN532_MOSI: GPIO %d\n", PN532_MOSI);
    Serial.println("🔍 Verifica:");
    Serial.println("   1. Alimentación 3.3V (NO 5V)");
    Serial.println("   2. Cables firmemente conectados");
    Serial.println("   3. Módulo PN532 en modo SPI (switches/jumpers)");
    Serial.println("   4. Pines SPI correctos según tu placa ESP32");
    
    sensores.pn532_ok = false;
    sensores.pn532_errores++;
    return false;
  }
  
  return true;
}

void startListeningToNFC() {
  // Ya no necesitamos hacer nada especial - usamos polling directo
}


NFCData leerNFC() {
  NFCData datos = {false, false, ""};
  
  if(!sensores.pn532_ok) {
    Serial.println("⚠️ PN532 no disponible");
    return datos;
  }
  
  uint8_t uid_local[] = {0, 0, 0, 0, 0, 0, 0};
  uint8_t uidLength = 0;
  
  // ✅ POLLING DIRECTO: readPassiveTargetID con timeout corto
  // Timeout de 100ms - no bloquea si no hay tarjeta
  uint8_t success = nfc.readPassiveTargetID(PN532_MIFARE_ISO14443A, uid_local, &uidLength, NFC_POLL_TIMEOUT);
  
  if(success) {
    if(uidLength != 4 && uidLength != 7) {
      Serial.printf("❌ UID longitud inválida: %d bytes\n", uidLength);
      return datos;
    }
    
    Serial.print("✅ Tarjeta detectada - UID: ");
    for(uint8_t i = 0; i < uidLength; i++) {
      Serial.print("0x");
      if(uid_local[i] < 0x10) Serial.print("0");
      Serial.print(uid_local[i], HEX);
      Serial.print(" ");
    }
    Serial.println();
    
    datos.uid = uidToString(uid_local, uidLength);
    memcpy(datos.uidBytes, uid_local, uidLength);
    datos.uidLen = uidLength;
    datos.tarjetaDetectada = true;
    datos.valido = true;
    
    Serial.println("UID String: " + datos.uid);
  }
  // Si no hay tarjeta, simplemente retorna datos con valido=false (sin mensaje de error)
  
  return datos;
}