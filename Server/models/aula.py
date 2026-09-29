from pydantic import BaseModel, validator, Field
from typing import Optional

class DatosAula(BaseModel):
    aula: str = Field(..., min_length=3, max_length=50, strip_whitespace=True)
    temperatura: float = Field(..., ge=-50.0, le=100.0, description="Temperatura en °C")
    luz: float = Field(..., ge=0.0, le=100000.0, description="Iluminación en lux")
    presion: float = Field(..., ge=800.0, le=1200.0, description="Presión en hPa")
    humedad: float = Field(..., ge=0.0, le=100.0, description="Humedad relativa %")
    personas: int = Field(..., ge=0, le=500, description="Número de personas")
    clase_en_curso: bool
    
    @validator('aula')
    def aula_name_format(cls, v):
        """Validar nombre de aula (prevenir inyección)"""
        v = v.strip()
        # Solo letras, números, espacios, guiones, puntos y guiones bajos
        if not v.replace(' ', '').replace('-', '').replace('_', '').replace('.', '').isalnum():
            raise ValueError('Nombre de aula contiene caracteres no permitidos')
        # Prevenir path traversal
        if '..' in v or '/' in v or '\\' in v:
            raise ValueError('Nombre de aula contiene caracteres no permitidos')
        return v
    
    class Config:
        json_schema_extra = {
            "example": {
                "aula": "Aula_101",
                "temperatura": 22.5,
                "luz": 350.0,
                "presion": 1013.25,
                "humedad": 45.0,
                "personas": 25,
                "clase_en_curso": True
            }
        }

class ControlRelay(BaseModel):
    aula: str = Field(..., min_length=3, max_length=50, strip_whitespace=True)
    aerotermia: bool
    iluminacion: bool
    
    @validator('aula')
    def aula_name_format(cls, v):
        """Validar nombre de aula (prevenir inyección)"""
        v = v.strip()
        if not v.replace(' ', '').replace('-', '').replace('_', '').replace('.', '').isalnum():
            raise ValueError('Nombre de aula contiene caracteres no permitidos')
        if '..' in v or '/' in v or '\\' in v:
            raise ValueError('Nombre de aula contiene caracteres no permitidos')
        return v

class EstadoAula(BaseModel):
    temperatura: float
    humedad: float
    presion: float
    luz: float
    personas: int
    clase_en_curso: bool
    relay: bool