from pydantic import BaseModel
from datetime import datetime

class Clase(BaseModel):
    asignatura: str
    hora_inicio: datetime
    hora_fin: datetime

class ClaseResponse(BaseModel):
    aula: str
    asignatura: str | None
    hora_inicio: str | None
    hora_fin: str | None
    estado: str  # "en_curso", "proxima", "sin_clase"
    fecha: str | None = None