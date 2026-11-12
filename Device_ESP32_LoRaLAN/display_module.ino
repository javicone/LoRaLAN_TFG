/*********
 * Display OLED 0.95" SSD1306 128x64 - Módulo de control
 * 
 * Muestra información del sistema:
 * - Id del nodo y estado del enlace LoRaLAN
 * - Estado del lector NFC
 * - Temperatura y otros sensores
 *********/

#include "globals.h"
#include "config.h"

// Dimensiones de la pantalla
#define SCREEN_WIDTH  128
#define SCREEN_HEIGHT 64

// Variables globales para actualización de pantalla
unsigned long lastDisplayUpdate = 0;
#define DISPLAY_UPDATE_INTERVAL 1000  // Actualizar cada segundo

// ============================================
// INICIALIZACIÓN DEL DISPLAY
// ============================================
bool setupDisplay() {
  Serial.println("🔧 Inicializando pantalla OLED 0.95\" (SSD1306)...");
  
  // Inicializar I2C en pines específicos
  Wire.begin(OLED_SDA_PIN, OLED_SCL_PIN);
  
  // Intentar inicializar la pantalla
  if(!display.begin(SSD1306_SWITCHCAPVCC, OLED_I2C_ADDRESS)) {
    Serial.println("❌ Error: No se detectó la pantalla OLED");
    Serial.printf("   Dirección I2C: 0x%02X\n", OLED_I2C_ADDRESS);
    Serial.printf("   SDA: GPIO %d | SCL: GPIO %d\n", OLED_SDA_PIN, OLED_SCL_PIN);
    
    sensores.display_ok = false;
    sensores.display_errores++;
    return false;
  }
  
  Serial.println("✅ Pantalla OLED inicializada correctamente");
  sensores.display_ok = true;
  
  // Configuración inicial de la pantalla
  display.setRotation(2);   // 180º: la OLED va montada del revés en la caja
  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);
  display.setCursor(0, 0);
  
  // Mensaje de bienvenida
  displayBootScreen();
  
  return true;
}

// ============================================
// PANTALLA DE INICIO
// ============================================
void displayBootScreen() {
  display.clearDisplay();
  display.setTextSize(1);
  display.setCursor(10, 10);
  display.println(F("Control Asistencia"));
  
  display.setTextSize(2);
  display.setCursor(20, 30);
  display.println(F("IoT UEX"));
  
  display.setTextSize(1);
  display.setCursor(25, 50);
  display.println(F("Iniciando..."));
  
  display.display();
  delay(2000);
}

// ============================================
// ACTUALIZAR PANTALLA PRINCIPAL
// ============================================
void updateDisplay() {
  if (!sensores.display_ok) return;
  
  // Control de frecuencia de actualización
  if (millis() - lastDisplayUpdate < DISPLAY_UPDATE_INTERVAL) {
    return;
  }
  lastDisplayUpdate = millis();
  
  display.clearDisplay();
  
  // ═══════════════════════════════════════
  // LÍNEA 1: Nodo y enlace LoRaLAN
  // ═══════════════════════════════════════
  display.setTextSize(1);
  display.setCursor(0, 0);
  display.printf("Nodo %03d", loraDireccion());

  display.setCursor(95, 0);
  display.print(loraAsociado() ? F("LoRa") : F("!LoRa"));

  // Línea separadora
  display.drawLine(0, 10, 127, 10, SSD1306_WHITE);

  // ═══════════════════════════════════════
  // LÍNEA 2-4: Estado del fichaje
  // ═══════════════════════════════════════
  // La clase en curso la valida el servidor al fichar: el nodo ya no la consulta.
  display.setCursor(0, 14);
  if (!loraAsociado()) {
    display.println(F("Buscando gateway..."));
  } else if (!sensores.pn532_ok) {
    display.println(F("NFC no disponible"));
  } else {
    display.println(F("Acerque la tarjeta"));
  }

  // ═══════════════════════════════════════
  // LÍNEA 5-6: Sensores
  // ═══════════════════════════════════════
  display.setCursor(0, 48);
  
  // Temperatura y Humedad
  if (sensores.bme280_ok) {
    BME280Data bmeData = leerBME280();  // ✅ CORREGIDO
    if (bmeData.valido) {
      display.print(bmeData.temperatura, 1);
      display.print(F("C "));
      display.print(bmeData.humedad, 0);
      display.print(F("%"));
    }
  }
  
  // Luz
  display.setCursor(0, 56);
  if (sensores.bh1750_ok) {
    LuzData luzData = leerLuz();  // ✅ YA ESTABA CORRECTO
    if (luzData.valido) {
      display.print(F("Luz:"));
      display.print(luzData.lux, 0);
      display.print(F("lx"));
    }
  }
  
  // Presencia (radar)
  if (sensores.ld2410_ok) {
    display.setCursor(80, 56);
    RadarData radarData = leerRadar();
    if (radarData.presenciaDetectada) {
      display.print(F("[PRES]"));
    }
  }
  
  display.display();
}

// ============================================
// MOSTRAR MENSAJE TEMPORAL
// ============================================
void displayMessage(String mensaje, int duracion_ms = 2000) {
  if (!sensores.display_ok) return;
  
  display.clearDisplay();
  display.setTextSize(1);
  display.setCursor(0, 28);
  
  // Centrar texto si es corto
  int16_t x1, y1;
  uint16_t w, h;
  display.getTextBounds(mensaje, 0, 28, &x1, &y1, &w, &h);
  
  if (w < 128) {
    display.setCursor((128 - w) / 2, 28);
  }
  
  display.println(mensaje);
  display.display();
  
  if (duracion_ms > 0) {
    delay(duracion_ms);
  }
}

// ============================================
// MOSTRAR FICHAJE EXITOSO
// ============================================
void displayFichajeExito(String uid) {
  if (!sensores.display_ok) return;
  
  display.clearDisplay();
  
  // Título grande
  display.setTextSize(2);
  display.setCursor(10, 5);
  display.println(F("FICHAJE"));
  display.setCursor(20, 25);
  display.println(F("EXITOSO!"));
  
  // UID
  display.setTextSize(1);
  display.setCursor(0, 50);
  display.print(F("UID: "));
  // Mostrar solo últimos 8 caracteres del UID
  if (uid.length() > 8) {
    display.print(uid.substring(uid.length() - 8));
  } else {
    display.print(uid);
  }
  
  display.display();
  delay(3000);
}

// ============================================
// MOSTRAR ERROR DE FICHAJE
// ============================================
void displayFichajeError() {
  if (!sensores.display_ok) return;
  
  display.clearDisplay();
  
  display.setTextSize(2);
  display.setCursor(25, 15);
  display.println(F("ERROR"));
  
  display.setTextSize(1);
  display.setCursor(10, 40);
  display.println(F("Fichaje fallido"));
  display.setCursor(5, 52);
  display.println(F("Intente nuevamente"));
  
  display.display();
  delay(2000);
}

// ============================================
// MOSTRAR INFORMACIÓN DE DEBUG
// ============================================
void displayDebugInfo() {
  if (!sensores.display_ok) return;
  
  display.clearDisplay();
  display.setTextSize(1);
  
  display.setCursor(0, 0);
  display.println(F("=== DEBUG ==="));
  
  display.setCursor(0, 12);
  display.print(F("LoRa: "));
  display.println(loraAsociado() ? "OK" : "OFF");
  
  display.setCursor(0, 22);
  display.print(F("BME: "));
  display.println(sensores.bme280_ok ? "OK" : "ERR");
  
  display.setCursor(0, 32);
  display.print(F("BH1750: "));
  display.println(sensores.bh1750_ok ? "OK" : "ERR");
  
  display.setCursor(0, 42);
  display.print(F("LD2410: "));
  display.println(sensores.ld2410_ok ? "OK" : "ERR");
  
  display.setCursor(0, 52);
  display.print(F("PN532: "));
  display.println(sensores.pn532_ok ? "OK" : "ERR");
  
  display.display();
}