from datetime import datetime, timedelta
from fastapi import HTTPException
from repositories.claseRepository import get_clase_from_webservice

async def obtener_clase_test(aula: str) -> dict:
    clase_siguiente = await get_clase_from_webservice(aula)
    
    # Si el webservice devuelve un mensaje (no hay clases), devolverlo
    if "mensaje" in clase_siguiente:
        return {
            "mensaje": clase_siguiente["mensaje"]
        }
    
    # Si hay datos de clase, formatearlos
    return {
        "asignatura": clase_siguiente["asignatura"],
        "hora_inicio": clase_siguiente["hora_inicio"].strftime("%H:%M:%S"),
        "hora_fin": clase_siguiente["hora_fin"].strftime("%H:%M:%S"),
    }
    
    # Datos de prueba basados en la hora actual
    ahora = datetime.now()
    hora_inicio = ahora.replace(minute=0, second=0, microsecond=0)
    hora_fin = hora_inicio + timedelta(hours=1)
    
    return {
        "aula": aula,
        "asignatura": "Asignatura de Prueba",
        "hora_inicio": hora_inicio.strftime("%H:%M:%S"),
        "hora_fin": hora_fin.strftime("%H:%M:%S"),
        "estado": "test"
    }