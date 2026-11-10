# Database

Base de datos MySQL del sistema: usuarios, aulas, clases, asistencias y dispositivos. Solo
la usa el servidor; ni el gateway ni los nodos se conectan a ella.

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

```bash
# Desde cero
mysql -u root -p control-asistencia < esquema.sql
mysql -u root -p control-asistencia < migracion_loralan.sql

# BD existente
mysql -u root -p control-asistencia < migracion_loralan.sql
```

La migración crea el gateway `gw-rpi-01` con una api_key de ejemplo: hay que cambiarla y
poner la misma en `LORALAN_API_KEY` del gateway.
