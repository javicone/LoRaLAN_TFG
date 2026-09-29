from typing import Dict, Any, Optional
from fastapi import HTTPException
import logging

logger = logging.getLogger(__name__)

# Estado en memoria (podría moverse a Redis en producción)
aulas_estado: Dict[str, Any] = {}

def actualizar_estado_aula(device_id: str, datos: dict) -> dict:
    """Actualiza el estado de un aula en memoria usando device_id como clave"""
    if device_id not in aulas_estado:
        aulas_estado[device_id] = {}
    
    # Actualizar datos
    aulas_estado[device_id].update(datos)
    aulas_estado[device_id]["device_id"] = device_id  # Asegurar que siempre esté
    
    logger.debug(f"✅ Estado actualizado para device_id '{device_id}': {aulas_estado[device_id]}")
    return aulas_estado[device_id]

def obtener_estado_aulas() -> dict:
    """Obtiene el estado de todas las aulas"""
    return aulas_estado

def actualizar_relay(aula: str, estado: bool) -> dict:
    """Actualiza el estado del relay de un aula"""
    if aula not in aulas_estado:
        raise HTTPException(status_code=404, detail="Aula no encontrada")
    
    aulas_estado[aula]["relay"] = estado
    return aulas_estado[aula]

def verificar_nombre_aula_disponible(nuevo_nombre: str, device_id_excluir: Optional[str] = None) -> tuple[bool, Optional[str]]:
    """
    Verifica si un nombre de aula está disponible (no usado por otro dispositivo).
    
    Args:
        nuevo_nombre: El nombre que se quiere asignar
        device_id_excluir: El device_id que está intentando usar el nombre (para excluirlo de la búsqueda)
    
    Returns:
        (disponible, device_id_que_lo_usa) - Si no está disponible, retorna el device_id que ya lo usa
    """
    nuevo_nombre_lower = nuevo_nombre.strip().lower()
    
    for device_id, aula_data in aulas_estado.items():
        # Excluir el propio dispositivo que está cambiando su nombre
        if device_id == device_id_excluir:
            continue
            
        nombre_actual = aula_data.get("aula") or aula_data.get("nombre", "")
        if nombre_actual and nombre_actual.strip().lower() == nuevo_nombre_lower:
            logger.warning(f"⚠️ Nombre '{nuevo_nombre}' ya está en uso por {device_id}")
            return False, device_id
    
    return True, None

# Mapeo aula_nombre → device_id (puede venir de BD o de aulas_estado)
def get_device_id_por_aula(aula_nombre: str) -> str | None:
    """
    Busca el device_id asociado a un nombre de aula.
    Busca en aulas_estado (clave: device_id, valor: dict con campo 'aula').
    """
    logger.debug(f"🔍 Buscando device_id para aula: '{aula_nombre}'")
    logger.debug(f"🔍 aulas_estado actual: {aulas_estado}")
    
    # Buscar por campo 'aula' en los datos de cada device_id
    for device_id, aula_data in aulas_estado.items():
        nombre = aula_data.get("aula") or aula_data.get("nombre")
        logger.debug(f"🔍 Revisando device_id '{device_id}': nombre='{nombre}'")
        if nombre and nombre.strip().lower() == aula_nombre.strip().lower():
            logger.info(f"✅ Encontrado device_id '{device_id}' para aula '{aula_nombre}'")
            return device_id

    # Si el nombre de aula es igual a un device_id (caso raro)
    if aula_nombre in aulas_estado:
        logger.info(f"✅ aula_nombre '{aula_nombre}' es directamente un device_id")
        return aula_nombre

    logger.warning(f"❌ No se encontró device_id para aula '{aula_nombre}'")
    return None