"""
Pruebas de los endpoints HTTP que sustituyen al WebSocket (sin MySQL).

    cd Server
    python -m pytest scripts/test_gateway_panel.py -q

La base de datos se sustituye por funciones falsas: se prueba el contrato con
el gateway (gatewayLoRaLAN/src/bridge.h), la traducción de errores a códigos
ACK/NACK, la cola de comandos y los permisos de los paneles.
"""

import asyncio
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.abspath(__file__))))

import pytest
from fastapi import FastAPI, HTTPException
from fastapi.testclient import TestClient

import routers.gateway as rgw
import routers.panel as rpanel
import services.asistencia_service as asis
from services import gateway_service as gw
from services.auth_service import create_access_token

GW = "gw-rpi-01"


@pytest.fixture
def client(monkeypatch):
    gw._reset_para_tests()
    bd = {"nodos": set(), "aulas": {}}   # device_id -> nombre de aula

    async def registrar_nodo(device_id):
        bd["nodos"].add(device_id)

    async def aula_de_nodo(device_id):
        nombre = bd["aulas"].get(device_id)
        return {"id": 1, "nombre": nombre} if nombre else None

    async def asignar_aula(device_id, nombre):
        bd["aulas"][device_id] = nombre
        return 1

    monkeypatch.setattr(rgw, "registrar_nodo_si_no_existe", registrar_nodo)
    monkeypatch.setattr(rgw, "aula_de_nodo", aula_de_nodo)
    monkeypatch.setattr(rpanel, "asignar_aula_a_nodo", asignar_aula)

    app = FastAPI()
    app.include_router(rgw.router)
    app.include_router(rpanel.router)
    c = TestClient(app)
    c.bd = bd
    return c


def auth_gw(nombre=GW):
    return {"Authorization": "Bearer " + create_access_token({"sub": nombre, "rol": "device"})}


def sesion(rol):
    return {"access_token": create_access_token({"sub": "1", "rol": rol, "correo": f"{rol}@x"})}


def fichar(client, monkeypatch, efecto, uid="04A30FB2"):
    async def registrar(uid_, device_id, timestamp=None):
        if isinstance(efecto, Exception):
            raise efecto
        return {"ok": True}
    monkeypatch.setattr(rgw, "registrar_asistencia", registrar)
    return client.post("/gateway/fichaje", json={"gateway": GW, "node": 16, "uid": uid}, headers=auth_gw())


# --------------------------------------------------------------------------
# Gateway: fichaje -> código del ACK
# --------------------------------------------------------------------------

@pytest.mark.parametrize("efecto, code", [
    (None, gw.CODE_OK),
    (HTTPException(400, "sin clase"), gw.CODE_SIN_CLASE),
    (HTTPException(403, "otro profesor"), gw.CODE_DENEGADO),
    (HTTPException(404, "tarjeta"), gw.CODE_DESCONOCIDO),
    (HTTPException(500, "x"), gw.CODE_REINTENTAR),
    (RuntimeError("BD caída"), gw.CODE_REINTENTAR),
])
def test_fichaje_codigos(client, monkeypatch, efecto, code):
    r = fichar(client, monkeypatch, efecto)
    assert r.status_code == 200
    assert r.json() == {"code": code}


def test_codigos_coinciden_con_app_protocol():
    # AppProtocol.h, enum class app::Code
    assert (gw.CODE_OK, gw.CODE_SIN_CLASE, gw.CODE_DENEGADO, gw.CODE_DESCONOCIDO, gw.CODE_REINTENTAR) == (0, 1, 2, 3, 4)


def test_fichaje_uid_invalido(client, monkeypatch):
    assert fichar(client, monkeypatch, None, uid="04a3").status_code == 422   # corto y minúsculas


def test_gateway_requiere_token_de_dispositivo(client):
    cuerpo = {"gateway": GW, "node": 16, "uid": "04A30FB2"}
    assert client.post("/gateway/fichaje", json=cuerpo).status_code == 401
    staff = {"Authorization": "Bearer " + create_access_token({"sub": "1", "rol": "administrador"})}
    assert client.post("/gateway/fichaje", json=cuerpo, headers=staff).status_code == 403
    # token de otro gateway
    assert client.post("/gateway/fichaje", json=cuerpo, headers=auth_gw("gw-otro")).status_code == 403
    # un nodo WiFi antiguo con api_key no puede hacerse pasar por gateway
    cuerpo_nodo = {**cuerpo, "gateway": "node-002"}
    assert client.post("/gateway/fichaje", json=cuerpo_nodo, headers=auth_gw("node-002")).status_code == 403


# --------------------------------------------------------------------------
# Gateway: uplink -> estado que ven los paneles
# --------------------------------------------------------------------------

def test_uplink_sensores_y_aula_de_bd(client):
    client.bd["aulas"]["node-016"] = "Aula 2"
    r = client.post("/gateway/uplink", headers=auth_gw(), json={
        "gateway": GW, "node": 16, "tipo": "sensores", "temperatura": 21.5, "humedad": 40.1,
        "presion": None, "luz": 320, "presencia": True, "rssi": -60.0, "snr": 9.5})
    assert r.status_code == 200
    assert "node-016" in client.bd["nodos"]

    estado = client.get("/panel/estado", cookies=sesion("administrador")).json()
    aula = estado["aulas"][0]
    assert aula["device_id"] == "node-016" and aula["aula"] == "Aula 2"
    assert aula["temperatura"] == 21.5 and aula["presion"] is None and aula["personas"] == 1
    assert aula["conectado"] is True and aula["rssi"] == -60.0

    # el conserje no ve los datos de radio
    aula_c = client.get("/panel/estado", cookies=sesion("conserje")).json()["aulas"][0]
    assert "rssi" not in aula_c and "gateway" not in aula_c and aula_c["temperatura"] == 21.5

    # radar sin lectura: "N/A" en el panel, no 0 personas
    client.post("/gateway/uplink", headers=auth_gw(), json={
        "gateway": GW, "node": 16, "tipo": "sensores", "presencia": None})
    aula = client.get("/panel/estado", cookies=sesion("administrador")).json()["aulas"][0]
    assert aula["personas"] is None


def test_uplink_baja_y_timeout(client, monkeypatch):
    client.post("/gateway/uplink", headers=auth_gw(), json={"gateway": GW, "node": 7, "tipo": "reles",
                                                              "rele1": True, "rele2": False})
    assert gw.conectado(gw.aulas_estado["node-007"])
    client.post("/gateway/uplink", headers=auth_gw(), json={"gateway": GW, "node": 7, "tipo": "estado",
                                                              "online": False})
    assert not gw.conectado(gw.aulas_estado["node-007"])
    # sin baja pero sin noticias durante más de ONLINE_TIMEOUT_S
    client.post("/gateway/uplink", headers=auth_gw(), json={"gateway": GW, "node": 8, "tipo": "reles",
                                                              "rele1": False, "rele2": False})
    e = gw.aulas_estado["node-008"]
    assert not gw.conectado(e, ahora=e["ultima_vez"] + gw.ONLINE_TIMEOUT_S + 1)


# --------------------------------------------------------------------------
# Panel -> cola del gateway -> GET /gateway/commands
# --------------------------------------------------------------------------

def test_rele_llega_al_gateway_una_vez(client):
    client.post("/gateway/uplink", headers=auth_gw(), json={"gateway": GW, "node": 16, "tipo": "reles",
                                                              "rele1": False, "rele2": False})
    r = client.post("/panel/rele", cookies=sesion("conserje"),
                    json={"device_id": "node-016", "relay_num": 2, "relay_state": True})
    assert r.status_code == 202

    cmds = client.get(f"/gateway/commands?gateway={GW}", headers=auth_gw()).json()
    assert cmds == {"commands": [{"node": 16, "tipo": "rele", "rele": 2, "on": True}]}
    assert client.get(f"/gateway/commands?gateway={GW}", headers=auth_gw()).json() == {"commands": []}


def test_rele_a_nodo_desconocido(client):
    r = client.post("/panel/rele", cookies=sesion("administrador"),
                    json={"device_id": "node-099", "relay_num": 1, "relay_state": True})
    assert r.status_code == 404


def test_permisos_panel(client):
    client.post("/gateway/uplink", headers=auth_gw(), json={"gateway": GW, "node": 16, "tipo": "reles",
                                                              "rele1": False, "rele2": False})
    assert client.get("/panel/estado").status_code == 401
    assert client.get("/panel/estado", cookies=sesion("profesor")).status_code == 403
    # reset y aula: solo administrador
    assert client.post("/panel/reset", cookies=sesion("conserje"), json={"device_id": "node-016"}).status_code == 403
    assert client.post("/panel/reset", cookies=sesion("administrador"), json={"device_id": "node-016"}).status_code == 202
    assert client.post("/panel/aula", cookies=sesion("conserje"),
                       json={"device_id": "node-016", "nombre": "Aula 3"}).status_code == 403


def test_asignar_aula(client):
    r = client.post("/panel/aula", cookies=sesion("administrador"), json={"device_id": "node-016", "nombre": "Aula 3"})
    assert r.status_code == 200 and client.bd["aulas"]["node-016"] == "Aula 3"
    # el mismo nombre para otro nodo: conflicto
    r = client.post("/panel/aula", cookies=sesion("administrador"), json={"device_id": "node-017", "nombre": "aula 3"})
    assert r.status_code == 409
    r = client.post("/panel/aula", cookies=sesion("administrador"), json={"device_id": "node-017", "nombre": "../etc"})
    assert r.status_code == 422


# --------------------------------------------------------------------------
# Servicio de asistencia: el reenvío de un fichaje no duplica
# --------------------------------------------------------------------------

def test_fichaje_reenviado_no_duplica(monkeypatch):
    insertados = []
    registrados = set()

    async def uid(u): return 5
    async def aula(d): return 1
    async def clase(a, f): return {"id": 9, "profesor_id": 5, "asignatura": "Redes"}
    async def existe(u, a, c): return (u, a, c) in registrados
    async def insertar(u, a, f, c):
        insertados.append((u, a, c))
        registrados.add((u, a, c))

    monkeypatch.setattr(asis, "get_usuario_id_by_uid", uid)
    monkeypatch.setattr(asis, "get_aula_id_by_device_id", aula)
    monkeypatch.setattr(asis, "get_clase_actual_by_aula_and_time", clase)
    monkeypatch.setattr(asis, "existe_asistencia_en_clase", existe)
    monkeypatch.setattr(asis, "insert_asistencia", insertar)
    gw.aulas_estado["node-016"] = {"aula": "Aula 2"}

    asyncio.run(asis.registrar_asistencia("04A30FB2", "node-016"))
    asyncio.run(asis.registrar_asistencia("04A30FB2", "node-016"))   # reenvío tras REINTENTAR
    assert insertados == [(5, 1, 9)]
    gw.aulas_estado.clear()


def test_fallo_de_bd_en_aula_no_es_nack_definitivo(client, monkeypatch):
    """Un error de BD al buscar el aula debe dar REINTENTAR, no DESCONOCIDO."""
    import repositories.nodos as nodos

    async def uid(u): return 5
    async def aula_rota(device_id): raise RuntimeError("MySQL caído")

    monkeypatch.setattr(asis, "get_usuario_id_by_uid", uid)
    monkeypatch.setattr(nodos, "aula_de_nodo", aula_rota)   # la usa get_aula_id_by_device_id
    monkeypatch.setattr(rgw, "registrar_asistencia", asis.registrar_asistencia)

    r = client.post("/gateway/fichaje", json={"gateway": GW, "node": 16, "uid": "04A30FB2"}, headers=auth_gw())
    assert r.json() == {"code": gw.CODE_REINTENTAR}


def test_app_completa_devuelve_el_codigo_de_error_real():
    """
    Los manejadores de error de appFastApi renderizan error.html. Con la firma
    antigua de TemplateResponse, Starlette >= 1.0 lanzaba TypeError y todo
    401/403/404 salía como 500: el gateway no renovaba el token (solo lo hace
    ante un 401) y las páginas web no cargaban.
    """
    import appFastApi
    c = TestClient(appFastApi.app, raise_server_exceptions=False)   # sin "with": no conecta a la BD
    assert c.get("/no-existe").status_code == 404
    assert c.post("/gateway/fichaje", json={"gateway": GW, "node": 16, "uid": "04A30FB2"}).status_code == 401
    assert c.get("/").status_code == 200
