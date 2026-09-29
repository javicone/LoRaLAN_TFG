import logging

import aiomysql
from typing import Optional, Dict

from utils.database_pool import get_db_pool

logger = logging.getLogger(__name__)


async def fetch_one(conn, query: str, params=()):
    try:
        async with conn.cursor(aiomysql.DictCursor) as cursor:
            await cursor.execute(query, params)
            return await cursor.fetchone()
    except Exception as e:
        logger.error(f"DB Error fetch_one: {e}")
        return None


async def execute_write(conn, query: str, params=()):
    try:
        async with conn.cursor() as cursor:
            await cursor.execute(query, params)
            await conn.commit()
            return cursor.lastrowid
    except Exception as e:
        logger.error(f"DB Error write: {e}")
        await conn.rollback()
        return None


# --------------------------------------------------------
# LÓGICA DE NODOS (versión mínima)
# --------------------------------------------------------

async def verificar_nodo_existe(conn, device_id: str, api_key: str) -> bool:
    query = "SELECT id FROM nodos WHERE device_id = %s AND api_key = %s"
    return (await fetch_one(conn, query, (device_id, api_key))) is not None


async def obtener_nodo(conn, device_id: str) -> Optional[Dict]:
    query = "SELECT id, device_id, api_key FROM nodos WHERE device_id = %s"
    return await fetch_one(conn, query, (device_id,))


async def crear_nodo(conn, device_id: str, api_key: str) -> Optional[int]:
    query = "INSERT INTO nodos (device_id, api_key) VALUES (%s, %s)"
    return await execute_write(conn, query, (device_id, api_key))


async def actualizar_api_key(conn, device_id: str, nueva_key: str) -> bool:
    query = "UPDATE nodos SET api_key = %s WHERE device_id = %s"
    return execute_write(conn, query, (nueva_key, device_id)) is not None


async def eliminar_nodo(conn, device_id: str) -> bool:
    query = "DELETE FROM nodos WHERE device_id = %s"
    return execute_write(conn, query, (device_id,)) is not None


# --------------------------------------------------------
# NODOS LoRaLAN: el aula se guarda en BD (nodos.id_aula)
# --------------------------------------------------------
# Con WebSocket el nodo anunciaba su aula al conectar. Por LoRaLAN el nodo no
# sabe en qué aula está: lo asigna el administrador desde el panel.

async def registrar_nodo_si_no_existe(device_id: str) -> None:
    """Alta sin aula ni api_key la primera vez que el gateway sube algo del nodo."""
    pool = await get_db_pool()
    async with pool.acquire() as conn:
        await execute_write(conn, "INSERT IGNORE INTO nodos (device_id) VALUES (%s)", (device_id,))


async def aula_de_nodo(device_id: str) -> Optional[Dict]:
    """
    {'id', 'nombre'} del aula asignada al nodo, o None si no tiene.

    No usa fetch_one a propósito: fetch_one convierte un error de BD en None, y
    aquí None significa "nodo sin aula", que acaba en un NACK definitivo
    (DESCONOCIDO) y el nodo no reintenta. Un fallo de BD debe propagarse para
    que el fichaje responda REINTENTAR.
    """
    pool = await get_db_pool()
    async with pool.acquire() as conn:
        async with conn.cursor(aiomysql.DictCursor) as cursor:
            await cursor.execute(
                "SELECT a.id, a.nombre FROM nodos n JOIN aulas a ON a.id = n.id_aula WHERE n.device_id = %s",
                (device_id,),
            )
            return await cursor.fetchone()


async def asignar_aula_a_nodo(device_id: str, nombre_aula: str) -> Optional[int]:
    """Asigna el aula (creándola si no existe) y devuelve su id, o None si falla."""
    pool = await get_db_pool()
    async with pool.acquire() as conn:
        await execute_write(conn, "INSERT IGNORE INTO nodos (device_id) VALUES (%s)", (device_id,))
        await execute_write(conn, "INSERT IGNORE INTO aulas (nombre) VALUES (%s)", (nombre_aula,))
        aula = await fetch_one(conn, "SELECT id FROM aulas WHERE nombre = %s", (nombre_aula,))
        if not aula:
            return None
        await execute_write(conn, "UPDATE nodos SET id_aula = %s WHERE device_id = %s", (aula["id"], device_id))
        return aula["id"]
