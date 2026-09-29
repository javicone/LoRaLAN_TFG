// Esta función convierte el array de bytes uid[] en un String hexadecimal
// (mayúsculas, sin separadores: el formato de usuarios.uid_tarjeta)
String uidToString(uint8_t *uidArray, size_t uidLength) {
  String uidString = "";
  for (size_t i = 0; i < uidLength; i++) {
    // Para cada byte, convertimos a hex y agregamos a la cadena
    if (uidArray[i] < 0x10) {
      uidString += "0";  // si es menor a 0x10, anteponer '0' para 2 dígitos
    }
    uidString += String(uidArray[i], HEX);
  }
  uidString.toUpperCase();
  return uidString;
}
