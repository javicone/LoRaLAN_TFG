"""
Endpoints del gateway LoRaLAN (Raspberry Pi). Sustituyen al WebSocket /ws.

El gateway es quien pregunta: sube lo que recibe por radio y recoge los
comandos pendientes a su ritmo. Así es él quien controla cuánto transmite
por radio (ciclo de trabajo), y no hay conexiones abiertas ni keep-alive.

Contrato (el mismo que gatewayLoRaLAN/src/bridge.h):

  POST /gateway/fichaje   {"gateway","node","uid"}          -> {"code": 0..4}
  POST /gateway/uplink    {"gateway","node","tipo":"sensores"|"reles"|"estado", ...}
  GET  /gateway/commands?gateway=gw                          -> {"commands":[...]}
  POST /gateway/status    {"gateway","nodes","push","poll","dropped","duty_pct"}

Autenticación: JWT de dispositivo (POST /auth/device, rol "device"). El nombre
de gateway del cuerpo debe coincidir con el del token.
"""

import logging
import time
from typing import Literal, Optional

from fastapi import APIRouter, Depends, Header, HTTPException, Query, status
from pydantic import BaseModel, Field

from repositories.nodos import aula_de_nodo, registrar_nodo_si_no_existe
from services import gateway_service as gw
from services.asistencia_service import registrar_asistencia
from services.auth_service import decode_access_token

logger = logging.getLogger(__name__)

router = APIRouter(prefix="/gateway", tags=["gateway"])


# ============================================================================
# Autenticación
# ============================================================================

def gateway_autenticado(authorization: Optional[str] = Header(None)) -> str:
    """Devuelve el nombre del gateway (sub del token)."""
    if not authorization or not authorization.startswith("Bearer "):
        raise HTTPException(status.HTTP_401_UNAUTHORIZED, "Falta Authorization: Bearer")
    datos = decode_access_token(authorization.split(" ", 1)[1])   # 401 si no vale
    if datos.rol != "device":
        raise HTTPException(status.HTTP_403_FORBIDDEN, "Se requiere token de dispositivo")
    # Los nodos ESP32 del sistema WiFi (node-NNN) conservan api_key y pueden
    # obtener un token "device": no deben poder hacerse pasar por un gateway.
    if gw.node_de(datos.id) is not None:
        raise HTTPException(status.HTTP_403_FORBIDDEN, "Un nodo no puede actuar como gateway")
    return datos.id


def _mismo_gateway(token_gw: str, cuerpo_gw: str) -> None:
    if token_gw != cuerpo_gw:
        raise HTTPException(status.HTTP_403_FORBIDDEN, "El gateway no coincide con el token")


# ============================================================================
# Modelos
# ============================================================================

Gateway = Field(..., min_length=1, max_length=64)
Nodo = Field(..., ge=1, le=254)


class FichajeIn(BaseModel):
    gateway: str = Gateway
    node: int = Nodo
    # UID NFC de 4..10 bytes en hexadecimal (AppProtocol.h, uidToHex)
    uid: str = Field(..., pattern=r"^[0-9A-F]{8,20}$")


class UplinkIn(BaseModel):
    gateway: str = Gateway
    node: int = Nodo
    tipo: Literal["sensores", "reles", "estado"]
    # sensores (null = sensor sin lectura)
    temperatura: Optional[float] = Field(None, ge=-50, le=100)
    humedad: Optional[float] = Field(None, ge=0, le=100)
    presion: Optional[float] = Field(None, ge=300, le=1200)
    luz: Optional[float] = Field(None, ge=0, le=65535)
    presencia: Optional[bool] = None
    # reles
    rele1: Optional[bool] = None
    rele2: Optional[bool] = None
    # estado
    online: Optional[bool] = None
    # radio
    rssi: Optional[float] = Field(None, ge=-200, le=50)
    snr: Optional[float] = Field(None, ge=-50, le=50)


class StatusIn(BaseModel):
    gateway: str = Gateway
    nodes: int = Field(..., ge=0, le=255)
    push: int = Field(0, ge=0)
    poll: int = Field(0, ge=0)
    dropped: int = Field(0, ge=0)
    duty_pct: float = Field(0.0, ge=0.0, le=100.0)


# ============================================================================
# Endpoints
# ============================================================================

@router.post("/fichaje")
async def fichaje(f: FichajeIn, token_gw: str = Depends(gateway_autenticado)):
    """
    Registra el fichaje y devuelve el código que el gateway mete en el ACK.
    Siempre 200: el resultado va en "code", no en el estado HTTP.
    """
    _mismo_gateway(token_gw, f.gateway)
    device_id = gw.device_id_de(f.node)
    gw.marcar_visto(device_id, f.gateway)

    try:
        await registrar_asistencia(f.uid, device_id)
        code = gw.CODE_OK
    except HTTPException as exc:
        code = gw.CODIGO_POR_HTTP.get(exc.status_code, gw.CODE_REINTENTAR)
        logger.info("Fichaje %s en %s rechazado: %s", f.uid, device_id, exc.detail)
    except Exception:
        # BD caída, webservice, etc.: que el nodo lo reintente más tarde.
        logger.exception("Error registrando fichaje %s en %s", f.uid, device_id)
        code = gw.CODE_REINTENTAR

    return {"code": code}


@router.post("/uplink")
async def uplink(u: UplinkIn, token_gw: str = Depends(gateway_autenticado)):
    _mismo_gateway(token_gw, u.gateway)
    device_id = gw.device_id_de(u.node)

    if u.tipo == "sensores":
        datos = u.model_dump(include={"temperatura", "humedad", "presion", "luz", "rssi", "snr"})
        # el panel muestra "personas"; sin lectura del radar -> None ("N/A"), no 0
        datos["personas"] = None if u.presencia is None else int(u.presencia)
        estado = gw.marcar_visto(device_id, u.gateway, **datos)
    elif u.tipo == "reles":
        estado = gw.marcar_visto(device_id, u.gateway, relay1=bool(u.rele1), relay2=bool(u.rele2),
                                 rssi=u.rssi, snr=u.snr)
    else:  # estado: alta o baja del nodo en la red LoRaLAN
        estado = gw.marcar_visto(device_id, u.gateway)
        estado["baja"] = not u.online

    if "aula" not in estado:
        # Primera noticia del nodo desde que arrancó el servidor: alta en BD y
        # nombre de su aula, si tiene.
        await registrar_nodo_si_no_existe(device_id)
        aula = await aula_de_nodo(device_id)
        estado["aula"] = aula["nombre"] if aula else None

    return {"ok": True}


@router.get("/commands")
async def commands(gateway: str = Query(..., min_length=1, max_length=64),
                   token_gw: str = Depends(gateway_autenticado)):
    _mismo_gateway(token_gw, gateway)
    return {"commands": gw.recoger_comandos(gateway)}


@router.post("/status")
async def reportar_estado(s: StatusIn, token_gw: str = Depends(gateway_autenticado)):
    _mismo_gateway(token_gw, s.gateway)
    gw.gateways[s.gateway] = {**s.model_dump(), "ultima_vez": time.time()}
    if s.duty_pct > 80.0:
        logger.warning("Gateway %s al %.1f%% de su ciclo de trabajo", s.gateway, s.duty_pct)
    if s.dropped > 0:
        logger.warning("Gateway %s ha descartado %d eventos por cola llena", s.gateway, s.dropped)
    return {"ok": True}
