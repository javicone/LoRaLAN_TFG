# Database

Base de datos del sistema: usuarios, aulas, clases, asistencias y dispositivos. Va en
MariaDB, en la Raspberry Pi, junto al servidor, que es el único que la usa.

## Ficheros

| Fichero | Cuándo usarlo |
|---|---|
| `esquema.sql` | Instalación desde cero |
| `migracion_loralan.sql` | Adaptar a LoRaLAN la BD del sistema anterior con WiFi; también da de alta el gateway |
| `backups/` | Volcados con datos personales; el `.gitignore` impide subirlos |

Funcionan en MySQL 8+ y MariaDB 10.5+. La migración se puede ejecutar varias veces sin
romper nada.

## Tablas

| Tabla | Qué guarda |
|---|---|
| `usuarios` | Profesores, conserjes y administradores: correo, contraseña (bcrypt), UID de la tarjeta y rol |
| `aulas` | Aulas |
| `clases` | Horario: profesor, asignatura, aula y hora |
| `asistencias` | Cada fichaje |
| `nodos` | Nodos de aula (`node-NNN`) y gateways (`gw-rpi-01`); el aula de cada nodo la asigna el administrador |

Un fichaje reenviado por el nodo no se guarda dos veces: el servidor comprueba antes si ya
existe para ese profesor, aula y clase.

## Uso

Instalación en la Pi (crear la BD y su usuario, cargar las tablas y cambiar la api_key del
gateway): [`../arranque.md`](../arranque.md), apartado 1. Lo esencial:

```bash
sudo mysql control-asistencia < esquema.sql
sudo mysql control-asistencia < migracion_loralan.sql
```

La migración crea el gateway `gw-rpi-01` con una api_key de ejemplo: hay que cambiarla y
poner la misma en `LORALAN_API_KEY` del gateway. El primer administrador se crea con
`Server/scripts/create_user.py`.
