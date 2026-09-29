"""
Estado de la red LoRaLAN visto desde el servidor.

Sustituye a utils/websocket_manager.py. Sin WebSocket no hay conexiones que
guardar: un nodo está "conectado" si el gateway ha subido algo suyo hace poco,
y un comando para un nodo se deja en la cola de su gateway hasta que éste la
consulte (GET /gateway/commands).

Los códigos de fichaje son los de AppProtocol.h (app::Code): cambiar ambos a la vez.
"""

import time
from collections import defaultdict, deque

from services.aula_service import aulas_estado

# Código del ACK del fichaje. 0 = ACK; cualquier otro = NACK.
CODE_OK = 0
CODE_SIN_CLASE = 1     # 400
CODE_DENEGADO = 2      # 403
CODE_DESCONOCIDO = 3   # 404
CODE_REINTENTAR = 4    # 5xx o cualquier fallo: el nodo lo reenvía

CODIGO_POR_HTTP = {400: CODE_SIN_CLASE, 403: CODE_DENEGADO, 404: CODE_DESCONOCIDO}

# Sin noticias de un nodo en este tiempo -> desconectado. El gateway sondea un
# nodo cada 5 s en ronda, así que con N nodos cada uno responde cada 5*N s.
# ponytail: valor fijo pensado para <= 20 nodos; calcularlo con el nº de nodos si crece.
ONLINE_TIMEOUT_S = 120

MAX_COMANDOS_POR_CONSULTA = 16

# gateway -> cola de comandos pendientes.
# ponytail: en memoria; un reinicio del servidor pierde los comandos no recogidos
# (órdenes de relé que el usuario puede repetir). Pasar a tabla si importa.
_comandos: dict[str, deque] = defaultdict(deque)

# gateway -> último latido (POST /gateway/status)
gateways: dict[str, dict] = {}


def device_id_de(node: int) -> str:
    """Nodo LoRaLAN -> device_id de la tabla nodos ("node-016")."""
    return f"node-{node:03d}"


def node_de(device_id: str) -> int | None:
    """Inverso de device_id_de. None si no es un device_id LoRaLAN."""
    prefijo, _, numero = device_id.partition("-")
    if prefijo != "node" or not numero.isdigit():
        return None
    node = int(numero)
    return node if 1 <= node <= 254 else None


def marcar_visto(device_id: str, gateway: str, **datos) -> dict:
    """Actualiza el estado en memoria de un nodo con lo que acaba de subir."""
    estado = aulas_estado.setdefault(device_id, {"device_id": device_id})
    estado.update(datos)
    estado["gateway"] = gateway
    estado["ultima_vez"] = time.time()
    estado["baja"] = False
    return estado


def conectado(estado: dict, ahora: float | None = None) -> bool:
    ahora = time.time() if ahora is None else ahora
    return (not estado.get("baja", False)) and ahora - estado.get("ultima_vez", 0) < ONLINE_TIMEOUT_S


def encolar_comando(device_id: str, comando: dict) -> bool:
    """
    Deja un comando para el gateway que atiende al nodo. False si el nodo no
    es LoRaLAN o ningún gateway lo ha visto todavía (no se sabe a quién dárselo).
    """
    node = node_de(device_id)
    gateway = aulas_estado.get(device_id, {}).get("gateway")
    if node is None or not gateway:
        return False
    _comandos[gateway].append({"node": node, **comando})
    return True


def recoger_comandos(gateway: str) -> list[dict]:
    cola = _comandos[gateway]
    salida = []
    while cola and len(salida) < MAX_COMANDOS_POR_CONSULTA:
        salida.append(cola.popleft())
    return salida


def estado_aulas(incluir_radio: bool) -> list[dict]:
    """Estado de cada nodo para los paneles, con 'conectado' calculado ahora."""
    ahora = time.time()
    privados = set() if incluir_radio else {"rssi", "snr", "gateway"}
    aulas = []
    for device_id, estado in aulas_estado.items():
        fila = {k: v for k, v in estado.items() if k not in privados}
        fila["conectado"] = conectado(estado, ahora)
        fila["hace_s"] = round(ahora - estado["ultima_vez"]) if "ultima_vez" in estado else None
        aulas.append(fila)
    return aulas


def _reset_para_tests() -> None:
    _comandos.clear()
    gateways.clear()
    aulas_estado.clear()
