"""
API HTTP de los paneles de administración y conserjería. Sustituye a los
WebSocket /ws/admin y /ws/conserje: el navegador consulta GET /panel/estado
cada pocos segundos y envía las órdenes con POST.

Las órdenes a un nodo no son inmediatas: quedan en la cola de su gateway, que
las recoge en su siguiente consulta y las transmite por radio. El panel ve el
cambio cuando el nodo confirma el nuevo estado de los relés.
"""

import logging
import time

from fastapi import APIRouter, Depends, HTTPException, Request, status
from pydantic import BaseModel, Field, field_validator

from repositories.nodos import asignar_aula_a_nodo
from routers.auth import verificar_token_web
from services import gateway_service as gw
from services.aula_service import verificar_nombre_aula_disponible

logger = logging.getLogger(__name__)

router = APIRouter(prefix="/panel", tags=["panel"])

ADMIN = "administrador"
STAFF = {ADMIN, "conserje"}


async def staff(request: Request) -> dict:
    payload = await verificar_token_web(request)   # 401 sin sesión válida
    if payload.get("rol") not in STAFF:
        raise HTTPException(status.HTTP_403_FORBIDDEN, "Rol no autorizado")
    return payload


async def admin(payload: dict = Depends(staff)) -> dict:
    if payload.get("rol") != ADMIN:
        raise HTTPException(status.HTTP_403_FORBIDDEN, "Solo administradores")
    return payload


class ReleIn(BaseModel):
    device_id: str = Field(..., min_length=1, max_length=50)
    relay_num: int = Field(..., ge=1, le=2)
    relay_state: bool


class ResetIn(BaseModel):
    device_id: str = Field(..., min_length=1, max_length=50)


class AulaIn(BaseModel):
    device_id: str = Field(..., min_length=1, max_length=50)
    nombre: str = Field(..., min_length=3, max_length=50)

    @field_validator("nombre")
    @classmethod
    def nombre_valido(cls, v: str) -> str:
        v = v.strip()
        # Solo letras, números, espacios, guiones, puntos y guiones bajos
        if not v.replace(" ", "").replace("-", "").replace("_", "").replace(".", "").isalnum() or ".." in v:
            raise ValueError("Nombre de aula con caracteres no permitidos")
        return v


def _encolar(device_id: str, comando: dict) -> dict:
    if not gw.encolar_comando(device_id, comando):
        raise HTTPException(status.HTTP_404_NOT_FOUND,
                            f"{device_id} no es un nodo LoRaLAN visto por ningún gateway")
    return {"encolado": True, "mensaje": "Orden enviada al gateway"}


@router.get("/estado")
async def estado(payload: dict = Depends(staff)):
    es_admin = payload.get("rol") == ADMIN
    respuesta = {"aulas": gw.estado_aulas(incluir_radio=es_admin)}
    if es_admin:
        ahora = time.time()
        respuesta["gateways"] = [
            {**g, "hace_s": round(ahora - g["ultima_vez"])} for g in gw.gateways.values()
        ]
    return respuesta


@router.post("/rele", status_code=status.HTTP_202_ACCEPTED)
async def rele(r: ReleIn, payload: dict = Depends(staff)):
    logger.info("%s: rele%d=%s en %s", payload.get("correo"), r.relay_num, r.relay_state, r.device_id)
    return _encolar(r.device_id, {"tipo": "rele", "rele": r.relay_num, "on": r.relay_state})


@router.post("/reset", status_code=status.HTTP_202_ACCEPTED)
async def reset(r: ResetIn, payload: dict = Depends(admin)):
    logger.info("%s: reset de %s", payload.get("correo"), r.device_id)
    return _encolar(r.device_id, {"tipo": "reset"})


@router.post("/aula")
async def asignar_aula(a: AulaIn, payload: dict = Depends(admin)):
    """Asigna el aula del nodo. Antes se guardaba en el ESP32; ahora vive en BD."""
    disponible, otro = verificar_nombre_aula_disponible(a.nombre, a.device_id)
    if not disponible:
        raise HTTPException(status.HTTP_409_CONFLICT, f"'{a.nombre}' ya está asignada a {otro}")
    if await asignar_aula_a_nodo(a.device_id, a.nombre) is None:
        raise HTTPException(status.HTTP_500_INTERNAL_SERVER_ERROR, "No se pudo guardar el aula")
    gw.aulas_estado.setdefault(a.device_id, {"device_id": a.device_id})["aula"] = a.nombre
    logger.info("%s: %s asignado al aula '%s'", payload.get("correo"), a.device_id, a.nombre)
    return {"ok": True}
