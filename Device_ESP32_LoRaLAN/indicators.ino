/*
 * ════════════════════════════════════════════════════════════════════════════
 * MÓDULO DE INDICADORES VISUALES Y SONOROS
 * ════════════════════════════════════════════════════════════════════════════
 * 
 * Este módulo gestiona todos los indicadores del sistema:
 * - LEDs de estado (Verde/Amarillo/Rojo)
 * - Buzzer para alertas sonoras
 * - Relé para control de dispositivos (calefacción, etc.)
 * 
 * Estados soportados:
 * - NORMAL:      Sistema funcionando correctamente
 * - ADVERTENCIA: Sensor con problemas, no crítico
 * - ERROR:       Fallo de sensor, requiere atención
 * - CRITICO:     Múltiples fallos, sistema comprometido
 * 
 * Autor: Sistema de Control de Asistencia
 * Fecha: 2025
 * ════════════════════════════════════════════════════════════════════════════
 */

#include "globals.h"

// ============================================================================
// INICIALIZACIÓN DE INDICADORES
// ============================================================================

void setupIndicators() {
  Serial.println("\n╔════════════════════════════════╗");
  Serial.println("║  INICIALIZANDO INDICADORES     ║");
  Serial.println("╚════════════════════════════════╝");
  
  // Configurar pines como salida
  pinMode(ledRojoPin, OUTPUT);
  pinMode(ledVerdePin, OUTPUT);
  pinMode(ledAmarilloPin, OUTPUT);
  pinMode(buzzerPin, OUTPUT);
  pinMode(relayPin1, OUTPUT);
  pinMode(relayPin2, OUTPUT);
  
  Serial.println("📌 Pines configurados:");
  Serial.printf("   LED Rojo:     GPIO %d\n", ledRojoPin);
  Serial.printf("   LED Verde:    GPIO %d\n", ledVerdePin);
  Serial.printf("   LED Amarillo: GPIO %d\n", ledAmarilloPin);
  Serial.printf("   Buzzer:       GPIO %d\n", buzzerPin);
  Serial.printf("   Relé 1:       GPIO %d\n", relayPin1);
  Serial.printf("   Relé 2:       GPIO %d\n", relayPin2);
  
  // Estado inicial: todos apagados
  digitalWrite(ledRojoPin, LOW);
  digitalWrite(ledVerdePin, LOW);
  digitalWrite(ledAmarilloPin, LOW);
  // Relés activos en LOW: HIGH = apagado. Antes se escribía LOW aquí, lo que
  // los encendía un instante en cada arranque.
  digitalWrite(relayPin1, HIGH);
  digitalWrite(relayPin2, HIGH);
  noTone(buzzerPin);
  
  Serial.println("🔌 Estado inicial: Todos apagados");
  
  // Test de arranque (secuencia visual)
  testIndicadores();
  
  Serial.println("✅ Indicadores inicializados correctamente\n");
}

// ============================================================================
// TEST DE ARRANQUE
// ============================================================================

void testIndicadores() {
  Serial.println("\n🧪 Ejecutando test de indicadores...");
  
  // Secuencia de LEDs
  Serial.print("   Verde... ");
  digitalWrite(ledVerdePin, HIGH);
  delay(200);
  Serial.println("✓");
  
  Serial.print("   Amarillo... ");
  digitalWrite(ledAmarilloPin, HIGH);
  delay(200);
  Serial.println("✓");
  
  Serial.print("   Rojo... ");
  digitalWrite(ledRojoPin, HIGH);
  delay(200);
  Serial.println("✓");
  
  Serial.print("   Buzzer... ");
  tone(buzzerPin, 1000, 150);
  delay(200);
  Serial.println("✓");
  
  // Apagar todos
  digitalWrite(ledVerdePin, LOW);
  digitalWrite(ledAmarilloPin, LOW);
  digitalWrite(ledRojoPin, LOW);
  
  Serial.println("✅ Test completado\n");
  delay(100);
}

// ============================================================================
// FUNCIÓN PRINCIPAL: INDICAR ESTADO
// ============================================================================

void indicarEstado(EstadoLED estado) {
  static EstadoLED estadoAnterior = NORMAL;
  
  // Solo loguear si cambia el estado
  if(estado != estadoAnterior) {
    Serial.println("\n╔════════════════════════════════╗");
    Serial.println("║   CAMBIO DE ESTADO SISTEMA     ║");
    Serial.println("╚════════════════════════════════╝");
    estadoAnterior = estado;
  }
  
  switch(estado) {
    case NORMAL:
      // LED verde encendido, todo OK
      digitalWrite(ledVerdePin, HIGH);
      digitalWrite(ledAmarilloPin, LOW);
      digitalWrite(ledRojoPin, LOW);
      noTone(buzzerPin);
      
      if(estado != estadoAnterior) {
        Serial.println("✅ Estado: NORMAL");
        Serial.println("   Todos los sistemas funcionando correctamente");
      }
      break;
      
    case ADVERTENCIA:
      // LED amarillo encendido, beep corto
      digitalWrite(ledVerdePin, LOW);
      digitalWrite(ledAmarilloPin, HIGH);
      digitalWrite(ledRojoPin, LOW);
      tone(buzzerPin, 1000, 100);  // Beep corto 1kHz
      
      if(estado != estadoAnterior) {
        Serial.println("⚠️  Estado: ADVERTENCIA");
        Serial.println("   Algunos sensores requieren atención");
      }
      break;
      
    case ERROR:
      // LED rojo parpadeante, beep medio
      digitalWrite(ledVerdePin, LOW);
      digitalWrite(ledAmarilloPin, LOW);
      parpadearLED(ledRojoPin, 3, 100);  // 3 parpadeos rápidos
      tone(buzzerPin, 800, 200);  // Beep medio 800Hz
      
      if(estado != estadoAnterior) {
        Serial.println("❌ Estado: ERROR");
        Serial.println("   Fallo detectado en sensores");
        Serial.println("   Se intentará recuperación automática");
      }
      break;
      
    case CRITICO:
      // LED rojo fijo, alarma sonora
      digitalWrite(ledVerdePin, LOW);
      digitalWrite(ledAmarilloPin, LOW);
      digitalWrite(ledRojoPin, HIGH);
      tone(buzzerPin, 500, 500);  // Beep largo 500Hz (grave)
      
      if(estado != estadoAnterior) {
        Serial.println("🚨 Estado: CRÍTICO");
        Serial.println("   MÚLTIPLES FALLOS DETECTADOS");
        Serial.println("   Sistema comprometido - Requiere intervención manual");
      }
      break;
  }
  
  if(estado != estadoAnterior) {
    Serial.println("════════════════════════════════\n");
  }
}

// ============================================================================
// FUNCIÓN AUXILIAR: PARPADEAR LED
// ============================================================================

void parpadearLED(int pin, int veces, int duracion_ms) {
  for(int i = 0; i < veces; i++) {
    digitalWrite(pin, HIGH);
    delay(duracion_ms);
    digitalWrite(pin, LOW);
    if(i < veces - 1) {  // No esperar en la última iteración
      delay(duracion_ms);
    }
  }
}

// ============================================================================
// CONTROL DE RELÉS
// ============================================================================

// Relés activos en LOW: true = encendido = LOW. Es la convención con la que se
// manejaban desde el panel; estas funciones escribían la contraria y no se usaban.
void activarRele1(bool estado) {
  digitalWrite(relayPin1, estado ? LOW : HIGH);
  relay1Estado = estado;
  
  Serial.println("\n╔════════════════════════════════╗");
  Serial.println("║    CONTROL DE RELÉ 1           ║");
  Serial.println("╚════════════════════════════════╝");
  Serial.printf("🔌 Relé 1 (Calefacción): %s\n", estado ? "ACTIVADO ⚡" : "DESACTIVADO 🔴");
  Serial.println("════════════════════════════════\n");
}

void activarRele2(bool estado) {
  digitalWrite(relayPin2, estado ? LOW : HIGH);
  relay2Estado = estado;
  
  Serial.println("\n╔════════════════════════════════╗");
  Serial.println("║    CONTROL DE RELÉ 2           ║");
  Serial.println("╚════════════════════════════════╝");
  Serial.printf("� Relé 2 (Iluminación): %s\n", estado ? "ACTIVADO ⚡" : "DESACTIVADO 🔴");
  Serial.println("════════════════════════════════\n");
}

bool estadoRele1() {
  return digitalRead(relayPin1) == LOW;
}

bool estadoRele2() {
  return digitalRead(relayPin2) == LOW;
}

// ============================================================================
// FUNCIONES ADICIONALES DE INDICADORES
// ============================================================================

void ledVerde(bool estado) {
  digitalWrite(ledVerdePin, estado ? HIGH : LOW);
}

void ledAmarillo(bool estado) {
  digitalWrite(ledAmarilloPin, estado ? HIGH : LOW);
}

void ledRojo(bool estado) {
  digitalWrite(ledRojoPin, estado ? HIGH : LOW);
}

void beep(int frecuencia, int duracion) {
  tone(buzzerPin, frecuencia, duracion);
}

void beepCorto() {
  tone(buzzerPin, 1000, 50);
}

void beepLargo() {
  tone(buzzerPin, 800, 300);
}

void beepError() {
  tone(buzzerPin, 500, 500);
}

// ============================================================================
// SECUENCIAS ESPECIALES
// ============================================================================

void secuenciaInicio() {
  Serial.println("🎵 Secuencia de inicio...");
  
  // Barrido de LEDs
  digitalWrite(ledVerdePin, HIGH);
  beep(800, 100);
  delay(150);
  digitalWrite(ledVerdePin, LOW);
  
  digitalWrite(ledAmarilloPin, HIGH);
  beep(1000, 100);
  delay(150);
  digitalWrite(ledAmarilloPin, LOW);
  
  digitalWrite(ledRojoPin, HIGH);
  beep(1200, 100);
  delay(150);
  digitalWrite(ledRojoPin, LOW);
  
  delay(100);
  
  // Encender verde final
  digitalWrite(ledVerdePin, HIGH);
  beep(1500, 150);
  delay(300);
}

void secuenciaError() {
  Serial.println("⚠️ Secuencia de error...");
  
  for(int i = 0; i < 3; i++) {
    digitalWrite(ledRojoPin, HIGH);
    digitalWrite(ledAmarilloPin, HIGH);
    beep(400, 100);
    delay(150);
    
    digitalWrite(ledRojoPin, LOW);
    digitalWrite(ledAmarilloPin, LOW);
    delay(150);
  }
}

void secuenciaExito() {
  Serial.println("✅ Secuencia de éxito...");
  
  digitalWrite(ledVerdePin, HIGH);
  beep(1200, 100);
  delay(100);
  beep(1500, 100);
  delay(100);
  beep(1800, 150);
  delay(300);
  digitalWrite(ledVerdePin, LOW);
}

// ============================================================================
// APAGAR TODOS LOS INDICADORES
// ============================================================================

void apagarTodosLosIndicadores() {
  digitalWrite(ledRojoPin, LOW);
  digitalWrite(ledVerdePin, LOW);
  digitalWrite(ledAmarilloPin, LOW);
  noTone(buzzerPin);
  
  Serial.println("🔇 Todos los indicadores apagados");
}
