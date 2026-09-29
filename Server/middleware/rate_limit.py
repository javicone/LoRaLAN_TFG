"""
Respuesta 429 del rate limiting. Los límites se declaran en cada endpoint
con @limiter.limit (routers/auth.py).
"""

from slowapi.util import get_remote_address
from slowapi.errors import RateLimitExceeded
from fastapi import Request, status
from fastapi.responses import JSONResponse
import logging

logger = logging.getLogger(__name__)


async def rate_limit_exceeded_handler(request: Request, exc: RateLimitExceeded) -> JSONResponse:
    endpoint = request.url.path
    logger.warning(f"🚫 Rate limit excedido: {get_remote_address(request)} en {endpoint} - {exc.detail}")
    return JSONResponse(
        status_code=status.HTTP_429_TOO_MANY_REQUESTS,
        content={
            "error": "Rate limit excedido",
            "detail": "Demasiados intentos. Por favor, espera un momento antes de reintentar.",
            "endpoint": endpoint,
            "retry_after": "60 segundos"
        },
        headers={"Retry-After": "60"}
    )
