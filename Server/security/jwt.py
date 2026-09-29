"""
Módulo JWT para Dispositivos IoT - RS256 (Asimétrico)

Este módulo implementa autenticación con RS256 para dispositivos ESP32.
Para entender por qué usamos RS256 aquí y HS256 en sesiones web,
consulta: DOCS/ARQUITECTURA_JWT_DUAL.md

RS256 es crítico para dispositivos IoT porque:
- Dispositivos físicamente vulnerables a robo
- Solo almacenan la clave PÚBLICA (no pueden firmar tokens falsos)
- Aunque extraigan el firmware, no comprometen la seguridad
- La clave PRIVADA permanece segura en el servidor
"""

from datetime import datetime, timedelta, timezone
from fastapi import HTTPException, status, Depends
from jose import jwt, JWTError
from pathlib import Path
from typing import Literal

ALG = "RS256"
KEYS_DIR = Path(__file__).resolve().parents[1] / "keys"
PRIV = (KEYS_DIR / "jwt_priv.pem").read_text(encoding="utf-8")
PUB = (KEYS_DIR / "jwt_pub.pem").read_text(encoding="utf-8")
AUDIENCE = "api"

def issue_token(sub: str, scope: Literal["device","admin","teacher","conserje"]="device", exp_seconds: int = 3600) -> str:
    now = datetime.now(timezone.utc)
    payload = {
        "sub": sub,
        "scope": scope,
        "aud": AUDIENCE,
        "iat": int(now.timestamp()),
        "exp": int((now + timedelta(seconds=exp_seconds)).timestamp()),
    }
    return jwt.encode(payload, PRIV, algorithm=ALG)

def verify_token(token: str, required_scope: str = "device") -> dict:
    try:
        payload = jwt.decode(token, PUB, algorithms=[ALG], audience=AUDIENCE)
        if payload.get("scope") != required_scope:
            raise HTTPException(status_code=status.HTTP_403_FORBIDDEN, detail="invalid scope")
        return payload
    except JWTError as e:
        raise HTTPException(status_code=status.HTTP_401_UNAUTHORIZED, detail="invalid token") from e

def bearer_auth(authorization: str | None) -> dict:
    if not authorization or not authorization.lower().startswith("bearer "):
        raise HTTPException(status_code=status.HTTP_401_UNAUTHORIZED, detail="missing bearer token")
    token = authorization.split(" ",1)[1].strip()
    return verify_token(token, required_scope="device")

def DevicePrincipal(payload: dict = Depends(bearer_auth)) -> str:
    return payload["sub"]