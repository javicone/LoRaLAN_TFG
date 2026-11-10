# Diseño de las pruebas de LoRaLAN

**Estado (2026-11-01):** SIM1–SIM5, F1–F6 y los 76 casos de protocolo están escritos y ejecutados en simulador; el resultado está en `results/casos.csv`. Los experimentos tienen una ejecución preliminar en `results/perf`; las mutaciones se han descartado. Las decisiones 1 a 6 del apartado 7 se han programado con la propuesta de la tabla, a la espera de que Javier las confirme.

---

## 1. La idea en un minuto

Una prueba es un pequeño teatro con tres actos:

1. **Se monta la escena:** por ejemplo, un gateway y un nodo ya asociados.
2. **Se provoca un problema concreto:** por ejemplo, «se pierde el primer ACK».
3. **Se comprueba qué ocurrió:** por ejemplo, «el nodo repitió el mensaje, pero la aplicación solo lo recibió una vez».

El simulador ejecuta la biblioteca real, pero controla el reloj y el aire. Por eso se puede provocar el mismo problema, en el mismo instante, todas las veces.

### Un ejemplo completo: la prueba U6

> **Pregunta:** si se pierde la confirmación de un evento, ¿la aplicación lo recibe dos veces?

```text
 Nodo                                   Gateway
  |---- PUSH_EVENT (1.ª vez) ------------->|  la aplicación recibe el evento
  |     X<-------- ACK --------------------|  el simulador borra este ACK
  |                                        |
  |  pasan 1200 ms sin ACK                 |
  |                                        |
  |---- PUSH_EVENT (repetido) ------------>|  lo reconoce: NO lo entrega otra vez
  |<------------- ACK ---------------------|
  |  la aplicación del nodo sabe que llegó |
```

| Parte de la prueba | En U6 |
|---|---|
| **Pregunta** | ¿Se entrega dos veces si se pierde el ACK? |
| **Requisito** | RF5: confirmar y retransmitir |
| **Escena** | Un nodo asociado y canal limpio |
| **Problema provocado** | Se borra el primer ACK de bajada |
| **Debe ocurrir** | 2 emisiones del evento, 1 sola entrega a la aplicación y 1 aviso de «entregado» al nodo |
| **Si no ocurre** | Fallida: se ha encontrado un defecto |

Todas las pruebas de los apartados 4 y 5 tienen esta misma forma, resumida en una fila.

---

## 2. Qué significa cada nombre

### 2.1 Identificadores de las pruebas

La letra dice qué parte del protocolo se prueba. Se conservan los de `documentacion/evaluaciondeLoRaLAN.md` para poder cruzar ambos documentos. Los números no indican importancia y tienen huecos porque se han excluido algunos casos (apartado 8).

| Prefijo | Viene de | Qué prueba |
|---|---|---|
| **SIM** | Simulador | Que el propio simulador funciona bien |
| **F** | Formato | Cómo se construye y se lee una trama. **Son T01–T06 del catálogo:** se renombran aquí para no confundir T01 con T1 |
| **J** | JOIN | Entrar en la red |
| **L** | LEAVE | Salir de la red |
| **P** | POLL | El gateway pregunta por turnos a los nodos |
| **U** | *Uplink* (subida) | Eventos que envía el nodo por iniciativa propia (PUSH). La P ya estaba ocupada |
| **D** | Diferido | El gateway confirma más tarde, con una respuesta de la aplicación |
| **C** | CMD | Órdenes del gateway a un nodo |
| **T** | TIME_SYNC | Difusión de la hora |
| **S** | Silencio | Qué hace el nodo si el gateway deja de hablar o se reinicia |
| **A** | Ataque | Tramas falsas, alteradas o repetidas |
| **EXP** | Experimento | Medición con muchas repeticiones, sin aprobado ni suspenso |
| **VAL** | Validación | Comparar el simulador con una fórmula conocida |

### 2.2 Requisitos e invariantes

| Sigla | Significado | Ejemplo |
|---|---|---|
| **RF** | Requisito funcional: qué debe hacer el protocolo (capítulo 3) | RF5: confirmar y retransmitir |
| **RNF** | Requisito no funcional: con qué condiciones debe hacerlo | RNF2: carga de 64 bytes como máximo |
| **I** | Invariante: algo malo que no debe ocurrir **nunca** | I3: una orden no se ejecuta dos veces |
| **V** | Vivacidad: algo bueno que **acaba** ocurriendo | V2: todo evento termina, entregado o no |
| **R** | Recurso | R1: no superar el ciclo de trabajo |

Requisitos que se citan:

| Id | En pocas palabras | Id | En pocas palabras |
|---|---|---|---|
| RF1 | Un gateway y muchos nodos | RF9 | Baja ordenada |
| RF2 | Entrada autenticada | RF10 | Datos cifrados |
| RF3 | Sondeo y detección de inactivos | RF11 | Rechazar lo no auténtico |
| RF4 | Eventos, escuchando antes de hablar | RF12 | Rechazar repeticiones |
| RF5 | Confirmar y retransmitir un número limitado de veces | RNF1 | Rapidez |
| RF6 | Respuesta de la aplicación dentro del ACK | RNF2 | Carga de 64 B como máximo |
| RF7 | Órdenes del gateway a un nodo | RNF3 | Ciclo de trabajo legal |
| RF8 | Hora difundida periódicamente | RNF5 | Memoria fija, 32 nodos |

### 2.3 Términos del método

| Término | Significado sencillo |
|---|---|
| **Caso** | Una prueba concreta, como U6 |
| **Familia** | Grupo de casos del mismo tema, como PUSH |
| **Propiedad** | Una afirmación que se comprueba. Un caso puede tener varias |
| **Precondición** | La escena necesaria antes de empezar |
| **Estímulo** | El problema que se provoca |
| **Esperado** | Lo que debe ocurrir. Se escribe y se fecha **antes** de ejecutar, para no dar por bueno lo que el código ya hace |
| **Traza** | Registro de todo lo ocurrido en el caso. Las comprobaciones se hacen sobre ella |
| **Superada** | Ocurrió el estímulo y se cumplió lo esperado |
| **Fallida** | Ocurrió el estímulo y no se cumplió lo esperado. **Es un resultado válido:** se ha encontrado un defecto |
| **No concluyente** | El estímulo no llegó a ocurrir, así que no se ha demostrado nada |
| **Semilla** | Número del que sale todo el azar. Misma semilla, misma ejecución |
| **Réplica** | Repetir un experimento con otra semilla |

### 2.4 Términos del protocolo

| Término | Significado sencillo |
|---|---|
| **Gateway** | El coordinador de la red. **Nodo:** cada dispositivo que se conecta a él |
| **Trama** | Un mensaje enviado por radio |
| **Subida / bajada** | Del nodo al gateway / del gateway al nodo |
| **Tiempo en el aire** (*airtime*) | Lo que tarda una trama en transmitirse |
| **Colisión** | Dos emisiones a la vez que se destruyen entre sí |
| **CAD** | El nodo escucha un instante para saber si el canal está libre |
| **Backoff** | Esperar un rato antes de volver a intentarlo. **Exponencial:** cada vez más |
| **Jitter** | Un pequeño tiempo al azar para que dos nodos no repitan a la vez |
| **Ventana ciega (W)** | Hipótesis del simulador: los primeros W ms de una trama aún no se detectan con el CAD |
| **ACK** | «Recibido». **NACK:** no se usa en estas pruebas |
| **Reintento** | Volver a emitir por no recibir ACK. Por defecto: 3 emisiones en total, separadas 1200 ms |
| **JOIN_REQUEST / JOIN_ACCEPT** | «Quiero entrar» / «entra, con estos datos» |
| **DevNonce** | Número distinto en cada intento de JOIN, para detectar repeticiones |
| **appKey** | Clave maestra que comparten el gateway y los nodos |
| **Clave de sesión** | Clave propia de cada nodo, obtenida en el JOIN |
| **MIC** | Firma de la trama. Si no cuadra con la clave, se descarta |
| **netId** | Identificador de la red |
| **seq** | Número de secuencia de cada trama; sirve para detectar repeticiones |
| **Repetición** | Un atacante graba una trama y la vuelve a emitir |
| **PUSH_EVENT** | Evento que el nodo envía sin esperar turno; en el prototipo, un fichaje |
| **POLL / POLL_RESPONSE** | «¿Tienes algo?» / «sí, esto» o «no» |
| **Carga** (*payload*) | Los datos de aplicación dentro de la trama |
| **`delivered`** | Aviso a la aplicación del nodo: `true` si llegó el ACK, `false` si se agotaron los intentos |
| **Rescate** (*fallback*) | Si un PUSH agota sus intentos, se guarda para enviarlo en el siguiente POLL |
| **CMD** | Orden cifrada del gateway a un nodo |
| **TIME_SYNC** | Trama con la hora del gateway |
| **Round-robin** | Turnos en orden, uno tras otro |
| **`missedPolls`** | POLL seguidos sin respuesta del nodo; a los 3 pasa a inactivo |

---

## 3. Cómo leer las tablas

- **Qué se hace:** escena y problema provocado.
- **Debe ocurrir:** lo esperado.
- **Req.:** requisito que justifica lo esperado.
- **Señal:**
  - ✔ ya escrita y ejecutada;
  - 🐞 un documento anterior ya señala un posible defecto, así que puede salir Fallida;
  - ❓ tu decisión está pendiente (apartado 7);
  - ⚠ requisito por aclarar.

Si no se indica otra cosa, la escena es: un nodo asociado, canal limpio, sin sondeo automático.

---

## 4. Las pruebas

### 4.1 SIM: ¿el simulador es fiable? (5)

Si el instrumento mide mal, nada de lo demás vale.

| Id | Qué se hace | Debe ocurrir | Señal |
|---|---|---|---|
| SIM1 | Ejecutar dos veces con la misma semilla, y una con otra | Misma semilla: traza idéntica. Otra semilla: traza distinta | ✔ |
| SIM2 | Crear un mundo después de otro | El nuevo no hereda reloj, eventos ni sesiones | ✔ |
| SIM3 | Pedir que se borre un ACK que nunca aparece | Veredicto No concluyente, no Superada | ✔ |
| SIM4 | Usar cada tipo de fallo: borrar, retrasar, duplicar, reordenar, corromper | Cada uno hace exactamente lo que dice | ✔ |
| SIM5 | Comprobar el canal: tiempo en el aire, ventana ciega, pérdida y colisión | Coinciden con `modelo_canal.md` | ✔ |

### 4.2 F: ¿las tramas se construyen bien? (6)

Sin radio ni red: se llama directamente a las funciones que construyen y leen las tramas.

| Id | Catálogo | Qué se hace | Debe ocurrir | Req. |
|---|---|---|---|---|
| F1 | T01 | Construir una trama de cada tipo y volver a leerla | Sale lo mismo que entró | RF11 |
| F2 | T02 | Probar longitudes de 10, 11, 75 y 76 B | Solo se aceptan de 11 a 75 B | RNF2 |
| F3 | T03 | Avanzar seq hasta 0xFFFF y pasar a 0 | Sigue funcionando la detección de repeticiones | RF12 |
| F4 | T04 | Cambiar cada byte de una trama, de uno en uno | El MIC deja de cuadrar en todos los casos | RF11 |
| F5 | T05 | Usar versión, tipo, prioridad o flags inválidos | Rechazada, sin caída | RF11 |
| F6 | T06 | Construir con carga de 0, 1, 64 y 65 B | Acepta hasta 64; rechaza 65 | RNF2 |

### 4.3 J: entrar en la red (13)

| Id | Qué se hace | Debe ocurrir | Req. | Señal |
|---|---|---|---|---|
| J1 | Asociarse con canal limpio | Asociado; un aviso en cada extremo | RF2 | ✔ |
| J2 | Se pierde el JOIN_REQUEST | Lo repite más tarde con otro DevNonce | RF2 | |
| J3 | Se pierde el JOIN_ACCEPT | Repite; el gateway reutiliza entrada y dirección; ambos acaban con la misma clave | RF2 | |
| J4 | Canal ocupado al querer entrar | No transmite; reintenta después | RF4 | |
| J5 | Varios nodos entran a la vez | Todos asociados, cada uno con su dirección | RF1 | |
| J6 | El nodo se reinicia con sesión viva | Sesión nueva; el contador de repeticiones vuelve a empezar | RF2 | |
| J7 | El nodo tiene otra clave maestra | Nunca se asocia; el gateway no guarda nada | RF2 | |
| J8 | El nodo tiene otro netId | Se ignora | RF2 | |
| J9 | Entra el nodo 33 con la tabla llena | Rechazado; los otros 32 siguen igual | RNF5 | |
| J10 | Pide una dirección ocupada, o ninguna | Recibe otra libre del rango dinámico | RF2 | |
| J12 | Un atacante repite el último JOIN | Rechazado; la sesión sigue intacta | RF12 | |
| J13 | Un atacante repite un JOIN **antiguo** | Rechazado | RF12 | 🐞 |
| J15 | Encolar un evento antes de estar asociado | `queuePushEvent` devuelve false | RF2 | |

### 4.4 L: salir de la red (9)

| Id | Qué se hace | Debe ocurrir | Req. | Señal |
|---|---|---|---|---|
| L1 | Baja con canal limpio | ACK; clave borrada; un aviso; no vuelve a entrar solo | RF9 | |
| L2 | Se pierde el ACK de la baja | Repite; el gateway avisa una sola vez; acaba de baja | RF9 | |
| L3 | Se pierden todos los LEAVE | Baja local al agotar intentos; el gateway lo acaba viendo inactivo | RF9 | |
| L4 | Pedir la baja mientras espera otro ACK | No se pierde el aviso al gateway | RF9 | ❓ |
| L5 | Pedir la baja con un evento en cola | El evento se descarta avisando con `delivered=false` | RF5 | ❓ |
| L6 | Llega un POLL o CMD durante la baja | Ni responde ni ejecuta | RF9 | |
| L7 | Volver a entrar tras la baja | Sesión nueva, misma dirección | RF9 | |
| L9 | Un nodo de varios se da de baja | El sondeo lo salta; los demás no se ven afectados | RF9 | |
| L10 | Un atacante repite un LEAVE | Rechazado | RF12 | 🐞 |

### 4.5 P: preguntar por turnos (12)

| Id | Qué se hace | Debe ocurrir | Req. | Señal |
|---|---|---|---|---|
| P1 | POLL sin datos | Respuesta vacía, una vez, sin ACK | RF3 | |
| P2 | Respuesta generada al momento (bajo demanda) | Una emisión, sin ACK | RF3 | |
| P3 | Carga con ACK | Se confirma y se borra del nodo | RF3 | |
| P4 | Carga con ACK; se pierde el ACK | Se conserva y se repite después | RF5 | |
| P5 | Carga sin ACK | Una emisión y se borra | RF3 | |
| P6 | Encolar una carga con otra pendiente | `queuePollPayload` devuelve false; la primera no se pisa | RF5 | ❓ 🐞 |
| P7 | POLL dirigido a otro nodo | No responde | RF3 | |
| P8 | Se pierde el POLL | Sube `missedPolls`; el siguiente turno llega | RF3 | |
| P9 | Se pierde la POLL_RESPONSE | Sin ACK: dato perdido. Con ACK: se repite | RF5 | |
| P10 | Varios nodos | Turnos justos; cada nodo es preguntado cada N × intervalo | RF1 | |
| P11 | Un nodo deja de contestar y vuelve | Un aviso de inactivo; se sigue sondeando; se reactiva | RF3 | |
| P14 | Llega un POLL mientras el nodo espera un ACK | La respuesta no impide recibir ese ACK | RF5 | ❓ 🐞 |

### 4.6 U: eventos del nodo (20)

La duda clave aquí es qué pasa cuando un evento coincide con otra cosa.

| Id | Qué se hace | Debe ocurrir | Req. | Señal |
|---|---|---|---|---|
| U1 | Evento en reposo con canal libre | Escucha una vez, emite y llega en un tiempo en el aire | RF4 | ✔ |
| U2 | Evento sin ACK | Una emisión, sin ACK | RF4 | |
| U3 | Canal ocupado | Espera cada vez más; no se solapa | RF4 | |
| U4 | Canal ocupado para siempre | Límite de intentos y aviso a la aplicación | RF5 | |
| U5 | Se pierde la 1.ª emisión | Reintento; una sola entrega | RF5 | |
| U6 | Se pierde el ACK | Repite; una sola entrega | RF5 | ✔ |
| U7 | Se pierde todo, sin rescate | `delivered=false` | RF5 | |
| U8 | Se pierde todo, con rescate | Sale en el siguiente POLL | RF5 | |
| U9 | Rescate con otra carga ya encolada | No se pierde ninguna | RF5 | 🐞 |
| U10 | Dos eventos seguidos | El segundo devuelve false si el primero no ha salido | RF4 | |
| U11 | Evento mientras otro espera ACK | Espera y después sale | RF4 | |
| U12 | Evento justo cuando empieza un POLL | Si colisionan: un reintento y entrega | RF4 | |
| U13 | Evento mientras el nodo responde a un POLL | Espera a que el canal quede libre; sin colisión | RF4 | |
| U14 | Evento con una carga con ACK en curso | Espera, porque comparten el envío fiable | RF4 | |
| U15 | Hay que reintentar con el canal ocupado | El reintento escucha antes de transmitir | RF4 | ❓ 🐞 |
| U16 | Varios nodos colisionan (W > 0) | Dejan de coincidir y todos entregan | RF4 | 🐞 |
| U17 | El gateway se reinicia con un evento en curso | `delivered=false`; vuelve a entrar por silencio | RF5 | |
| U19 | Evento de 0, 1, 64 y 65 B | Acepta hasta 64; con 65 devuelve false | RNF2 | |
| U20 | Se pierden 63 y luego 64 tramas seguidas | Recupera la comunicación en ambos casos | RF5 | |
| U22 | Evento perdido y POLL antes del reintento (hallado al analizar EXP5) | Si el nodo recibe la confirmación, la aplicación recibió el evento | RF5 | |

### 4.7 D: respuesta de la aplicación dentro del ACK (5)

Escena: el gateway no confirma solo; su aplicación llama a `ackPush()` con la respuesta. En el prototipo, esa respuesta es la del servidor al fichaje.

| Id | Qué se hace | Debe ocurrir | Req. | Señal |
|---|---|---|---|---|
| D1 | La aplicación responde rápido | El código llega al nodo dentro del ACK | RF6 | |
| D2 | La aplicación responde tarde (más de 1200 ms) | El nodo repite; la aplicación no recibe duplicado | RF6 | |
| D3 | Se pierde el ACK con la respuesta | La repetición recibe la misma respuesta guardada | RF6 | |
| D4 | La aplicación no responde nunca | `delivered=false` | RF6 | |
| D6 | Llega un POLL durante la espera | El ACK no se pierde | RF6 | 🐞 |

### 4.8 C: órdenes al nodo (6)

| Id | Qué se hace | Debe ocurrir | Req. | Señal |
|---|---|---|---|---|
| C1 | Orden con canal limpio | Se descifra y se ejecuta una vez | RF7 | |
| C2 | Se pierde la orden | Reintento; se ejecuta una vez | RF5 | |
| C3 | Se pierde el ACK del nodo | **Se ejecuta una sola vez** | RF7 | |
| C4 | Orden a un nodo no asociado | `sendCommand` devuelve false | RF7 | |
| C5 | Orden con otra en espera | Devuelve false; la aplicación reintenta | RF7 | |
| C7 | Un atacante repite una orden capturada | Rechazada | RF12 | |

C3 importa porque ejecutar dos veces «abrir el relé» no siempre es inocuo. La simulación temporal hecha fuera del repositorio ya salió Fallida en este punto: el nodo ejecutó la orden 2 veces.

### 4.9 T: difusión de la hora (3)

| Id | Qué se hace | Debe ocurrir | Req. |
|---|---|---|---|
| T1 | Llega un TIME_SYNC | Cambia `timeOffsetMs` | RF8 |
| T2 | Solo llegan TIME_SYNC, sin otro tráfico | No cuentan como señal de vida; se detecta el silencio | RF8 |
| T-PER | Dejar correr el tiempo | Se emite solo, cada `timeSyncIntervalMs` | RF8 |

### 4.10 S: silencio y reinicios (3)

| Id | Qué se hace | Debe ocurrir | Req. | Señal |
|---|---|---|---|---|
| S1 | El gateway deja de hablar | El nodo vuelve a hacer JOIN | ⚠ | |
| S2 | Funcionamiento normal durante mucho tiempo | Ningún re-JOIN falso | ⚠ | |
| S4 | Se reinicia el gateway con 3 nodos | Los 3 vuelven a entrar con su dirección | ⚠ | |

⚠ Ningún requisito del capítulo 3 pide recuperarse de un silencio. Estas pruebas no tienen hoy requisito al que apuntar (decisión 7 del apartado 7).

### 4.11 A: ataques y tramas extrañas (5)

| Id | Qué se hace | Debe ocurrir | Req. | Señal |
|---|---|---|---|---|
| A1 | Repetir un PUSH capturado | Sin doble entrega; reenvía el ACK | RF12 | |
| A2 | Alterar carga o cabecera | Rechazada | RF11 | |
| A3 | Devolver al nodo su propio ACK | Rechazado | RF11 | 🐞 |
| A4 | Agotar los 65 536 valores del contador en una sesión | No se reutiliza el flujo de cifrado | RF10 | 🐞 |
| A5 | Tramas truncadas, longitudes extremas y tipos desconocidos | Sin caída ni cambio de estado | RF11 | |

**Límite de A4:** el cifrado del simulador es falso. Solo puede comprobar que el contador no se reutiliza, no la seguridad real.

---

## 5. Sin pruebas de mutación

Se descartaron el 2026-11-01 para simplificar el plan. Se pierde la prueba formal de que la batería detecta defectos puestos a propósito, y así debe declararse como limitación. Lo atenúa que la batería sí ha encontrado 17 casos con fallos y un defecto del propio simulador (F4).

C1 conserva la comprobación de que los bytes en el aire no contienen el texto claro.

---

## 6. Mediciones y lo que queda fuera

### 6.1 Experimentos (4)

No dan aprobado o suspenso: responden «¿cuánto?». Los definitivos se ejecutan **sobre la versión congelada**, con 30 semillas por nivel. Una latencia se da con mediana y percentiles; una tasa de entrega, con un intervalo de confianza. El programa es `experiments/perf_main.cpp`; el barrido, `experiments/sweep.py`; las figuras, `experiments/plot.py`.

| Id | Pregunta | Qué se varía | Qué se mide |
|---|---|---|---|
| VAL | ¿El simulador coincide con la teoría? | Pérdida solo en subida | Entrega y transmisiones frente a la fórmula |
| EXP1 | ¿Cómo empeora con pérdidas? | Pérdida de 0 a 50 %, en eventos y en respuestas al POLL | Entrega, latencia, falsos inactivos |
| EXP4 | ¿Colisionan los eventos entre sí? | Fichajes por minuto, N y ventana ciega de 0 a 5 ms, sin sondeo | Colisiones, entrega, latencia |
| EXP5 | ¿Cuánto estorba el sondeo a los eventos? | Lo mismo, con sondeo, e intervalo de sondeo | Colisiones con POLL, fichajes retrasados; se compara con la estimación por POLL: ~1,0 % con escucha ideal y ~1,9 % si la escucha no detecta un POLL ya comenzado |

EXP5 incluye una red de 254 nodos, que se compila con una copia de la biblioteca (`sim_perf254`) con `kMaxNodes = 254`; el diseño sigue en 32.

EXP4 y EXP5 son en parte un análisis de sensibilidad: nadie ha medido aún la ventana ciega real, así que no predicen el comportamiento en placas.

### 6.2 Lo que el simulador no puede demostrar

| Qué | Cómo se valida | Requisito |
|---|---|---|
| Que el cifrado y el MIC son correctos | Vectores publicados (RFC 4493, NIST SP 800-38A) con mbedTLS real | RF2, RF10, RF11 |
| Tiempo en el aire, CAD, colisiones, alcance y latencia real | Placas | RNF1, RNF3, RF4 |
| Coste | Lista de componentes | RNF4 |
| Sin memoria dinámica | Buscar en `src` | RNF5 |
| Portabilidad | Compilar en ESP32, Pi y PC | RNF6 |
| Sencillez de uso | Ejemplo mínimo de nodo y gateway | RNF7 |

---

## 7. Lo que tienes que decidir

Lo que el código hace hoy no decide lo correcto: si no lo cumple, sale Fallida. Marca cada una con tu decisión.

| # | Prueba | Situación | Propuesta | Alternativa |
|---|---|---|---|---|
| 1 | L4 | Baja mientras espera otro ACK. Hoy se da de baja sin avisar | `leave()` devuelve false y sigue asociado | Aplazar la baja hasta terminar |
| 2 | L5 | Baja con un evento en cola | Descartarlo avisando con `delivered=false` | Enviarlo antes de darse de baja |
| 3 | P6 | Carga nueva sobre otra pendiente. Hoy la pisa sin avisar | Devolver false y no pisar | Pisar y documentarlo |
| 4 | P14 | POLL mientras espera un ACK. Hoy responde | La respuesta no debe impedir recibir el ACK | No responder a ese POLL |
| 5 | U4 | Canal ocupado siempre. Hoy reintenta sin límite | Límite de intentos y aviso a la aplicación | Admitirlo como limitación |
| 6 | U15 | Reintento con canal ocupado. Hoy no escucha | El reintento escucha antes de emitir | Admitirlo como limitación |
| 7 | S1, S2, S4 | Sin requisito | Añadir un requisito de recuperación al capítulo 3 | Asignarlas a RF2 |
| 8 | Añadido | Comprobar I2, I3, I7 e I8 al final de **cada** caso | Sí: es una función y detecta fallos que el caso no buscaba | No |
| 9 | Añadido | Prueba al azar: 500 acciones por 30 semillas con esos invariantes | Hacerla tras los 75 casos; es lo primero que se recorta | No |
| 10 | EXP | Umbral de RNF1: «un evento llega en menos de X ms» | Lo fijas antes de medir | Sin él no hay veredicto |

---

## 8. Casos excluidos

Quedan como «no ejecutados» y con su motivo en el anexo. Motivo general: no son prioritarios para el plazo.

| Familia | Casos |
|---|---|
| JOIN | J11, J14 |
| LEAVE | L8 |
| POLL | P12 (lo cubre L9), P13, P15 |
| PUSH | U18, U21 (pasa a F3) |
| ACK diferido | D5, D7, D8 |
| CMD | C6 |
| TIME_SYNC | T3 |
| Silencio | S3, S5 |
| Seguridad | A6, A7 |

---

## 9. Recuento

| Bloque | Cantidad | Estado |
|---|---|---|
| Simulador (SIM) | 5 | Hechas |
| Formato (F) | 6 | Ejecutadas |
| Protocolo (J, L, P, U, D, C, T, S, A) | 76 | Ejecutadas: 59 casos sin fallos y 17 con alguna propiedad Fallida |
| Experimentos y validación | 6 | Escritos; ejecución preliminar en `results/perf`, a repetir sobre la versión congelada |
