# NodoSimuladoLilyGo

Nodo de aula sin sensores ni lector NFC, para probar el sistema completo con una LilyGo T3
V1.6 (ESP32 + SX1276). Para el gateway y el servidor es igual que un nodo real: mismos
mensajes, misma radio y misma APP_KEY. Pide la dirección 3 (`node-003` en el servidor).

- **Sensores inventados**: valores que varían lentamente y presencia aleatoria.
- **Fichajes**: escribiendo un UID en hexadecimal en el monitor serie (p. ej. `04A1B2C3`),
  pulsando el botón BOOT o automáticamente cada 2 minutos (`AUTO_FICHAJE_MS`, `0` lo
  desactiva).
- **Relés simulados**: el relé 1 enciende el LED de la placa. También responde a `RESET`.

A diferencia del nodo real, solo admite un fichaje pendiente a la vez. Todo se imprime por
el monitor serie a 115200 baudios.

## Ficheros

| Fichero | Qué contiene |
|---|---|
| `NodoSimuladoLilyGo.ino` | Todo el programa |
| `AppProtocol.h` | Copia de `../Device_ESP32_LoRaLAN/AppProtocol.h`; si cambia el protocolo, hay que volver a copiarla |

## Uso

1. Poner en `UIDS_SIMULADOS` UIDs que existan en la BD si se quieren ver fichajes
   registrados.
2. Compilar con RadioLib y la biblioteca LoRaLAN del repo (placa ESP32 Dev Module o TTGO
   LoRa32).
3. Con el gateway en marcha, el nodo se asocia solo; después se le asigna un aula desde el
   panel de administrador.
