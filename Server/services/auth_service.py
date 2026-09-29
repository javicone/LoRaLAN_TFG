from datetime import datetime, timedelta
from jose import JWTError, jwt
from fastapi import HTTPException, status
from config import SECRET_KEY, ALGORITHM, ACCESS_TOKEN_EXPIRE_MINUTES
from models.user import TokenData, UserInDB
from utils.database_pool import get_db_pool
import aiomysql
from repositories.usuarioRepository import get_usuario_por_mail
from security.password import verify_password, hash_password, is_hashed
import logging

logger = logging.getLogger(__name__)
    
def create_access_token(data: dict) -> str:
    """Crea un token JWT simple"""
    to_encode = data.copy()
    expire = datetime.utcnow() + timedelta(minutes=ACCESS_TOKEN_EXPIRE_MINUTES)
    to_encode.update({"exp": expire})
    encoded_jwt = jwt.encode(to_encode, SECRET_KEY, algorithm=ALGORITHM)
    return encoded_jwt

def decode_access_token(token: str) -> TokenData:
    """Decodifica y valida un token JWT"""
    credentials_exception = HTTPException(
        status_code=status.HTTP_401_UNAUTHORIZED,
        detail="No se pudieron validar las credenciales",
        headers={"WWW-Authenticate": "Bearer"},
    )
    try:
        payload = jwt.decode(token, SECRET_KEY, algorithms=[ALGORITHM])
        
        # ✅ FIX: Buscar en múltiples campos posibles
        user_id: str = payload.get("sub") or payload.get("id") or payload.get("device_id")
        rol: str = payload.get("rol")
        
        if user_id is None or rol is None:
            raise credentials_exception
            
        return TokenData(id=user_id, rol=rol)
        
    except JWTError:
        raise credentials_exception

async def authenticate_user(correo: str, password: str) -> UserInDB:
    """
    Autentica un usuario por correo y contraseña.
    
    ✅ ACTUALIZADO: Usa bcrypt para verificar contraseña hasheada.
    También mantiene compatibilidad temporal con contraseñas en texto plano
    para facilitar la migración.
    
    Args:
        correo: Email del usuario
        password: Contraseña en texto plano
        
    Returns:
        UserInDB: Datos del usuario autenticado
        
    Raises:
        HTTPException: 401 si credenciales inválidas
    """
    user = await get_usuario_por_mail(correo)
    
    if not user:
        logger.warning(f"❌ Intento de login fallido: usuario no existe ({correo})")
        # Ejecutar verify_password con valores dummy para prevenir timing attacks
        verify_password("dummy_password", "$2b$12$dummy_hash_to_prevent_timing_attack_abcdefghijklmnopqrstuvwxy")
        raise HTTPException(
            status_code=401, 
            detail="Correo o contraseña incorrectos"
        )
    
    stored_password = user["password"]
    
    # ✅ Verificar si la contraseña está hasheada
    if is_hashed(stored_password):
        # Contraseña hasheada con bcrypt - verificación segura
        if not verify_password(password, stored_password):
            logger.warning(f"❌ Intento de login fallido: contraseña incorrecta ({correo})")
            raise HTTPException(
                status_code=401, 
                detail="Correo o contraseña incorrectos"
            )
        logger.info(f"✅ Login exitoso (bcrypt): {correo}")
    else:
        # ⚠️ COMPATIBILIDAD TEMPORAL: Contraseña en texto plano
        # Esto permite que usuarios existentes sigan logueándose mientras se migra
        logger.warning(f"⚠️ Usuario {correo} tiene contraseña sin hashear - considerar migración")
        
        if stored_password != password:
            logger.warning(f"❌ Intento de login fallido: contraseña incorrecta ({correo})")
            raise HTTPException(
                status_code=401, 
                detail="Correo o contraseña incorrectos"
            )
        
        logger.info(f"✅ Login exitoso (texto plano - MIGRAR): {correo}")
        
        # TODO: Opcionalmente, auto-migrar la contraseña en el siguiente login
        # new_hash = hash_password(password)
        # await update_user_password(user["id"], new_hash)
    
    return UserInDB(**user)