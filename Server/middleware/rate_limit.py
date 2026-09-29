"""
Configuración de rate limiting para el sistema.

Este módulo centraliza los límites de tasa para diferentes endpoints
y proporciona funciones helper para manejar excepciones de rate limiting.
"""

from slowapi import Limiter
from slowapi.util import get_remote_address
from slowapi.errors import RateLimitExceeded
from fastapi import Request, status
from fastapi.responses import JSONResponse
import logging

logger = logging.getLogger(__name__)

# Límites por tipo de endpoint
RATE_LIMITS = {
    # Autenticación - más restrictivo para prevenir fuerza bruta
    "login": "5/minute",           # Login web (usuarios)
    "auth_device": "10/minute",    # Autenticación dispositivos ESP32
    "refresh_token": "10/minute",  # Renovación de tokens
    
    # API General - límites normales
    "api_default": "60/minute",    # Endpoints API generales
    "api_read": "100/minute",      # Endpoints solo lectura
    "api_write": "30/minute",      # Endpoints escritura
    
    # WebSocket - muy permisivo
    "websocket": "300/minute",     # Conexiones WebSocket
    
    # Registro de asistencia - moderado
    "registro": "20/minute",       # Registrar asistencia (NFC)
    "historial": "30/minute",      # Consultar historial
    
    # Control de aulas
    "control_relay": "15/minute",  # Toggle de relés
    "estado_aulas": "60/minute",   # Consultar estado
}

def get_limit(endpoint_type: str) -> str:
    """
    Obtiene el límite configurado para un tipo de endpoint.
    
    Args:
        endpoint_type: Tipo de endpoint (login, api_default, etc)
        
    Returns:
        str: Límite en formato "N/period" (ej: "5/minute")
        
    Example:
        >>> get_limit("login")
        "5/minute"
    """
    return RATE_LIMITS.get(endpoint_type, RATE_LIMITS["api_default"])

async def rate_limit_exceeded_handler(request: Request, exc: RateLimitExceeded) -> JSONResponse:
    """
    Handler personalizado para errores de rate limiting.
    
    Retorna una respuesta JSON clara indicando que se excedió el límite
    y cuándo puede volver a intentar.
    
    Args:
        request: Request de FastAPI
        exc: Excepción RateLimitExceeded de slowapi
        
    Returns:
        JSONResponse con status 429 y detalles del error
    """
    # Extraer información del límite
    endpoint = request.url.path
    client_ip = get_remote_address(request)
    
    logger.warning(
        f"🚫 Rate limit excedido: {client_ip} en {endpoint} - {exc.detail}"
    )
    
    return JSONResponse(
        status_code=status.HTTP_429_TOO_MANY_REQUESTS,
        content={
            "error": "Rate limit excedido",
            "detail": "Demasiados intentos. Por favor, espera un momento antes de reintentar.",
            "endpoint": endpoint,
            "retry_after": "60 segundos"
        },
        headers={
            "Retry-After": "60"  # Header estándar HTTP
        }
    )

def get_client_identifier(request: Request) -> str:
    """
    Obtiene un identificador único para el cliente.
    
    Prioriza en orden:
    1. Header X-Forwarded-For (si está detrás de proxy)
    2. Header X-Real-IP
    3. IP del cliente directo
    
    Args:
        request: Request de FastAPI
        
    Returns:
        str: IP/identificador del cliente
    """
    # Si está detrás de un proxy/load balancer
    forwarded_for = request.headers.get("X-Forwarded-For")
    if forwarded_for:
        # Tomar la primera IP (cliente original)
        return forwarded_for.split(",")[0].strip()
    
    # Si usa X-Real-IP (nginx)
    real_ip = request.headers.get("X-Real-IP")
    if real_ip:
        return real_ip
    
    # IP directa
    return get_remote_address(request)

# Crear instancia del limiter
limiter = Limiter(
    key_func=get_client_identifier,
    default_limits=[RATE_LIMITS["api_default"]],
    headers_enabled=True,  # Añadir headers X-RateLimit-*
)
