bool setupBME(){
  Serial.print("🌡️  Inicializando BME280... ");
  const uint8_t reintentos = 3;
  
  for(uint8_t i = 0; i < reintentos; i++) {
    if(i > 0) {
      Serial.printf("Reintento %d/%d... ", i + 1, reintentos);
      delay(1000);
    }
    
    if(bme.begin(0x76)) {  // Dirección I2C típica
      Serial.println("✅ OK");
      sensores.bme280_ok = true;
      sensores.bme280_errores = 0;
      return true;
    }
  }
  
  Serial.println("❌ FALLO");
  sensores.bme280_ok = false;
  sensores.bme280_errores++;
  indicarEstado(ERROR);
  return false;
}

// No redefinir BME280Data aquí, ya está en globals.h


BME280Data leerBME280() {
  BME280Data datos = {false, -999.0, -999.0, -999.0};
  
  // Verificar si el sensor está disponible
  if(!sensores.bme280_ok) {
    Serial.println("⚠️ BME280 no disponible, reintentando...");
    if(!setupBME()) {
      return datos; // Devolver datos inválidos
    }
  }
  
  // Leer valores
  datos.temperatura = bme.readTemperature();
  datos.humedad = bme.readHumidity();
  datos.presion = bme.readPressure() / 100.0F;
  
  // Validar temperatura (-40°C a 85°C según datasheet)
  if(isnan(datos.temperatura) || datos.temperatura < -40 || datos.temperatura > 85) {
    Serial.printf("❌ Temperatura inválida: %.2f°C\n", datos.temperatura);
    sensores.bme280_ok = false;
    datos.temperatura = -999.0;
    return datos;
  }
  
  // Validar humedad (0% a 100%)
  if(isnan(datos.humedad) || datos.humedad < 0 || datos.humedad > 100) {
    Serial.printf("❌ Humedad inválida: %.2f%%\n", datos.humedad);
    sensores.bme280_ok = false;
    datos.humedad = -999.0;
    return datos;
  }
  
  // Validar presión (300 hPa a 1100 hPa según datasheet)
  if(isnan(datos.presion) || datos.presion < 300 || datos.presion > 1100) {
    Serial.printf("❌ Presión inválida: %.2f hPa\n", datos.presion);
    sensores.bme280_ok = false;
    datos.presion = -999.0;
    return datos;
  }
  
  // Todo OK
  datos.valido = true;
  return datos;
}