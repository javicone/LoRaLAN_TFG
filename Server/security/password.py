"""
Sistema de hashing de contraseñas usando bcrypt.

Proporciona funciones seguras para:
- Hashear contraseñas nuevas
- Verificar contraseñas contra su hash
- Detectar hashes que necesitan actualización
"""

import bcrypt
from typing import Tuple
import logging

logger = logging.getLogger(__name__)

# Factor de trabajo (más alto = más seguro pero más lento)
BCRYPT_ROUNDS = 12

def _prepare_password(password: str) -> bytes:
    """
    Prepara la contraseña para bcrypt:
    - Convierte a bytes UTF-8
    - Trunca a 72 bytes (límite de bcrypt)
    """
    password_bytes = password.encode('utf-8')
    # bcrypt tiene un límite de 72 bytes
    return password_bytes[:72]

def hash_password(password: str) -> str:
    """
    Hashea una contraseña usando bcrypt.
    
    Args:
        password: Contraseña en texto plano
        
    Returns:
        str: Hash bcrypt de la contraseña (formato: $2b$12$...)
        
    Example:
        >>> hashed = hash_password("MiContraseña123!")
        >>> hashed.startswith("$2b$")
        True
    """
    try:
        password_bytes = _prepare_password(password)
        salt = bcrypt.gensalt(rounds=BCRYPT_ROUNDS)
        hashed = bcrypt.hashpw(password_bytes, salt)
        logger.debug(f"🔒 Contraseña hasheada exitosamente")
        return hashed.decode('utf-8')
    except Exception as e:
        logger.error(f"❌ Error hasheando contraseña: {e}")
        raise

def verify_password(plain_password: str, hashed_password: str) -> bool:
    """
    Verifica si una contraseña coincide con su hash.
    
    Esta función es resistente a timing attacks ya que bcrypt
    internamente usa comparación de tiempo constante.
    
    Args:
        plain_password: Contraseña en texto plano
        hashed_password: Hash bcrypt almacenado
        
    Returns:
        bool: True si la contraseña es correcta, False en caso contrario
        
    Example:
        >>> hashed = hash_password("MiContraseña123!")
        >>> verify_password("MiContraseña123!", hashed)
        True
        >>> verify_password("OtraContraseña", hashed)
        False
    """
    try:
        password_bytes = _prepare_password(plain_password)
        hashed_bytes = hashed_password.encode('utf-8')
        result = bcrypt.checkpw(password_bytes, hashed_bytes)
        if result:
            logger.debug("✅ Contraseña verificada correctamente")
        else:
            logger.debug("❌ Contraseña incorrecta")
        return result
    except Exception as e:
        logger.warning(f"⚠️ Error verificando contraseña: {e}")
        # Si hay error (ej: hash inválido), retornar False
        return False

def needs_rehash(hashed_password: str) -> bool:
    """
    Verifica si un hash necesita ser regenerado.
    
    Útil cuando se cambia el factor de trabajo (rounds) o se actualiza
    el algoritmo de hashing.
    
    Args:
        hashed_password: Hash bcrypt existente
        
    Returns:
        bool: True si debe rehashear la contraseña
        
    Example:
        >>> # Si cambiamos de rounds=10 a rounds=12
        >>> needs_rehash("$2b$10$old_hash...")
        True
    """
    try:
        # Extraer el número de rounds del hash existente
        # Formato: $2b$XX$... donde XX son los rounds
        if not is_hashed(hashed_password):
            return True
        parts = hashed_password.split('$')
        if len(parts) >= 3:
            current_rounds = int(parts[2])
            return current_rounds != BCRYPT_ROUNDS
        return False
    except Exception as e:
        logger.warning(f"⚠️ Error verificando si necesita rehash: {e}")
        return False

def is_hashed(password: str) -> bool:
    """
    Verifica si una cadena parece ser un hash bcrypt.
    
    Args:
        password: Cadena a verificar
        
    Returns:
        bool: True si parece un hash bcrypt válido
        
    Example:
        >>> is_hashed("$2b$12$abc123...")
        True
        >>> is_hashed("contraseña_texto_plano")
        False
    """
    return password.startswith("$2b$") or password.startswith("$2a$")
