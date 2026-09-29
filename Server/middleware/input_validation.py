"""
Middleware de validación y sanitización de inputs.

Aplica validación automática a todas las requests para prevenir inyecciones.
Este middleware se ejecuta antes de que lleguen a los routers.

Author: Security Team
Date: 2026-02-11
"""

from fastapi import Request, HTTPException
from starlette.middleware.base import BaseHTTPMiddleware
from starlette.responses import JSONResponse
import logging
from typing import Optional
import json

from security.input_sanitization import check_injection_patterns, sanitize_for_log

logger = logging.getLogger(__name__)


class InputValidationMiddleware(BaseHTTPMiddleware):
    """
    Middleware que valida todos los inputs antes de procesarlos.
    
    Funcionalidad:
    - Detecta patrones de inyección SQL, XSS, Command Injection
    - Registra intentos de inyección
    - Bloquea requests peligrosas
    - Sanitiza logs para prevenir log injection
    """
    
    # Rutas que NO necesitan validación estricta (assets estáticos, etc.)
    EXEMPT_PATHS = [
        '/static/',
        '/health',
        '/docs',
        '/openapi.json',
        '/favicon.ico',
    ]
    
    # Headers que deben ser validados
    # NOTA: user-agent excluido porque contiene caracteres como ; ( ) 
    # que son legítimos en User-Agent strings pero disparan falsos positivos.
    HEADERS_TO_VALIDATE = [
        'referer',
        'x-forwarded-for',
        'x-real-ip',
    ]
    
    def __init__(self, app, enable_strict_mode: bool = True):
        """
        Args:
            app: FastAPI application
            enable_strict_mode: Si True, bloquea requests con patrones peligrosos.
                               Si False, solo registra warnings.
        """
        super().__init__(app)
        self.enable_strict_mode = enable_strict_mode
        logger.info(
            f"🛡️  InputValidationMiddleware iniciado "
            f"(strict_mode: {enable_strict_mode})"
        )
    
    async def dispatch(self, request: Request, call_next):
        """Procesa cada request antes de pasarla al router"""
        
        # 1. Skip rutas exentas
        if any(request.url.path.startswith(path) for path in self.EXEMPT_PATHS):
            return await call_next(request)
        
        try:
            # 2. Validar headers
            await self._validate_headers(request)
            
            # 3. Validar query parameters
            await self._validate_query_params(request)
            
            # 4. Validar body si existe
            if request.method in ['POST', 'PUT', 'PATCH']:
                await self._validate_body(request)
            
            # 5. Si llegó aquí, la request es segura
            response = await call_next(request)
            return response
            
        except HTTPException as e:
            # Re-raise HTTPExceptions (ya están manejadas)
            raise
        except ValueError as e:
            # Error de validación
            logger.warning(
                f"⚠️  Validación fallida: {sanitize_for_log(str(e))} "
                f"- IP: {self._get_client_ip(request)} "
                f"- Path: {request.url.path}"
            )
            return JSONResponse(
                status_code=400,
                content={
                    "detail": "Input inválido. Por favor verifica los datos enviados.",
                    "error_type": "validation_error"
                }
            )
        except Exception as e:
            # Error inesperado
            logger.error(
                f"❌ Error en InputValidationMiddleware: {sanitize_for_log(str(e))}"
            )
            return JSONResponse(
                status_code=500,
                content={"detail": "Error interno del servidor"}
            )
    
    async def _validate_headers(self, request: Request):
        """Valida headers de la request"""
        for header_name in self.HEADERS_TO_VALIDATE:
            header_value = request.headers.get(header_name, '')
            if header_value and check_injection_patterns(header_value):
                logger.warning(
                    f"🚨 Patrón de inyección en header '{header_name}': "
                    f"{sanitize_for_log(header_value)}"
                )
                if self.enable_strict_mode:
                    raise HTTPException(
                        status_code=400,
                        detail=f"Header inválido: {header_name}"
                    )
    
    async def _validate_query_params(self, request: Request):
        """Valida query parameters de la URL"""
        for key, value in request.query_params.items():
            # Validar key
            if check_injection_patterns(key):
                logger.warning(
                    f"🚨 Patrón de inyección en query param key: "
                    f"{sanitize_for_log(key)}"
                )
                if self.enable_strict_mode:
                    raise HTTPException(
                        status_code=400,
                        detail=f"Query parameter inválido: {key}"
                    )
            
            # Validar value
            if isinstance(value, str) and check_injection_patterns(value):
                logger.warning(
                    f"🚨 Patrón de inyección en query param '{key}': "
                    f"{sanitize_for_log(value)}"
                )
                if self.enable_strict_mode:
                    raise HTTPException(
                        status_code=400,
                        detail=f"Valor de query parameter inválido: {key}"
                    )
    
    async def _validate_body(self, request: Request):
        """
        Valida el body de la request.
        
        NOTA: Esta validación es adicional a la validación de Pydantic.
        Pydantic valida tipos y formato, este middleware valida patrones peligrosos.
        """
        # Leer body
        body_bytes = await request.body()
        
        if not body_bytes:
            return
        
        # Intentar parsear como JSON
        try:
            content_type = request.headers.get('content-type', '')
            
            if 'application/json' in content_type:
                body_str = body_bytes.decode('utf-8')
                body_data = json.loads(body_str)
                
                # Validar recursivamente todos los strings en el JSON
                self._validate_json_recursive(body_data)
                
            elif 'application/x-www-form-urlencoded' in content_type:
                # Form data
                body_str = body_bytes.decode('utf-8')
                if check_injection_patterns(body_str):
                    logger.warning(
                        f"🚨 Patrón de inyección en form data: "
                        f"{sanitize_for_log(body_str[:100])}"
                    )
                    if self.enable_strict_mode:
                        raise HTTPException(
                            status_code=400,
                            detail="Form data contiene patrones no permitidos"
                        )
            
        except json.JSONDecodeError:
            # No es JSON válido, skip (Pydantic lo manejará)
            pass
        except UnicodeDecodeError:
            # Body no es texto, skip (puede ser archivo binario)
            pass
    
    def _validate_json_recursive(self, data, path: str = "root"):
        """Valida recursivamente un objeto JSON"""
        if isinstance(data, dict):
            for key, value in data.items():
                # Validar key
                if isinstance(key, str) and check_injection_patterns(key):
                    logger.warning(
                        f"🚨 Patrón de inyección en JSON key '{path}.{key}': "
                        f"{sanitize_for_log(key)}"
                    )
                    if self.enable_strict_mode:
                        raise HTTPException(
                            status_code=400,
                            detail=f"JSON key inválido: {path}.{key}"
                        )
                
                # Validar value recursivamente
                self._validate_json_recursive(value, f"{path}.{key}")
        
        elif isinstance(data, list):
            for i, item in enumerate(data):
                self._validate_json_recursive(item, f"{path}[{i}]")
        
        elif isinstance(data, str):
            # Validar string
            if check_injection_patterns(data):
                logger.warning(
                    f"🚨 Patrón de inyección en '{path}': "
                    f"{sanitize_for_log(data)}"
                )
                if self.enable_strict_mode:
                    raise HTTPException(
                        status_code=400,
                        detail=f"Valor inválido en: {path}"
                    )
    
    def _get_client_ip(self, request: Request) -> str:
        """Obtiene la IP real del cliente"""
        # Prioridad a headers de proxy
        forwarded_for = request.headers.get('x-forwarded-for')
        if forwarded_for:
            return forwarded_for.split(',')[0].strip()
        
        real_ip = request.headers.get('x-real-ip')
        if real_ip:
            return real_ip
        
        # Fallback a request.client
        if request.client:
            return request.client.host
        
        return "unknown"
