from pydantic import BaseModel, Field, validator
from datetime import datetime
from typing import Optional
import re

class RegistroAsistencia(BaseModel):
    uid: str = Field(..., min_length=8, max_length=20, strip_whitespace=True)
    device_id: str = Field(..., min_length=3, max_length=50, strip_whitespace=True)
    timestamp: Optional[datetime] = Field(default=None, description="Timestamp del fichaje (opcional, se usa el del servidor si no se proporciona)")
    
    @validator('uid')
    def uid_format(cls, v):
        """Validar formato UID hexadecimal de tarjeta NFC"""
        v = v.upper().strip()
        if not re.match(r'^[0-9A-F]+$', v):
            raise ValueError('UID debe ser hexadecimal (0-9, A-F)')
        return v
    
    @validator('device_id')
    def device_id_format(cls, v):
        """Validar formato de device_id (solo alfanumérico, guiones y guiones bajos)"""
        v = v.strip()
        if not re.match(r'^[a-zA-Z0-9_-]+$', v):
            raise ValueError('device_id debe ser alfanumérico (guiones y guiones bajos permitidos)')
        # Prevenir path traversal y command injection
        if any(pattern in v for pattern in ['..', '/', '\\', '$', '`', '|', '&', ';']):
            raise ValueError('device_id contiene caracteres no permitidos')
        return v

class AsistenciaResponse(BaseModel):
    mensaje: str
    usuario_id: int
    aula_id: int
    clase_id: Optional[int] = None
    asignatura: Optional[str] = None
    fecha_hora: datetime