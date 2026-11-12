from utils.database_pool import get_db_pool
import aiomysql
import aiohttp
import asyncio
import logging
from typing import Optional, Dict

logger = logging.getLogger(__name__)

async def get_clase_from_webservice(aula: str) -> Optional[Dict]:
    """
    Obtiene la información de la clase actual desde el webservice de reserva de instalaciones.
    
    Args:
        aula: Nombre del aula (ej: "Aula 1")
    
    Returns:
        Dict con {asignatura, hora_inicio, hora_fin} o None si hay error
    """
    url = "https://docencia-cum.unex.es/ReservaInstalaciones_ant/ws.php"
    
    try:
        logger.info(f"🌐 Consultando webservice para aula: {aula}")
        
        async with aiohttp.ClientSession() as session:
            # Realizar petición GET con el parámetro aula. Timeout de 0,5 s: el
            # gateway corta la petición del fichaje a los 800 ms
            # (FICHAJE_HTTP_TIMEOUT_MS); si el webservice no llega a tiempo se
            # sigue sin él y se abre la clase automática.
            async with session.get(url, params={"aula": aula}, timeout=aiohttp.ClientTimeout(total=0.5)) as response:
                
                # Verificar código de respuesta HTTP
                if response.status != 200:
                    logger.error(f"❌ Error HTTP {response.status} al consultar webservice")
                    return None
                
                # Intentar parsear como JSON
                try:
                    data = await response.json()
                    
                    # Verificar que tenga los campos esperados
                    if "asignatura" in data and "hora_inicio" in data and "hora_fin" in data:
                        logger.info(f"✅ Datos obtenidos del webservice: {data['asignatura']}")
                        return {
                            "asignatura": data["asignatura"],
                            "hora_inicio": data["hora_inicio"],
                            "hora_fin": data["hora_fin"]
                        }
                    # Si no hay campos de clase pero hay mensaje, devolver el mensaje
                    elif "mensaje" in data:
                        logger.info(f"ℹ️ Mensaje del webservice: {data['mensaje']}")
                        return {
                            "mensaje": data["mensaje"]
                        }
                    else:
                        logger.warning(f"⚠️ Respuesta JSON sin campos esperados: {data}")
                        return None
                        
                except aiohttp.ContentTypeError:
                    # Si no es JSON, intentar leer como texto (podría ser un mensaje de error)
                    text = await response.text()
                    logger.warning(f"⚠️ Respuesta no es JSON válido: {text}")
                    return None
                    
    except aiohttp.ClientError as e:
        logger.error(f"❌ Error de conexión al webservice: {e}")
        return None
    except asyncio.TimeoutError:
        logger.error(f"❌ Timeout al consultar webservice (>0,5 s)")
        return None
    except Exception as e:
        logger.error(f"❌ Error inesperado al consultar webservice: {e}")
        return None


async def guardar_clase_webservice(
    aula_id: int, 
    profesor_id: int, 
    asignatura: str, 
    hora_inicio: str, 
    hora_fin: str,
    fecha: str = None
) -> int:
    """
    Guarda una clase obtenida del webservice en la BD local.
    Evita duplicados verificando si ya existe una clase con los mismos datos.
    
    Args:
        aula_id: ID del aula
        profesor_id: ID del profesor que fichó
        asignatura: Nombre de la asignatura
        hora_inicio: Hora de inicio (formato HH:MM o HH:MM:SS)
        hora_fin: Hora de fin (formato HH:MM o HH:MM:SS)
        fecha: Fecha de la clase (YYYY-MM-DD), si no se proporciona usa la actual
    
    Returns:
        ID de la clase (existente o nueva)
    """
    from datetime import datetime, date
    
    pool = await get_db_pool()
    
    # Si no hay fecha, usar la actual
    if not fecha:
        fecha = date.today().isoformat()
    
    # Construir datetime completos
    try:
        # Parsear hora_inicio y hora_fin
        if isinstance(hora_inicio, str):
            # Puede venir como "HH:MM" o "HH:MM:SS"
            if len(hora_inicio) == 5:
                hora_inicio += ":00"
            dt_inicio = datetime.strptime(f"{fecha} {hora_inicio}", "%Y-%m-%d %H:%M:%S")
        else:
            dt_inicio = hora_inicio
            
        if isinstance(hora_fin, str):
            if len(hora_fin) == 5:
                hora_fin += ":00"
            dt_fin = datetime.strptime(f"{fecha} {hora_fin}", "%Y-%m-%d %H:%M:%S")
        else:
            dt_fin = hora_fin
            
    except ValueError as e:
        logger.error(f"❌ Error parseando fechas: {e}")
        raise
    
    async with pool.acquire() as conn:
        async with conn.cursor(aiomysql.DictCursor) as cursor:
            # Verificar si ya existe esta clase (misma aula, asignatura, hora_inicio, hora_fin)
            await cursor.execute(
                """
                SELECT id FROM clases 
                WHERE aula_id = %s 
                  AND asignatura = %s 
                  AND hora_inicio = %s 
                  AND hora_fin = %s
                """,
                (aula_id, asignatura, dt_inicio, dt_fin)
            )
            clase_existente = await cursor.fetchone()
            
            if clase_existente:
                logger.info(f"ℹ️ Clase ya existe en BD con ID: {clase_existente['id']}")
                return clase_existente['id']
            
            # Insertar nueva clase
            await cursor.execute(
                """
                INSERT INTO clases (aula_id, profesor_id, asignatura, hora_inicio, hora_fin)
                VALUES (%s, %s, %s, %s, %s)
                """,
                (aula_id, profesor_id, asignatura, dt_inicio, dt_fin)
            )
            clase_id = cursor.lastrowid
            logger.info(f"✅ Clase guardada en BD con ID: {clase_id}")
            return clase_id