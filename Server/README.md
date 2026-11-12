# Server

Servidor web en Python (FastAPI) del sistema de fichaje:

- **Atiende al gateway**: recibe fichajes, telemetría y latidos, y le entrega las órdenes
  pendientes. Todo por HTTP; no hay WebSocket.
- **Decide cada fichaje**: busca al profesor por el UID, el aula por el nodo y la clase de
  ese momento, y devuelve el código que el gateway envía al nodo.
- **Sirve los paneles web** de cada rol:

| Rol | Página | Qué puede hacer |
|---|---|---|
| Profesor | `/profesor/asistencias` | Ver sus fichajes |
| Conserje | `/conserje` | Ver el estado de las aulas y encender o apagar relés |
| Administrador | `/adminPanel` | Además, asignar aula a cada nodo, reiniciar nodos y ver la radio |
| Administrador | `/gestion-datos` | Gestionar usuarios, aulas, clases y asistencias |

## Estructura

Capas: **router** (HTTP) → **service** (lógica) → **repository** (SQL).

| Carpeta | Qué contiene |
|---|---|
| `appFastApi.py` | Punto de entrada (puerto 8000) |
| `config.py` | Configuración leída de `.env` |
| `routers/` | Endpoints; los del gateway en `gateway.py` y los de los paneles en `panel.py` |
| `services/` | Lógica: fichaje, autenticación y estado de la red |
| `repositories/` | Consultas SQL y cliente del webservice de horarios de la UEx |
| `models/`, `security/`, `middleware/`, `utils/` | Modelos, bcrypt y JWT, CORS y límites, pool de conexiones a la BD |
| `templates/`, `static/` | HTML, CSS y JS de los paneles |
| `scripts/` | Crear usuarios, migrar contraseñas y pruebas |

## Cómo funciona

**Autenticación**: los usuarios entran con correo y contraseña (bcrypt) y reciben un JWT en
una cookie; el gateway obtiene el suyo con `POST /auth/device` y su api_key de la tabla
`nodos`.

**Fichaje** (`POST /gateway/fichaje`): siempre responde `{"code": n}`.

| Resultado | Código |
|---|---|
| Asistencia registrada (o ya registrada antes) | 0 OK |
| (El servidor ya no lo devuelve; se conserva en el protocolo) | 1 SIN_CLASE |
| La clase es de otro profesor | 2 DENEGADO |
| Tarjeta desconocida o nodo sin aula | 3 DESCONOCIDO |
| Cualquier otro error (p. ej. BD caída) | 4 REINTENTAR |

Si la clase no está en la BD se consulta el webservice de horarios (como mucho 0,5 s, para
responder dentro del plazo del gateway) y se guarda. Si tampoco está ahí, el primer profesor
que ficha abre una «Clase sin programar» suya hasta la siguiente hora en punto; otro profesor
en esa hora recibe DENEGADO. Los paneles marcan «Clase en curso» mientras dura cualquier clase.

**Estado de la red**: se guarda en memoria. Un nodo está conectado si el gateway subió algo
suyo en los últimos 120 s, y aparece solo en la tabla `nodos` como `node-NNN` con su
primera trama. Las órdenes de los paneles quedan en cola hasta que el gateway las recoge
(cada 5 s). Reiniciar el servidor pierde el estado y las órdenes no recogidas.

**A tener en cuenta**: el login aún acepta contraseñas guardadas en claro;
`scripts/migrate_passwords.py` las pasa a bcrypt.

## Ejecutar

```bash
python3 -m venv .venv && . .venv/bin/activate
pip install -r requirements.txt
cp .env.example .env        # ajustar DB_HOST, DB_PASSWORD y SECRET_KEY
python appFastApi.py        # http://0.0.0.0:8000
```

En Windows: `$env:PYTHONUTF8=1; python appFastApi.py`. Debe ir sin TLS: el gateway no
habla HTTPS.

Pruebas sin base de datos:

```bash
python -m pytest scripts/test_gateway_panel.py scripts/test_errores.py -q
```
