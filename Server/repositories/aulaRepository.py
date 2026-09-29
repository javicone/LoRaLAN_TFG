from utils.database_pool import get_db_pool
import aiomysql

# =============================================
# REPOSITORY: Aulas CRUD
# =============================================

async def get_all_aulas():
    """Obtiene todas las aulas"""
    pool = await get_db_pool()
    async with pool.acquire() as conn:
        async with conn.cursor(aiomysql.DictCursor) as cursor:
            await cursor.execute("SELECT id, nombre FROM aulas ORDER BY nombre ASC")
            return await cursor.fetchall()

async def get_aula_by_id(aula_id: int):
    """Obtiene un aula por su ID"""
    pool = await get_db_pool()
    async with pool.acquire() as conn:
        async with conn.cursor(aiomysql.DictCursor) as cursor:
            await cursor.execute(
                "SELECT id, nombre FROM aulas WHERE id = %s", 
                (aula_id,)
            )
            return await cursor.fetchone()

async def get_aula_by_nombre(nombre: str):
    """Obtiene un aula por su nombre"""
    pool = await get_db_pool()
    async with pool.acquire() as conn:
        async with conn.cursor(aiomysql.DictCursor) as cursor:
            await cursor.execute(
                "SELECT id, nombre FROM aulas WHERE nombre = %s", 
                (nombre,)
            )
            return await cursor.fetchone()

async def create_aula(nombre: str):
    """Crea un aula nueva"""
    pool = await get_db_pool()
    async with pool.acquire() as conn:
        async with conn.cursor() as cursor:
            sql = "INSERT INTO aulas (nombre) VALUES (%s)"
            await cursor.execute(sql, (nombre,))
            await conn.commit()
            return cursor.lastrowid

async def update_aula(aula_id: int, nombre: str = None):
    """Actualiza un aula existente"""
    pool = await get_db_pool()
    async with pool.acquire() as conn:
        async with conn.cursor(aiomysql.DictCursor) as cursor:
            updates = []
            values = []
            
            if nombre is not None:
                updates.append("nombre = %s")
                values.append(nombre)
            
            if not updates:
                return None
            
            values.append(aula_id)
            sql = f"UPDATE aulas SET {', '.join(updates)} WHERE id = %s"
            
            await cursor.execute(sql, tuple(values))
            await conn.commit()
            
            if cursor.rowcount == 0:
                return None
            
            # Devolver el aula actualizada
            await cursor.execute("SELECT id, nombre FROM aulas WHERE id = %s", (aula_id,))
            return await cursor.fetchone()

async def delete_aula(aula_id: int):
    """Elimina un aula (verifica que no haya dependencias)"""
    pool = await get_db_pool()
    async with pool.acquire() as conn:
        async with conn.cursor() as cursor:
            # Verificar si hay clases o asistencias asociadas
            await cursor.execute("SELECT COUNT(*) FROM clases WHERE aula_id = %s", (aula_id,))
            clases_count = (await cursor.fetchone())[0]
            
            await cursor.execute("SELECT COUNT(*) FROM asistencias WHERE aula_id = %s", (aula_id,))
            asistencias_count = (await cursor.fetchone())[0]
            
            if clases_count > 0 or asistencias_count > 0:
                return False, f"No se puede eliminar: tiene {clases_count} clases y {asistencias_count} asistencias asociadas"
            
            await cursor.execute("DELETE FROM aulas WHERE id = %s", (aula_id,))
            await conn.commit()
            return cursor.rowcount > 0, "Aula eliminada correctamente"
