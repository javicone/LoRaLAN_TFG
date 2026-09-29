from fastapi import APIRouter
from services.clase_service import obtener_clase_test, get_clase_from_webservice

router = APIRouter(prefix="/clases", tags=["Clases"])

@router.get("/actual")
async def clase_actual(aula: str):
    """Obtiene la clase actual de un aula"""
    return await get_clase_from_webservice(aula)

@router.get("/test")
async def clase_test(aula: str):
    """Endpoint de prueba con datos simulados"""
    return await obtener_clase_test(aula)