-- =============================================================
-- MIGRACIÓN: Control de Asistencia sobre LoRaLAN (BD existente)
-- =============================================================
-- Lleva una BD creada con el backup de ControlAsistencia (o con
-- Esquemas_bbdd.sql) a lo que espera Server/. Para instalar desde cero,
-- usar esquema.sql.
--
-- Idempotente y válida en MySQL 8+ y MariaDB 10.5+: no usa
-- "ADD COLUMN IF NOT EXISTS" (solo MariaDB); cada cambio consulta antes
-- information_schema y se ejecuta con PREPARE solo si hace falta.
--   mysql -u root -p control-asistencia < migracion_loralan.sql
-- =============================================================

-- ---------- nodos ----------
CREATE TABLE IF NOT EXISTS nodos (
    id           INT NOT NULL AUTO_INCREMENT,
    device_id    VARCHAR(50) NOT NULL,
    api_key      VARCHAR(255) NULL,
    id_aula      INT NULL,
    relay1_state BOOLEAN DEFAULT FALSE,
    relay2_state BOOLEAN DEFAULT FALSE,
    PRIMARY KEY (id),
    UNIQUE KEY uq_nodos_device (device_id)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci;

-- id_aula, si la tabla venía de una versión sin ella
SET @hace_falta := (SELECT COUNT(*) = 0 FROM information_schema.COLUMNS
                    WHERE TABLE_SCHEMA = DATABASE() AND TABLE_NAME = 'nodos' AND COLUMN_NAME = 'id_aula');
SET @sql := IF(@hace_falta, 'ALTER TABLE nodos ADD COLUMN id_aula INT NULL', 'DO 0');
PREPARE st FROM @sql; EXECUTE st; DEALLOCATE PREPARE st;

-- Los nodos LoRaLAN no tienen api_key
ALTER TABLE nodos MODIFY COLUMN api_key VARCHAR(255) NULL;

-- FK de nodos.id_aula con ON DELETE SET NULL: borrar un aula deja sus nodos
-- sin aula. Si ya había otra FK sobre id_aula (Esquemas_bbdd.sql la creaba
-- sin ON DELETE, con nombre automático), se sustituye.
SET @fk_vieja := (SELECT k.CONSTRAINT_NAME
                  FROM information_schema.KEY_COLUMN_USAGE k
                  JOIN information_schema.REFERENTIAL_CONSTRAINTS r
                    ON r.CONSTRAINT_SCHEMA = k.CONSTRAINT_SCHEMA AND r.CONSTRAINT_NAME = k.CONSTRAINT_NAME
                  WHERE k.TABLE_SCHEMA = DATABASE() AND k.TABLE_NAME = 'nodos' AND k.COLUMN_NAME = 'id_aula'
                    AND NOT (k.CONSTRAINT_NAME = 'fk_nodos_aula' AND r.DELETE_RULE = 'SET NULL')
                  LIMIT 1);
SET @sql := IF(@fk_vieja IS NULL, 'DO 0', CONCAT('ALTER TABLE nodos DROP FOREIGN KEY `', @fk_vieja, '`'));
PREPARE st FROM @sql; EXECUTE st; DEALLOCATE PREPARE st;

SET @hace_falta := (SELECT COUNT(*) = 0 FROM information_schema.REFERENTIAL_CONSTRAINTS
                    WHERE CONSTRAINT_SCHEMA = DATABASE() AND TABLE_NAME = 'nodos' AND CONSTRAINT_NAME = 'fk_nodos_aula');
SET @sql := IF(@hace_falta,
    'ALTER TABLE nodos ADD CONSTRAINT fk_nodos_aula FOREIGN KEY (id_aula) REFERENCES aulas (id) ON DELETE SET NULL',
    'DO 0');
PREPARE st FROM @sql; EXECUTE st; DEALLOCATE PREPARE st;

-- ---------- asistencias.clase_id (deduplicación del fichaje reenviado) ----------
SET @hace_falta := (SELECT COUNT(*) = 0 FROM information_schema.COLUMNS
                    WHERE TABLE_SCHEMA = DATABASE() AND TABLE_NAME = 'asistencias' AND COLUMN_NAME = 'clase_id');
SET @sql := IF(@hace_falta, 'ALTER TABLE asistencias ADD COLUMN clase_id INT NULL AFTER aula_id', 'DO 0');
PREPARE st FROM @sql; EXECUTE st; DEALLOCATE PREPARE st;

SET @hace_falta := (SELECT COUNT(*) = 0 FROM information_schema.REFERENTIAL_CONSTRAINTS
                    WHERE CONSTRAINT_SCHEMA = DATABASE() AND TABLE_NAME = 'asistencias' AND REFERENCED_TABLE_NAME = 'clases');
SET @sql := IF(@hace_falta,
    'ALTER TABLE asistencias ADD CONSTRAINT fk_asistencias_clase FOREIGN KEY (clase_id) REFERENCES clases (id) ON DELETE SET NULL',
    'DO 0');
PREPARE st FROM @sql; EXECUTE st; DEALLOCATE PREPARE st;

SET @hace_falta := (SELECT COUNT(*) = 0 FROM information_schema.STATISTICS
                    WHERE TABLE_SCHEMA = DATABASE() AND TABLE_NAME = 'asistencias' AND INDEX_NAME = 'idx_asistencias_usuario_clase');
SET @sql := IF(@hace_falta,
    'CREATE INDEX idx_asistencias_usuario_clase ON asistencias (usuario_id, aula_id, clase_id)',
    'DO 0');
PREPARE st FROM @sql; EXECUTE st; DEALLOCATE PREPARE st;

-- ---------- usuarios.uid_tarjeta opcional ----------
-- Conserjes y administradores no tienen por qué tener tarjeta
-- (scripts/create_user.py inserta sin ella). UNIQUE admite varios NULL.
ALTER TABLE usuarios MODIFY COLUMN uid_tarjeta VARCHAR(50) NULL;

-- ---------- gateway de la Raspberry ----------
-- Cambiar la api_key (mínimo 16 caracteres) y usar la misma en LORALAN_API_KEY.
-- El device_id debe coincidir con LORALAN_GATEWAY_NAME.
INSERT IGNORE INTO nodos (device_id, api_key) VALUES ('gw-rpi-01', 'CAMBIAR-esta-api-key-del-gateway');
