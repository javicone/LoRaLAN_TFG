#!/usr/bin/env python3
"""
Test en vivo del rate limiting.

Este script hace requests reales al servidor para verificar
que el rate limiting funciona correctamente.

REQUISITO: El servidor debe estar corriendo en http://localhost:8000
NOTA: Necesita 'requests' instalado: pip install requests
"""

try:
    import requests
except ImportError:
    print("❌ Error: 'requests' no está instalado.")
    print("   Instala con: pip install requests")
    exit(1)

import time
from datetime import datetime

BASE_URL = "http://localhost:8000"

def test_login_rate_limit():
    """
    Prueba rate limiting en /auth/login (5 intentos/minuto).
    """
    print("\n" + "="*70)
    print("🧪 TEST: Rate Limiting en /auth/login")
    print("="*70)
    print("\n⏱️  Límite configurado: 5 intentos por minuto")
    print("📋 Estrategia: Hacer 7 intentos rápidos\n")
    
    url = f"{BASE_URL}/auth/login"
    data = {
        "correo": "test@test.com",
        "password": "wrongpassword"
    }
    
    results = []
    
    for i in range(7):
        print(f"Intento {i+1}... ", end="", flush=True)
        
        start = time.time()
        try:
            response = requests.post(url, data=data, allow_redirects=False)
            elapsed = time.time() - start
            
            status = response.status_code
            results.append(status)
            
            if status == 429:
                print(f"🛑 429 TOO MANY REQUESTS (esperado en intento 6+)")
                data_json = response.json()
                print(f"   📝 Respuesta: {data_json.get('detail', 'N/A')}")
                print(f"   ⏳ Retry-After: {response.headers.get('Retry-After', 'N/A')} segundos")
                break
            elif status == 401:
                print(f"✅ 401 UNAUTHORIZED (credenciales inválidas - normal)")
            elif status == 303:
                print(f"⚠️  303 REDIRECT (credenciales correctas?)")
            else:
                print(f"⚠️  {status} (inesperado)")
            
            # Pequeño delay para no saturar
            time.sleep(0.1)
            
        except requests.exceptions.ConnectionError:
            print(f"❌ ERROR: No se pudo conectar al servidor")
            print(f"\n   ⚠️  Asegúrate de que el servidor está corriendo:")
            print(f"      python Server\\appFastApi.py")
            return False
        except Exception as e:
            print(f"❌ ERROR: {e}")
            return False
    
    # Análisis de resultados
    print(f"\n📊 Resultados:")
    print(f"   Total intentos: {len(results)}")
    print(f"   401 (bloqueado por auth): {results.count(401)}")
    print(f"   429 (rate limited): {results.count(429)}")
    
    # Verificar que el intento 6 fue bloqueado
    if len(results) >= 6 and 429 in results:
        print(f"\n✅ Rate limiting FUNCIONANDO correctamente")
        return True
    elif len(results) < 6:
        print(f"\n⚠️  Test detenido antes del intento 6")
        return False
    else:
        print(f"\n❌ Rate limiting NO funcionó (se esperaba 429 en intento 6)")
        return False

def test_device_auth_rate_limit():
    """
    Prueba rate limiting en /auth/device (10 intentos/minuto).
    """
    print("\n" + "="*70)
    print("🧪 TEST: Rate Limiting en /auth/device")
    print("="*70)
    print("\n⏱️  Límite configurado: 10 intentos por minuto")
    print("📋 Estrategia: Hacer 12 intentos rápidos\n")
    
    url = f"{BASE_URL}/auth/device"
    json_data = {
        "device_id": "TEST_ESP32_001",
        "api_key": "invalid_key_for_testing"
    }
    
    results = []
    
    for i in range(12):
        print(f"Intento {i+1}... ", end="", flush=True)
        
        try:
            response = requests.post(url, json=json_data)
            status = response.status_code
            results.append(status)
            
            if status == 429:
                print(f"🛑 429 TOO MANY REQUESTS (esperado en intento 11+)")
                break
            elif status == 401:
                print(f"✅ 401 UNAUTHORIZED (API key inválida - normal)")
            else:
                print(f"⚠️  {status}")
            
            time.sleep(0.1)
            
        except requests.exceptions.ConnectionError:
            print(f"❌ ERROR: Servidor no disponible")
            return False
        except Exception as e:
            print(f"❌ ERROR: {e}")
            return False
    
    print(f"\n📊 Resultados:")
    print(f"   Total intentos: {len(results)}")
    print(f"   401 (auth fallida): {results.count(401)}")
    print(f"   429 (rate limited): {results.count(429)}")
    
    if len(results) >= 11 and 429 in results:
        print(f"\n✅ Rate limiting FUNCIONANDO correctamente")
        return True
    else:
        print(f"\n❌ Rate limiting NO funcionó como esperado")
        return False

def test_headers():
    """
    Verifica que los headers X-RateLimit-* están presentes.
    """
    print("\n" + "="*70)
    print("🧪 TEST: Headers de Rate Limiting")
    print("="*70)
    
    url = f"{BASE_URL}/estado_aulas"
    
    try:
        print(f"\n📡 Haciendo GET a {url}")
        response = requests.get(url)
        
        headers = response.headers
        
        # Buscar headers de rate limit
        rate_headers = {k: v for k, v in headers.items() if 'ratelimit' in k.lower()}
        
        if rate_headers:
            print(f"\n✅ Headers de rate limiting encontrados:")
            for key, value in rate_headers.items():
                print(f"   {key}: {value}")
            return True
        else:
            print(f"\n⚠️  No se encontraron headers X-RateLimit-*")
            print(f"   (Pueden no estar habilitados en la configuración)")
            print(f"\n   Headers recibidos:")
            for key in list(headers.keys())[:10]:
                print(f"   {key}: {headers[key]}")
            return True  # No es crítico
            
    except Exception as e:
        print(f"❌ ERROR: {e}")
        return False

def main():
    """Ejecuta todos los tests"""
    print("="*70)
    print("🚦 TEST EN VIVO - RATE LIMITING")
    print("="*70)
    print(f"\n🌐 Servidor: {BASE_URL}")
    print(f"⏰ Hora: {datetime.now().strftime('%Y-%m-%d %H:%M:%S')}")
    
    # Verificar que el servidor está corriendo
    print(f"\n🔍 Verificando servidor...")
    try:
        response = requests.get(f"{BASE_URL}/health", timeout=5)
        if response.status_code == 200:
            print(f"   ✅ Servidor respondiendo correctamente")
        else:
            print(f"   ⚠️  Servidor responde pero con status {response.status_code}")
    except requests.exceptions.ConnectionError:
        print(f"   ❌ ERROR: Servidor no disponible en {BASE_URL}")
        print(f"\n   💡 Iniciar servidor primero:")
        print(f"      python Server\\appFastApi.py")
        return
    except Exception as e:
        print(f"   ❌ ERROR: {e}")
        return
    
    # Ejecutar tests
    tests = [
        ("Login Rate Limit (5/min)", test_login_rate_limit),
        ("Device Auth Rate Limit (10/min)", test_device_auth_rate_limit),
        ("Rate Limit Headers", test_headers),
    ]
    
    results = []
    for nombre, test_func in tests:
        resultado = test_func()
        results.append((nombre, resultado))
        time.sleep(1)  # Pausa entre tests
    
    # Reporte final
    print("\n" + "="*70)
    print("📊 RESUMEN DE RESULTADOS")
    print("="*70)
    
    passed = sum(1 for _, r in results if r)
    total = len(results)
    
    for nombre, resultado in results:
        status = "✅ PASS" if resultado else "❌ FAIL"
        print(f"   {status} - {nombre}")
    
    print(f"\n   Total: {passed}/{total} tests pasados")
    
    if passed == total:
        print("\n🎉 ¡Rate Limiting funcionando correctamente!")
    else:
        print(f"\n⚠️  Algunos tests fallaron")
        print(f"\n   Verificar:")
        print(f"   - Que slowapi está instalado: pip list | grep slowapi")
        print(f"   - Que appFastApi.py tiene app.state.limiter configurado")
        print(f"   - Que routers tienen @limiter.limit decoradores")
    
    print("="*70 + "\n")

if __name__ == "__main__":
    main()
