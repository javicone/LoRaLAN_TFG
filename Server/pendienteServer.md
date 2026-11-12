# El aula sigue al nodo físico (MAC), no a su dirección LoRaLAN

## Contexto
El fichaje de `63B32602` en `node-254` se rechaza con «Aula no configurada». El aula se había asignado a `node-016`, y el mismo ESP32 tiene ahora la dirección dinámica 254. Hoy el servidor liga el aula a la **dirección** (`nodos.device_id = "node-NNN"`), y esa dirección puede cambiar:
- al pasar de dirección fija a dinámica (lo que acaba de pasar);
- si se borra la NVS del nodo (reflasheo con borrado);
- si el gateway se reinicia (su tabla de direcciones vive solo en RAM) y un nodo nuevo se asocia antes que el antiguo: se queda la dirección libre más alta (0xFE) y el antiguo recibe otra. Las aulas quedan cruzadas sin que nadie se entere.

**Solución definitiva:** ligar el aula a la **MAC** del ESP32, que no cambia nunca. El gateway ya la conoce: la recibe en el JOIN, la guarda la biblioteca (`nodeHwId`) y `onNodeJoined` la imprime (`gatewayLoRaLAN/src/main.cpp:214`). Basta con enviarla al servidor en el aviso de alta que ya existe (`tipo:"estado"`). El servidor, al recibirla, mueve la fila del nodo físico a su dirección actual. Así el aula se asigna **una sola vez** y sigue al nodo en cualquier cambio de dirección.

No se toca la biblioteca LoRaLAN, ni el firmware del nodo, ni el resto del servidor: fichajes, órdenes, panel y asignación de aula siguen usando `device_id`. Lo que cambia es que `device_id` pasa a ser la dirección *actual* de una fila identificada por la MAC.

## Cambios (uno por paso, con visto bueno entre pasos)

### 1. Base de datos
- `Database/esquema.sql`, tabla `nodos`: `mac CHAR(17) NULL` con `UNIQUE KEY uq_nodos_mac (mac)`, y `device_id` pasa a `NULL` (un nodo físico apagado puede quedarse sin dirección; el UNIQUE admite varios NULL).
- Nuevo `Database/migracion_mac.sql` para la BD de la Pi, que ya existe: `ALTER TABLE nodos ADD COLUMN IF NOT EXISTS mac ...`, `ADD UNIQUE KEY IF NOT EXISTS ...` y `MODIFY device_id VARCHAR(50) NULL`. Se puede lanzar más de una vez sin efectos.
- `Database/README.md`: una línea para la nueva migración.

### 2. Servidor
- `Server/repositories/nodos.py`: nueva `vincular_mac(device_id, mac)`, con el patrón de `asignar_aula_a_nodo` (`nodos.py:94`, usa `execute_write`/`fetch_one`):
  1. `UPDATE nodos SET device_id=NULL WHERE device_id=%s AND mac IS NOT NULL AND mac<>%s` → la dirección deja de ser de otro nodo físico.
  2. Si ya hay fila con esa MAC: `DELETE FROM nodos WHERE device_id=%s AND mac IS NULL` (fila huérfana creada por un uplink) y `UPDATE nodos SET device_id=%s WHERE mac=%s`. El aula de la fila se conserva.
  3. Si no hay fila: `UPDATE nodos SET mac=%s WHERE device_id=%s AND mac IS NULL` (adopta la fila antigua y su aula, el caso de la migración). Si no afecta a ninguna fila, `INSERT INTO nodos (device_id, mac)`.
- `Server/routers/gateway.py`:
  - `UplinkIn`: `mac: Optional[str] = Field(None, pattern=r"^([0-9A-F]{2}:){5}[0-9A-F]{2}$")`.
  - Rama `estado` (`gateway.py:144`): si `u.online` y `u.mac` → `await vincular_mac(device_id, u.mac)`, se guarda `estado["mac"]` y se vacía la caché de aulas en memoria (`for e in gw.aulas_estado.values(): e.pop("aula", None)`). Se recarga sola desde la BD en el siguiente uplink (`gateway.py:148`) o fichaje (`asistenciaRepository.py:31`). Las altas son raras, así que vaciarla entera no cuesta nada. También se evita el 409 de `verificar_nombre_aula_disponible` por entradas viejas como `node-016`.
  - Docstring del contrato: añadir `"mac"` al aviso de estado.
- `Server/scripts/test_gateway_panel.py`: dos pruebas con el `monkeypatch` que ya se usa:
  - un estado con MAC llama a `vincular_mac("node-254", "AA:BB:...")` y vacía la caché;
  - una MAC mal formada da 422.

  El SQL de `vincular_mac` no tiene prueba local porque en el PC no hay BD; se valida en la Pi (paso 5).

Es compatible con el gateway actual: sin `mac`, todo se comporta como hoy.

### 3. Gateway (Pi)
- `gatewayLoRaLAN/src/bridge.h/.cpp`: `estadoJson(gateway, node, online, const std::string& mac = "")` añade `,"mac":"AA:BB:CC:DD:EE:FF"` si `mac` no está vacía.
- `gatewayLoRaLAN/src/main.cpp`, `onNodeJoined`: formatea la MAC que ya obtiene (líneas 216-220) y la pasa a `estadoJson`. No la envía si la MAC sale toda a cero.
- `gatewayLoRaLAN/test_bridge.cpp`: un `assert` del JSON con MAC. El existente, sin MAC, sigue valiendo.
- `gatewayLoRaLAN/README.md` y `src/bridge.h`: contrato actualizado.

### 4. Memoria y documentación
- `documentacion/tfg version definitiva/tfg/capitulos/capitulo7.tex` (descripción de la tabla `nodos`, la frase «Los nodos se registran automáticamente…»): el nodo se identifica por su MAC y el aula se asigna una vez. Revisar el capítulo 5 y los anexos por si dicen que el servidor no conoce la MAC.
- Actualizar `loralan-direccionamiento-dinamico.md` en la memoria de Claude: se rompe la decisión «el servidor y node-NNN no se tocan».

### 5. Despliegue en la Pi
1. `sudo mysql control-asistencia < Database/migracion_mac.sql`
2. Llevar el código a la Pi (commit + `git pull`, solo si me lo pides) y reiniciar el servidor.
3. Recompilar el gateway (`controlAsistenciaMigrado/README.md`) y reiniciarlo.
4. Reiniciar el nodo para que haga JOIN; en la BD, `node-254` tendrá la MAC.
5. Asignar el aula a `node-254` desde el panel del **administrador** (el conserje no puede: `panel.py:98`). Es la última vez que hay que hacerlo.

## Verificación
- PC: `python -m pytest scripts/test_gateway_panel.py -q` y `test_bridge` (orden de CLAUDE.md) en verde.
- Pi:
  - tras el JOIN, `SELECT device_id, mac, id_aula FROM nodos;` muestra `node-254` con MAC y aula;
  - pasar la tarjeta → ya no sale «Aula no configurada»: sale OK, o SIN_CLASE si no hay clase a esa hora (eso es otro tema);
  - prueba de fuego: `UPDATE nodos SET device_id='node-200' WHERE mac='…'`, reiniciar el nodo → la fila vuelve a `node-254` con su aula.
- El estado se informará como «verificado en PC (tests)» y «verificado en hardware» por separado.
