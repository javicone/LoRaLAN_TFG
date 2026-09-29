# Server — backend FastAPI y paneles web

## Qué hace

Es el cerebro del sistema. Un servidor web en Python (FastAPI) que:

- **Recibe del gateway** los fichajes, la telemetría de los nodos y su latido, y le
  entrega las órdenes pendientes. Todo por HTTP: el gateway pregunta, el servidor responde.
- **Decide cada fichaje**: busca al profesor por el UID de la tarjeta, el aula por el
  nodo y la clase que hay en ese momento, y devuelve un código (OK, sin clase,
  denegado, desconocido, reintentar).
- **Sirve las páginas web** para cada rol:

| Rol | Página | Qué puede hacer |
|---|---|---|
| Profesor | `/profesor/asistencias` | Ver sus fichajes y estadísticas del mes |
| Conserje | `/conserje` | Ver el estado de las aulas y encender/apagar relés |
| Administrador | `/adminPanel` | Lo anterior + asignar aula a cada nodo, reiniciar nodos, ver la radio y los gateways |
| Administrador | `/gestion-datos` | CRUD de usuarios, aulas, clases y asistencias |

No hay WebSocket en ningún sitio: los paneles consultan `GET /panel/estado` cada pocos
segundos.

## Estructura

Arquitectura en capas: **router** (HTTP) → **service** (lógica) → **repository** (SQL).

| Carpeta / fichero | Qué contiene |
|---|---|
| `appFastApi.py` | Punto de entrada: crea la app, logging, rate limiter, CORS, manejadores de error, estáticos y routers. Arranca uvicorn en el puerto 8000 |
| `config.py` | Lee `.env`: conexión a la BD, `SECRET_KEY`, caducidad del token (1440 min) |
| `database.py` | `DB_CONFIG` (lo reexporta para el pool) y conexión síncrona antigua |
| `utils/database_pool.py` | Pool de conexiones `aiomysql` (1–10), creado una sola vez con un lock |
| `routers/` | Endpoints HTTP (ver tabla abajo) |
| `services/` | Lógica: fichaje, autenticación, estado de la red LoRaLAN, usuarios, aulas |
| `repositories/` | Consultas SQL a cada tabla y cliente del webservice de horarios |
| `models/` | Modelos Pydantic de entrada/salida |
| `security/` | Hash bcrypt de contraseñas, sanitización de entradas, JWT RS256 (heredado) |
| `middleware/` | CORS, respuesta de rate limit y un middleware de validación de entradas |
| `templates/` | HTML (Jinja2): login, paneles y página de error |
| `static/` | CSS y JS de los paneles |
| `scripts/` | Utilidades (crear usuario, migrar contraseñas) y pruebas |
| `keys/` | Claves JWT RS256 y certificados HTTPS renombrados a `.bak` (el gateway no habla TLS) |
| `*.md` en la raíz | Revisiones y notas de mejoras anteriores (bcrypt, rate limit, inyecciones) |

### Routers

| Router | Rutas | Para quién |
|---|---|---|
| `gateway.py` | `POST /gateway/fichaje`, `POST /gateway/uplink`, `GET /gateway/commands`, `POST /gateway/status` | El gateway (token de dispositivo) |
| `panel.py` | `GET /panel/estado`, `POST /panel/rele`, `POST /panel/reset`, `POST /panel/aula` | Paneles de conserje y admin (cookie de sesión) |
| `auth.py` | `POST /auth/device`, `POST /auth/login`, `GET /logout` | Gateway y usuarios |
| `views.py` | `/`, `/adminPanel`, `/profesor/asistencias`, `/conserje`, `/gestion-datos` | Navegador (HTML) |
| `asistencias.py` | `GET /asistencias/mis-asistencias`, `POST /asistencias/registro` | Profesor / heredado |
| `gestion_crud.py` | `/aulas-crud`, `/clases-crud`, `/asistencias-crud` | Administrador |
| `usuarios.py` | `/usuarios/` | Administrador |
| `clases.py` | `/clases/actual`, `/clases/test` | Consulta del webservice de horarios |

## Cómo funciona

### 1. Autenticación

Hay dos tipos de token, los dos JWT HS256 firmados con `SECRET_KEY`
(`services/auth_service.py`):

- **Usuarios**: `POST /auth/login` (formulario correo + contraseña, máx. 5/min por IP)
  comprueba la contraseña con bcrypt, crea un token con `id`, `rol`, `correo` y
  `nombre`, lo guarda en una **cookie HttpOnly** `access_token` y redirige a la página
  de su rol. Las páginas y el panel leen esa cookie (`verificar_token_web`).
- **Gateway**: `POST /auth/device` con `device_id` + `api_key` (máx. 10/min). Si existe en
  la tabla `nodos`, devuelve un token con `rol: "device"`. En `routers/gateway.py`,
  `gateway_autenticado()` exige ese rol, impide que un `node-NNN` se haga pasar por
  gateway, y cada endpoint comprueba que el nombre de gateway del cuerpo coincide con
  el del token.

### 2. Fichaje (`POST /gateway/fichaje` → `services/asistencia_service.py`)

`registrar_asistencia(uid, device_id)`:

1. Profesor por UID (`usuarios.uid_tarjeta`). No existe → 404.
2. Aula del nodo (`nodos.id_aula`). Sin aula → 404.
3. Clase de ese aula en este momento en la tabla `clases`. Si no hay, consulta el
   **webservice de reservas de la UEx** y, si devuelve una clase, la guarda en `clases`.
4. Sin clase → 400. Clase de otro profesor → 403.
5. Si ya hay asistencia de ese profesor en esa clase y aula, no inserta otra
   (deduplicación del reenvío del nodo). Si no, inserta.

El router traduce el resultado a un código del ACK (`services/gateway_service.py`):

| Resultado | Código |
|---|---|
| Registrado | 0 OK |
| 400 | 1 SIN_CLASE |
| 403 | 2 DENEGADO |
| 404 | 3 DESCONOCIDO |
| Cualquier otro error (BD caída…) | 4 REINTENTAR |

Siempre responde HTTP 200 con `{"code": n}`: el resultado va en el código, no en el
estado HTTP. `aula_de_nodo()` no se traga los errores de BD a propósito: un fallo de BD
debe acabar en REINTENTAR, no en DESCONOCIDO (que el nodo no reintentaría).

### 3. Estado de la red (en memoria)

`services/gateway_service.py` sustituye al antiguo gestor de WebSocket. Todo vive en
diccionarios de Python:

- `aulas_estado[device_id]`: últimos sensores, relés, RSSI/SNR, gateway que lo atiende,
  aula y hora de la última noticia. Lo actualiza `marcar_visto()` con cada uplink.
- Un nodo está **conectado** si el gateway subió algo suyo en los últimos 120 s y no se
  dio de baja.
- `_comandos[gateway]`: cola de órdenes pendientes. El panel encola (`encolar_comando`)
  y el gateway recoge hasta 16 por consulta (`recoger_comandos`).
- `gateways[nombre]`: último latido (nodos, PUSH, POLL, descartes, % de ciclo de trabajo).
  Avisa en el log si el ciclo pasa del 80 % o hay descartes.

La primera vez que llega algo de un nodo, `uplink` lo da de alta en la tabla `nodos`
(`node-NNN`, sin aula) y carga el nombre de su aula si la tiene.

Consecuencia de ir en memoria: **reiniciar el servidor pierde** el estado y las órdenes
que el gateway aún no había recogido. El estado se recupera solo con los siguientes
uplinks (el nodo reenvía los relés cada 6 sondeos).

### 4. Paneles (`routers/panel.py` + `static/js/`)

- `GET /panel/estado`: lista de aulas con `conectado` y `hace_s`. El conserje no ve los
  datos de radio (`rssi`, `snr`, `gateway`); el administrador sí, y además los gateways.
- `POST /panel/rele` (conserje y admin), `POST /panel/reset` (admin): encolan la orden y
  responden 202. No es inmediato: el gateway la recoge en su siguiente consulta (≤ 5 s)
  y el panel ve el cambio cuando el nodo confirma el nuevo estado de los relés.
- `POST /panel/aula` (admin): asigna un aula al nodo en BD (la crea si no existe) y
  rechaza con 409 un nombre que ya use otro nodo.

`administrador.js` y `conserjes.js` llaman a `/panel/estado` con `setInterval` y pintan
las tarjetas de cada aula; `gestion_datos.js` hace el CRUD contra las rutas `*-crud`.

### 5. Errores

`appFastApi.py` responde con `templates/error.html` si la petición viene de un navegador
(`Accept: text/html`) y con JSON `{"detail": ...}` si viene de `fetch` o del gateway.
Los mensajes propios (p. ej. "Correo o contraseña incorrectos") se muestran; los 5xx se
sustituyen por un mensaje genérico para no filtrar detalles internos.

## Cosas a tener en cuenta (estado actual del código)

- `authenticate_user` **todavía acepta contraseñas guardadas en claro** (compatibilidad
  temporal, con aviso en el log). Ejecutar `scripts/migrate_passwords.py` para pasarlas
  a bcrypt.
- El webservice de horarios tiene timeout de 10 s, pero el gateway solo espera 800 ms
  por el fichaje. Si una clase no está en la BD y hay que preguntar al webservice, el
  primer intento acabará casi seguro en REINTENTAR; el reenvío ya la encontrará en BD.
- `POST /asistencias/registro` (heredado del sistema WiFi) no pide autenticación.
- `middleware/input_validation.py` y `security/jwt.py` (RS256) existen pero no se usan en
  el flujo actual: el middleware no está registrado en la app y los tokens son HS256.

## Ejecutar

```bash
python3 -m venv .venv && . .venv/bin/activate
pip install -r requirements.txt
cp .env.example .env        # y ajustar DB_HOST, DB_PASSWORD, SECRET_KEY
python appFastApi.py        # http://0.0.0.0:8000
```

En Windows: `$env:PYTHONUTF8=1; python appFastApi.py` (el log lleva emojis).

Pruebas sin MySQL:

```bash
python -m pytest scripts/test_gateway_panel.py scripts/test_errores.py -q
```

`test_gateway_panel.py` sustituye la BD por funciones falsas y prueba el contrato con el
gateway, la traducción de errores a códigos, la cola de comandos y los permisos.
