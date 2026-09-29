from fastapi import APIRouter, Request, HTTPException
from jose import jwt, JWTError
from services.asistencia_service import obtener_asistencias
from services.auth_service import SECRET_KEY, ALGORITHM
import logging

logger = logging.getLogger(__name__)

router = APIRouter(prefix="/asistencias", tags=["Asistencias"])

# Los fichajes solo entran por POST /gateway/fichaje (token de gateway).

@router.get("/mis-asistencias")
async def mis_asistencias(request: Request):
    """Obtiene las asistencias del usuario autenticado con información detallada"""
    # DEBUG: Ver todas las cookies
    logger.info(f"🍪 Cookies recibidas: {request.cookies}")
    
    # Obtener token de la cookie
    token_str = request.cookies.get("access_token")
    
    if not token_str:
        logger.error("❌ Cookie 'access_token' no encontrada")
        raise HTTPException(status_code=401, detail="No autenticado")
    
    logger.info(f"✅ Token encontrado: {token_str[:20]}...")
    
    try:
        # Decodificar token
        payload = jwt.decode(token_str, SECRET_KEY, algorithms=[ALGORITHM])
        user_id = payload.get("sub") or payload.get("id")  # Puede ser 'sub' o 'id'
        rol = payload.get("rol")
        
        logger.info(f"👤 Usuario: {user_id}, Rol: {rol}")
        
        if not user_id or not rol:
            raise HTTPException(status_code=401, detail="Token inválido")
        
        # Obtener asistencias según el rol (ahora devuelve dict con nombre y asistencias)
        resultado = await obtener_asistencias(user_id, rol)
        return resultado
        
    except JWTError as e:
        logger.error(f"❌ Error JWT: {e}")
        raise HTTPException(status_code=401, detail="Token inválido o expirado")