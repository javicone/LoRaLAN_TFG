bool setupLightMeter(){
  Serial.print("💡 Inicializando BH1750... ");
  const uint8_t reintentos = 3;
  
  for(uint8_t i = 0; i < reintentos; i++) {
    if(i > 0) {
      Serial.printf("Reintento %d/%d... ", i + 1, reintentos);
      delay(1000);
    }
    
    if(lightMeter.begin(BH1750::CONTINUOUS_HIGH_RES_MODE)) {
      Serial.println("✅ OK");
      sensores.bh1750_ok = true;
      sensores.bh1750_errores = 0;
      return true;
}
  }
  
  Serial.println("❌ FALLO");
  sensores.bh1750_ok = false;
  sensores.bh1750_errores++;
  return false;
}


LuzData leerLuz() {
  LuzData datos = {false, -999.0};
  
  // Verificar si el sensor está disponible
  if(!sensores.bh1750_ok) {
    Serial.println("⚠️ BH1750 no disponible, reintentando...");
    if(!setupLightMeter()) {
      return datos;
    }
  }
  
  // Leer valor
  datos.lux = lightMeter.readLightLevel();
  
  // Validar luz (0 a 65535 lux según datasheet BH1750)
  if(datos.lux < 0 || datos.lux > 65535) {
    Serial.printf("❌ Luz inválida: %.2f lux\n", datos.lux);
    sensores.bh1750_ok = false;
    datos.lux = -999.0;
    return datos;
  }
  
  // Validar que no sea NaN
  if(isnan(datos.lux)) {
    Serial.println("❌ Lectura de luz es NaN");
    sensores.bh1750_ok = false;
    datos.lux = -999.0;
    return datos;
  }
  
  // Todo OK
  datos.valido = true;
  return datos;
}