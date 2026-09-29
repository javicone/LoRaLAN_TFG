bool setupRadar(){
    Serial2.begin(256000, SERIAL_8N1, RADAR_RX_PIN, RADAR_TX_PIN);  //UART for monitoring the radar
    radar.begin(Serial2, false); 
    delay(500);
    radar.read();//Initial read to check connection, como sea esto me arranco la cabeza
    Serial.print(F("\nConnect LD2410 radar TX to GPIO:"));
    Serial.println(RADAR_RX_PIN);
    Serial.print(F("Connect LD2410 radar RX to GPIO:"));
    Serial.println(RADAR_TX_PIN);
    Serial.print(F("LD2410 radar sensor initialising: "));
    if (radar.isConnected()) {
        sensores.ld2410_ok = true;
        indicarEstado(NORMAL);
        Serial.println(F("OK"));
        Serial.print(F("LD2410 firmware version: "));
        Serial.print(radar.firmware_major_version);
        Serial.print('.');
        Serial.print(radar.firmware_minor_version);
        Serial.print('.');
        Serial.println(radar.firmware_bugfix_version, HEX);
        return true;
    } else {
        Serial.println(F("not connected"));
        indicarEstado(ERROR);
        sensores.ld2410_ok = false;
        return false;
    }
}


RadarData leerRadar() {
  RadarData datos = {false, -1, -1, false};
  
  // Verificar si el sensor está disponible
  if(!sensores.ld2410_ok) {
    Serial.println("⚠️ LD2410 no disponible, reintentando...");
    if(!setupRadar()) {
      return datos;
    }
  }
  
  // Leer radar
  radar.read();
  
  // Verificar conexión
  if(!radar.isConnected()) {
    Serial.println("❌ LD2410 perdió conexión");
    sensores.ld2410_ok = false;
    return datos;
  }
  
  // Obtener datos
  datos.presenciaDetectada = radar.presenceDetected();
  
  if(datos.presenciaDetectada) {
    if(radar.stationaryTargetDetected()) {
      datos.distanciaEstatico = radar.stationaryTargetDistance();
      
      // Validar distancia (0 a 600 cm según LD2410)
      if(datos.distanciaEstatico < 0 || datos.distanciaEstatico > 600) {
        Serial.printf("❌ Distancia estático inválida: %d cm\n", datos.distanciaEstatico);
        datos.distanciaEstatico = -1;
      }
    }
    
    if(radar.movingTargetDetected()) {
      datos.distanciaMovil = radar.movingTargetDistance();
      
      // Validar distancia
      if(datos.distanciaMovil < 0 || datos.distanciaMovil > 600) {
        Serial.printf("❌ Distancia móvil inválida: %d cm\n", datos.distanciaMovil);
        datos.distanciaMovil = -1;
      }
    }
  }
  
  // Todo OK
  datos.valido = true;
  return datos;
}