from datetime import datetime
from utils.database_pool import get_db_pool
import aiomysql
from typing import Optional, Dict

async def get_usuario_id_by_uid(uid: str) -> int | None:
    pool = await get_db_pool()
    async with pool.acquire() as conn:
        async with conn.cursor(aiomysql.DictCursor) as cursor:
            await cursor.execute(
                "SELECT id FROM usuarios WHERE uid_tarjeta = %s", 
                (uid,)
            )
            usuario = await cursor.fetchone()
            return usuario["id"] if usuario else None

async def get_aula_id_by_device_id(device_id: str) -> int | None:
    """
    Obtiene el aula_id asociado a un device_id.
    Primero intenta buscar en aulas_estado (memoria), luego en BD.
    """
    from services.aula_service import aulas_estado
    
    # 1. Buscar en memoria (aulas_estado)
    if device_id in aulas_estado:
        nombre_aula = aulas_estado[device_id].get("aula") or aulas_estado[device_id].get("nombre")
        if nombre_aula:
            # Buscar aula_id por nombre
            return await get_aula_id_by_nombre(nombre_aula)
    
    # 2. Si no está en memoria, el aula asignada al nodo en BD (nodos.id_aula).
    # Es el caso normal con LoRaLAN: el nodo no anuncia su aula.
    from repositories.nodos import aula_de_nodo
    aula = await aula_de_nodo(device_id)
    return aula["id"] if aula else None

async def get_aula_id_by_nombre(nombre_aula: str) -> int | None:
    pool = await get_db_pool()
    async with pool.acquire() as conn:
        async with conn.cursor(aiomysql.DictCursor) as cursor:
            await cursor.execute(
                "SELECT id FROM aulas WHERE nombre = %s", 
                (nombre_aula,)
            )
            aula = await cursor.fetchone()
            return aula["id"] if aula else None

async def get_clase_actual_by_aula_and_time(aula_id: int, fecha_hora: datetime) -> Optional[Dict]:
    """Busca la clase que está en curso en un aula en un momento específico"""
    pool = await get_db_pool()
    async with pool.acquire() as conn:
        async with conn.cursor(aiomysql.DictCursor) as cursor:
            await cursor.execute(
                """
                SELECT id, profesor_id, asignatura, hora_inicio, hora_fin
                FROM clases
                WHERE aula_id = %s
                  AND %s BETWEEN hora_inicio AND hora_fin
                ORDER BY hora_inicio DESC
                LIMIT 1
                """,
                (aula_id, fecha_hora)
            )
            return await cursor.fetchone()

async def existe_asistencia_en_clase(usuario_id: int, aula_id: int, clase_id: int) -> bool:
    """True si el usuario ya fichó en esa clase y aula."""
    pool = await get_db_pool()
    async with pool.acquire() as conn:
        async with conn.cursor() as cursor:
            await cursor.execute(
                "SELECT 1 FROM asistencias WHERE usuario_id = %s AND aula_id = %s AND clase_id = %s LIMIT 1",
                (usuario_id, aula_id, clase_id)
            )
            return await cursor.fetchone() is not None

async def insert_asistencia(usuario_id: int, aula_id: int, fecha_hora: datetime, clase_id: Optional[int] = None) -> None:
    """
    Inserta asistencia con clase_id opcional.
    Si clase_id está presente, lo guarda (requiere la columna en la tabla).
    """
    pool = await get_db_pool()
    async with pool.acquire() as conn:
        async with conn.cursor(aiomysql.DictCursor) as cursor:
            if clase_id:
                # Intentar insertar con clase_id
                try:
                    await cursor.execute(
                        "INSERT INTO asistencias (usuario_id, aula_id, fecha_hora, clase_id) VALUES (%s, %s, %s, %s)",
                        (usuario_id, aula_id, fecha_hora, clase_id)
                    )
                except Exception as e:
                    # Si falla (columna no existe), insertar sin clase_id
                    import logging
                    logging.getLogger(__name__).warning(f"⚠️ No se pudo guardar clase_id, insertando sin él: {e}")
                    await cursor.execute(
                        "INSERT INTO asistencias (usuario_id, aula_id, fecha_hora) VALUES (%s, %s, %s)",
                        (usuario_id, aula_id, fecha_hora)
                    )
            else:
                await cursor.execute(
                    "INSERT INTO asistencias (usuario_id, aula_id, fecha_hora) VALUES (%s, %s, %s)",
                    (usuario_id, aula_id, fecha_hora)
                )

async def fetch_asistencias_by_usuario(user_id: str) -> list:
    """
    Obtiene las asistencias de un usuario con información detallada:
    - nombre_aula: nombre del aula
    - asignatura: asignatura de la clase en ese momento (si existe)
    - hora_inicio, hora_fin: horario de la clase (si existe)
    
    Usa clase_id si está disponible, o JOIN temporal como fallback.
    """
    pool = await get_db_pool()
    async with pool.acquire() as conn:
        async with conn.cursor(aiomysql.DictCursor) as cursor:
            await cursor.execute(
                """
                SELECT 
                    a.id,
                    a.usuario_id,
                    a.aula_id,
                    a.fecha_hora,
                    au.nombre AS nombre_aula,
                    COALESCE(c_direct.asignatura, c_temporal.asignatura) AS asignatura,
                    COALESCE(c_direct.hora_inicio, c_temporal.hora_inicio) AS hora_inicio,
                    COALESCE(c_direct.hora_fin, c_temporal.hora_fin) AS hora_fin
                FROM asistencias a
                JOIN aulas au ON a.aula_id = au.id
                LEFT JOIN clases c_direct ON a.clase_id = c_direct.id
                LEFT JOIN clases c_temporal ON c_temporal.aula_id = a.aula_id 
                    AND a.fecha_hora BETWEEN c_temporal.hora_inicio AND c_temporal.hora_fin
                    AND a.clase_id IS NULL
                WHERE a.usuario_id = %s
                ORDER BY a.fecha_hora DESC
                """, 
                (user_id,)
            )
            return await cursor.fetchall()

async def fetch_all_asistencias() -> list:
    """
    Obtiene todas las asistencias con información detallada para admin/conserje.
    Usa clase_id si está disponible, o JOIN temporal como fallback.
    """
    pool = await get_db_pool()
    async with pool.acquire() as conn:
        async with conn.cursor(aiomysql.DictCursor) as cursor:
            await cursor.execute(
                """
                SELECT 
                    a.id,
                    a.usuario_id,
                    a.aula_id,
                    a.fecha_hora,
                    au.nombre AS nombre_aula,
                    u.nombre AS nombre_usuario,
                    COALESCE(c_direct.asignatura, c_temporal.asignatura) AS asignatura,
                    COALESCE(c_direct.hora_inicio, c_temporal.hora_inicio) AS hora_inicio,
                    COALESCE(c_direct.hora_fin, c_temporal.hora_fin) AS hora_fin
                FROM asistencias a
                JOIN aulas au ON a.aula_id = au.id
                JOIN usuarios u ON a.usuario_id = u.id
                LEFT JOIN clases c_direct ON a.clase_id = c_direct.id
                LEFT JOIN clases c_temporal ON c_temporal.aula_id = a.aula_id 
                    AND a.fecha_hora BETWEEN c_temporal.hora_inicio AND c_temporal.hora_fin
                    AND a.clase_id IS NULL
                ORDER BY a.fecha_hora DESC
                """
            )
            return await cursor.fetchall()

async def get_nombre_usuario_by_id(user_id: str) -> str | None:
    """Obtiene el nombre del usuario por su ID"""
    pool = await get_db_pool()
    async with pool.acquire() as conn:
        async with conn.cursor(aiomysql.DictCursor) as cursor:
            await cursor.execute(
                "SELECT nombre FROM usuarios WHERE id = %s", 
                (user_id,)
            )
            usuario = await cursor.fetchone()
            return usuario["nombre"] if usuario else None

async def fetch_estadisticas_mensuales(user_id: str, año: int, mes: int) -> dict:
    """
    Calcula las horas de docencia por semana del mes especificado.
    Devuelve: {
        "mes": "Diciembre 2025",
        "semanas": [
            {"semana": 1, "fecha_inicio": "01/12", "fecha_fin": "07/12", "horas": 12.5},
            ...
        ],
        "total_mes": 45.0
    }
    """
    from datetime import date, timedelta
    import calendar
    
    pool = await get_db_pool()
    
    # Calcular primer y último día del mes
    primer_dia = date(año, mes, 1)
    ultimo_dia = date(año, mes, calendar.monthrange(año, mes)[1])
    
    # Nombres de meses en español
    meses_es = ["", "Enero", "Febrero", "Marzo", "Abril", "Mayo", "Junio",
                "Julio", "Agosto", "Septiembre", "Octubre", "Noviembre", "Diciembre"]
    
    async with pool.acquire() as conn:
        async with conn.cursor(aiomysql.DictCursor) as cursor:
            # Obtener todas las asistencias del mes con duración de clase
            # Usa clase_id si existe, o hace JOIN temporal como fallback
            await cursor.execute(
                """
                SELECT 
                    a.fecha_hora,
                    COALESCE(
                        TIMESTAMPDIFF(MINUTE, c_direct.hora_inicio, c_direct.hora_fin),
                        TIMESTAMPDIFF(MINUTE, c_temporal.hora_inicio, c_temporal.hora_fin)
                    ) AS duracion_minutos,
                    COALESCE(c_direct.asignatura, c_temporal.asignatura) AS asignatura
                FROM asistencias a
                LEFT JOIN clases c_direct ON a.clase_id = c_direct.id
                LEFT JOIN clases c_temporal ON c_temporal.aula_id = a.aula_id 
                    AND a.fecha_hora BETWEEN c_temporal.hora_inicio AND c_temporal.hora_fin
                    AND a.clase_id IS NULL
                WHERE a.usuario_id = %s
                  AND a.fecha_hora >= %s
                  AND a.fecha_hora < %s
                ORDER BY a.fecha_hora
                """,
                (user_id, primer_dia, ultimo_dia + timedelta(days=1))
            )
            asistencias = await cursor.fetchall()
    
    # Agrupar por semana
    semanas = []
    semana_actual = 1
    
    # Encontrar el lunes de la semana que contiene el primer día del mes
    inicio_semana = primer_dia - timedelta(days=primer_dia.weekday())
    
    while inicio_semana <= ultimo_dia:
        fin_semana = inicio_semana + timedelta(days=6)
        
        # Ajustar para mostrar solo días dentro del mes
        fecha_inicio_display = max(inicio_semana, primer_dia)
        fecha_fin_display = min(fin_semana, ultimo_dia)
        
        # Calcular horas de esta semana
        horas_semana = 0
        for asist in asistencias:
            fecha_asist = asist["fecha_hora"].date() if hasattr(asist["fecha_hora"], 'date') else asist["fecha_hora"]
            if isinstance(fecha_asist, datetime):
                fecha_asist = fecha_asist.date()
            
            if inicio_semana <= fecha_asist <= fin_semana:
                duracion = asist.get("duracion_minutos") or 0
                horas_semana += duracion / 60
        
        if fecha_inicio_display <= ultimo_dia:  # Solo incluir semanas que tocan el mes
            semanas.append({
                "semana": semana_actual,
                "fecha_inicio": fecha_inicio_display.strftime("%d/%m"),
                "fecha_fin": fecha_fin_display.strftime("%d/%m"),
                "horas": round(horas_semana, 1)
            })
            semana_actual += 1
        
        inicio_semana += timedelta(days=7)
    
    total_mes = sum(s["horas"] for s in semanas)
    
    return {
        "mes": f"{meses_es[mes]} {año}",
        "semanas": semanas,
        "total_mes": round(total_mes, 1)
    }