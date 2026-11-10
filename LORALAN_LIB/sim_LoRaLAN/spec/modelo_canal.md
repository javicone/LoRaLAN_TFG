# Modelo de canal

Hipótesis declaradas del simulador. Toda cifra de rendimiento depende de ellas.

| Aspecto | Modelo | Origen o límite |
|---|---|---|
| Tiempo en el aire | Fórmula de Semtech por longitud (`radio::timeOnAirMs`), redondeada al ms superior. `fixedAirtimeMs` la sustituye por un valor medido | Sin medir en placas |
| CAD | Instantáneo. Una trama no es detectable hasta W ms después de empezar (`cadBlindMs`). Se detecta durante toda la trama | Casos funcionales: W = 0. Experimentos: barrido, como análisis de sensibilidad. La capacidad real del CAD está sin medir |
| Pérdida | Independiente por receptor y por enlace (`lossPct`, `setLoss`) | Sin ráfagas |
| Trama perdida | Ocupa el aire igual que las demás | |
| Half-duplex | El emisor no ejecuta `process()` hasta acabar su emisión, y dos emisiones seguidas de un dispositivo salen una tras otra | Como `transmit()` bloqueante en la placa |
| Colisión | Cualquier solape entre radios distintas destruye ambas tramas para todos | Sin efecto captura ni terminales ocultos |
| Reloj | Único y sin deriva | |
| Radio | `transmit()` y CAD nunca devuelven error | |
| Recepción | El canal llama a `handleReceived()`; no se ejercita la lectura de la radio real | |
| Cifrado y MIC | Sustitutos de AES y CMAC dependientes de la clave, sin valor criptográfico | Se validan con mbedTLS y vectores publicados, fuera del simulador |

Una trama que acaba en el milisegundo t ya no ocupa el canal a partir de t.
