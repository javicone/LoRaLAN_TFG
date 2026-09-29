"""
Errores: navegador (Accept: text/html) -> error.html con el mensaje; fetch -> JSON {"detail"}.
Sin BD. Uso (desde Server/): python -m pytest scripts/test_errores.py
"""
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent.parent))

from fastapi.testclient import TestClient

import appFastApi

c = TestClient(appFastApi.app, raise_server_exceptions=False)   # sin "with": no conecta a la BD


def test_navegador_ve_error_html_con_mensaje():
    r = c.get("/no-existe", headers={"accept": "text/html"})
    assert r.status_code == 404
    assert "No hemos podido encontrar" in r.text and "Código de error: 404" in r.text


def test_fetch_recibe_json_con_detail():
    r = c.get("/panel/estado")
    assert r.status_code == 401 and r.json()["detail"].startswith("No se encontró la sesión")


def test_api_pedida_por_navegador_da_html():
    r = c.get("/api/v1/aulas-crud", headers={"accept": "text/html"})
    assert r.status_code == 401 and "<html" in r.text.lower()
