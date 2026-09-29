-- =============================================================
-- Esquema de Control de Asistencia sobre LoRaLAN (instalación nueva)
-- =============================================================
-- Es el esquema que usa el código de Server/, verificado contra sus consultas.
-- Sustituye a ControlAsistencia/Database/Esquemas_bbdd.sql, que no coincide
-- con el código (clases.id_aula, rol 'admin', sin profesor_id).
--
-- Válido en MySQL 8+ y MariaDB 10.5+. Sin datos.
--   mysql -u root -p control-asistencia < esquema.sql
--
-- Para una BD que ya existe, usar migracion_loralan.sql.
-- =============================================================

CREATE TABLE IF NOT EXISTS usuarios (
    id          INT NOT NULL AUTO_INCREMENT,
    nombre      VARCHAR(100) NOT NULL,
    correo      VARCHAR(100) NOT NULL,
    password    VARCHAR(255) NOT NULL DEFAULT '',        -- hash bcrypt
    uid_tarjeta VARCHAR(50) NULL,                        -- hex en mayúsculas; NULL = sin tarjeta
    rol         ENUM('profesor', 'conserje', 'administrador') NOT NULL,
    PRIMARY KEY (id),
    UNIQUE KEY uq_usuarios_correo (correo),              -- el login es por correo
    UNIQUE KEY uq_usuarios_uid (uid_tarjeta)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci;

CREATE TABLE IF NOT EXISTS aulas (
    id     INT NOT NULL AUTO_INCREMENT,
    nombre VARCHAR(50) NOT NULL,
    PRIMARY KEY (id),
    UNIQUE KEY uq_aulas_nombre (nombre)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci;

CREATE TABLE IF NOT EXISTS clases (
    id          INT NOT NULL AUTO_INCREMENT,
    aula_id     INT NOT NULL,
    profesor_id INT NOT NULL,
    asignatura  VARCHAR(100) DEFAULT '',
    hora_inicio DATETIME NOT NULL,
    hora_fin    DATETIME NOT NULL,
    PRIMARY KEY (id),
    KEY idx_clases_aula_horario (aula_id, hora_inicio, hora_fin),
    CONSTRAINT fk_clases_aula     FOREIGN KEY (aula_id)     REFERENCES aulas (id),
    CONSTRAINT fk_clases_profesor FOREIGN KEY (profesor_id) REFERENCES usuarios (id)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci;

CREATE TABLE IF NOT EXISTS asistencias (
    id         INT NOT NULL AUTO_INCREMENT,
    usuario_id INT NOT NULL,
    aula_id    INT NOT NULL,
    clase_id   INT NULL,
    fecha_hora TIMESTAMP NULL DEFAULT CURRENT_TIMESTAMP,
    PRIMARY KEY (id),
    -- Deduplicación del fichaje reenviado (NACK REINTENTAR): una vez por clase y aula
    KEY idx_asistencias_usuario_clase (usuario_id, aula_id, clase_id),
    KEY idx_asistencias_aula (aula_id),
    CONSTRAINT fk_asistencias_usuario FOREIGN KEY (usuario_id) REFERENCES usuarios (id),
    CONSTRAINT fk_asistencias_aula    FOREIGN KEY (aula_id)    REFERENCES aulas (id),
    CONSTRAINT fk_asistencias_clase   FOREIGN KEY (clase_id)   REFERENCES clases (id) ON DELETE SET NULL
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci;

-- Nodos LoRaLAN (node-NNN) y gateways. Un nodo se da de alta solo, sin
-- api_key, con su primera trama; el gateway necesita api_key para su token.
CREATE TABLE IF NOT EXISTS nodos (
    id           INT NOT NULL AUTO_INCREMENT,
    device_id    VARCHAR(50) NOT NULL,
    api_key      VARCHAR(255) NULL,
    id_aula      INT NULL,
    relay1_state BOOLEAN DEFAULT FALSE,
    relay2_state BOOLEAN DEFAULT FALSE,
    PRIMARY KEY (id),
    UNIQUE KEY uq_nodos_device (device_id),
    -- Borrar un aula deja sus nodos sin aula, en vez de impedir el borrado
    CONSTRAINT fk_nodos_aula FOREIGN KEY (id_aula) REFERENCES aulas (id) ON DELETE SET NULL
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci;
