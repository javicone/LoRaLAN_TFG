# Database — base de datos MySQL

## Qué hace

Guarda los usuarios, las aulas, las clases programadas, los fichajes (asistencias) y
los dispositivos LoRaLAN. Solo la usa el servidor (`Server/`); ni el gateway ni los
nodos se conectan a ella.

## Ficheros

| Fichero | Cuándo usarlo |
|---|---|
| `esquema.sql` | Instalación desde cero. Crea las 5 tablas vacías |
| `migracion_loralan.sql` | BD que ya existía (la del proyecto `ControlAsistencia` con WiFi). La adapta a LoRaLAN |
| `backups/` | Volcados de la BD real. Tienen datos personales: el `.gitignore` impide subirlos |

Ambos scripts funcionan en MySQL 8+ y MariaDB 10.5+.

## Tablas

```
usuarios ──< clases >── aulas ──< nodos
    │           │         │
    └────< asistencias >──┘
```

| Tabla | Qué guarda | Detalles |
|---|---|---|
| `usuarios` | Profesores, conserjes y administradores | `correo` único (es el login), `password` = hash bcrypt, `uid_tarjeta` = UID NFC en hex mayúsculas (único, puede ser NULL), `rol` ∈ profesor/conserje/administrador |
| `aulas` | Aulas | `nombre` único |
| `clases` | Horario: qué profesor da qué asignatura, en qué aula y cuándo | Índice `(aula_id, hora_inicio, hora_fin)` para buscar "la clase de ahora" |
| `asistencias` | Cada fichaje | `usuario_id`, `aula_id`, `clase_id` (NULL si se borra la clase), `fecha_hora` |
| `nodos` | Nodos de aula (`node-NNN`) y gateways (`gw-rpi-01`) | `api_key` solo la tienen los gateways; `id_aula` la asigna el administrador; `relay1_state`/`relay2_state` |

Decisiones de diseño:

- **Deduplicación de fichajes**: el nodo reenvía un fichaje si no le llega respuesta a
  tiempo, aunque el servidor ya lo hubiera guardado. El índice
  `(usuario_id, aula_id, clase_id)` permite comprobar rápido si ya existe, y el servidor
  no lo inserta dos veces.
- **Nodos sin api_key**: un nodo LoRaLAN no habla con el servidor; lo da de alta el
  servidor la primera vez que el gateway sube algo suyo.
- **`ON DELETE SET NULL`** en `nodos.id_aula` y `asistencias.clase_id`: borrar un aula o
  una clase no está bloqueado por los nodos ni borra el histórico de fichajes.

## Cómo está hecha la migración

`migracion_loralan.sql` es **repetible** (se puede ejecutar varias veces sin romper
nada) sin usar `ADD COLUMN IF NOT EXISTS`, que solo existe en MariaDB. Cada cambio:

1. Consulta `information_schema` para ver si hace falta.
2. Construye la sentencia en una variable (`SET @sql := IF(..., 'ALTER ...', 'DO 0')`).
3. La ejecuta con `PREPARE` / `EXECUTE`.

Cambios que aplica:

- Crea `nodos` si no existe y le añade `id_aula`.
- `nodos.api_key` pasa a opcional.
- Sustituye la FK antigua de `nodos.id_aula` por `fk_nodos_aula` con `ON DELETE SET NULL`.
- Añade `asistencias.clase_id`, su FK y el índice de deduplicación.
- `usuarios.uid_tarjeta` pasa a opcional (conserjes y admins no tienen por qué tener tarjeta).
- Da de alta el gateway `gw-rpi-01` con una api_key de ejemplo: **cambiarla** y poner la
  misma en `LORALAN_API_KEY` del gateway.

## Uso

```bash
# Nueva
mysql -u root -p control-asistencia < esquema.sql
mysql -u root -p control-asistencia < migracion_loralan.sql   # alta del gateway

# Existente
mysql -u root -p control-asistencia < migracion_loralan.sql
```

No usar `ControlAsistencia/Database/Esquemas_bbdd.sql` ni restaurar
`backupControlAsistencia.sql`: no coinciden con lo que espera el código.
