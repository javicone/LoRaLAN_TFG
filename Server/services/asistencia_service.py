from datetime import datetime
from fastapi import HTTPException
import logging
from repositories.asistenciaRepository import (
    get_usuario_id_by_uid,
    get_aula_id_by_device_id,
    get_clase_actual_by_aula_and_time,
    insert_asistencia,
    existe_asistencia_en_clase,
    fetch_asistencias_by_usuario,
    fetch_all_asistencias,
)
from repositories.claseRepository import get_clase_from_webservice, guardar_clase_webservice

logger = logging.getLogger(__name__)

async def registrar_asistencia(uid: str, device_id: str, timestamp: datetime = None) -> dict:
    """Registra asistencia vinculando profesor, aula y clase actual"""
    
    # 1. Validar que el profesor existe
    usuario_id = await get_usuario_id_by_uid(uid)
    if not usuario_id:
        logger.error(f"❌ Profesor no encontrado para UID: {uid}")
        raise HTTPException(status_code=404, detail="Profesor no encontrado")
    
    # 2. Obtener aula desde device_id y nombre del aula
    aula_id = await get_aula_id_by_device_id(device_id)
    if not aula_id:
        logger.error(f"❌ Aula no encontrada para device_id: {device_id}")
        raise HTTPException(status_code=404, detail=f"Aula no configurada para dispositivo {device_id}")
    
    # Obtener nombre del aula desde aulas_estado para consultar webservice
    from services.aula_service import aulas_estado
    nombre_aula = None
    if device_id in aulas_estado:
        nombre_aula = aulas_estado[device_id].get("aula") or aulas_estado[device_id].get("nombre")
    if not nombre_aula:
        from repositories.nodos import aula_de_nodo
        aula_bd = await aula_de_nodo(device_id)
        nombre_aula = aula_bd["nombre"] if aula_bd else None
    
    # 3. Usar timestamp del dispositivo o del servidor
    fecha_hora = timestamp if timestamp else datetime.now()
    
    # 4. Intentar obtener clase actual desde BD primero
    clase = await get_clase_actual_by_aula_and_time(aula_id, fecha_hora)
    
    # 5. Si no hay clase en BD, consultar webservice
    if not clase and nombre_aula:
        logger.info(f"🌐 No hay clase en BD, consultando webservice para {nombre_aula}")
        clase_ws = await get_clase_from_webservice(nombre_aula)
        
        if clase_ws and "asignatura" in clase_ws:
            # ✅ GUARDAR la clase del webservice en la BD para futuras consultas y estadísticas
            try:
                clase_id_guardada = await guardar_clase_webservice(
                    aula_id=aula_id,
                    profesor_id=usuario_id,
                    asignatura=clase_ws["asignatura"],
                    hora_inicio=clase_ws["hora_inicio"],
                    hora_fin=clase_ws["hora_fin"]
                )
                
                # Convertir respuesta del webservice a formato esperado
                clase = {
                    "id": clase_id_guardada,  # Ahora tenemos el ID de la BD
                    "profesor_id": usuario_id,
                    "asignatura": clase_ws["asignatura"],
                    "hora_inicio": clase_ws.get("hora_inicio"),
                    "hora_fin": clase_ws.get("hora_fin")
                }
                logger.info(f"✅ Clase obtenida desde webservice y guardada en BD: {clase['asignatura']} (ID: {clase_id_guardada})")
            except Exception as e:
                logger.error(f"❌ Error guardando clase del webservice: {e}")
                # Continuar sin guardar pero permitir el fichaje
                clase = {
                    "id": None,
                    "profesor_id": usuario_id,
                    "asignatura": clase_ws["asignatura"],
                    "hora_inicio": clase_ws.get("hora_inicio"),
                    "hora_fin": clase_ws.get("hora_fin")
                }
    
    clase_id = None
    asignatura = None
    
    if clase:
        # 6. Validar que el profesor del fichaje coincida con el profesor de la clase
        # Solo validar si tenemos profesor_id de la BD
        if clase.get("profesor_id") and clase["profesor_id"] != usuario_id:
            logger.warning(
                f"⚠️ El profesor {usuario_id} (UID: {uid}) intentó fichar en clase de profesor {clase['profesor_id']}"
            )
            raise HTTPException(
                status_code=403, 
                detail=f"No puedes fichar en esta clase. La clase de {clase['asignatura']} está asignada a otro profesor"
            )
        
        clase_id = clase.get("id")
        asignatura = clase["asignatura"]
        logger.info(
            f"✅ Fichaje válido: Profesor {usuario_id} en clase ({asignatura}) "
            f"de {clase.get('hora_inicio')} a {clase.get('hora_fin')}"
        )
    else:
        logger.error(
            f"❌ No hay clase programada en aula '{nombre_aula}' para {fecha_hora}. "
            f"Fichaje rechazado."
        )
        raise HTTPException(
            status_code=400, 
            detail=f"No hay clase programada en '{nombre_aula}' en este momento. No se puede fichar."
        )
    
    # 7. Guardar asistencia, una sola vez por clase. Por LoRaLAN el nodo reenvía
    # el fichaje si no le llega la respuesta a tiempo (NACK REINTENTAR), aunque
    # el servidor ya lo hubiera registrado: el reenvío debe contestar OK sin duplicar.
    if clase_id and await existe_asistencia_en_clase(usuario_id, aula_id, clase_id):
        logger.info(f"ℹ️ Fichaje repetido de {usuario_id} en clase {clase_id}: ya registrado")
    else:
        await insert_asistencia(usuario_id, aula_id, fecha_hora, clase_id)
    
    return {
        "mensaje": "Asistencia registrada",
        "usuario_id": usuario_id,
        "aula_id": aula_id,
        "clase_id": clase_id,
        "asignatura": asignatura,
        "fecha_hora": fecha_hora
    }

async def obtener_asistencias(user_id: str, rol: str) -> dict:
    """
    Obtiene las asistencias según el rol del usuario.
    Devuelve: { "nombre": "...", "asistencias": [...] }
    """
    from repositories.asistenciaRepository import get_nombre_usuario_by_id
    
    # Obtener nombre del usuario
    nombre_usuario = await get_nombre_usuario_by_id(user_id)
    
    if rol == "profesor":
        asistencias = await fetch_asistencias_by_usuario(user_id)
    elif rol in ["conserje", "administrador"]:
        asistencias = await fetch_all_asistencias()
    else:
        raise HTTPException(status_code=403, detail="Acceso denegado")
    
    # Serializar datetime para JSON
    asistencias_serializadas = []
    for asist in asistencias:
        asist_dict = dict(asist)
        # Convertir datetime a string ISO
        for key in ['fecha_hora', 'hora_inicio', 'hora_fin']:
            if key in asist_dict and asist_dict[key]:
                asist_dict[key] = asist_dict[key].isoformat()
        asistencias_serializadas.append(asist_dict)
    
    # Obtener estadísticas mensuales para profesores
    estadisticas = None
    if rol == "profesor":
        from repositories.asistenciaRepository import fetch_estadisticas_mensuales
        from datetime import datetime
        ahora = datetime.now()
        estadisticas = await fetch_estadisticas_mensuales(user_id, ahora.year, ahora.month)
    
    return {
        "nombre": nombre_usuario or "Docente",
        "estadisticas": estadisticas,
        "asistencias": asistencias_serializadas
    }