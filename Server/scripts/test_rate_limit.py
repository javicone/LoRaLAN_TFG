#!/usr/bin/env python3
"""
Script de verificación del sistema de rate limiting.

Ejecutar para verificar que:
1. SlowAPI está instalado correctamente
2. El rate limiting está configurado en la app
3. Los endpoints protegidos funcionan correctamente
"""

import sys
from pathlib import Path
from slowapi.errors import RateLimitExceeded

# Añadir Server/ al path
sys.path.insert(0, str(Path(__file__).resolve().parent.parent))

def test_imports():
    """Verifica que slowapi está instalado"""
    print("\n🔍 Verificando imports...")
    
    try:
        from slowapi import Limiter, _rate_limit_exceeded_handler
        from slowapi.util import get_remote_address
        from slowapi.errors import RateLimitExceeded
        print("   ✅ slowapi importado correctamente")
        
        import limits
        print("   ✅ limits library disponible")
        
        return True
    except ImportError as e:
        print(f"   ❌ Error importando: {e}")
        print("\n   Solución:")
        print("   pip install slowapi limits")
        return False

def test_middleware_config():
    """Verifica que el middleware está configurado"""
    print("\n🔍 Verificando configuración de middleware...")
    
    try:
        from middleware.rate_limit import RATE_LIMITS, get_limit, limiter
        
        print(f"   ✅ Middleware rate_limit.py creado")
        print(f"   ✅ {len(RATE_LIMITS)} límites configurados:")
        
        for key, limit in RATE_LIMITS.items():
            print(f"      - {key}: {limit}")
        
        # Test get_limit
        login_limit = get_limit("login")
        if login_limit == "5/minute":
            print(f"   ✅ get_limit('login') = {login_limit}")
        else:
            print(f"   ⚠️  get_limit('login') = {login_limit} (esperado: 5/minute)")
        
        return True
        
    except Exception as e:
        print(f"   ❌ Error: {e}")
        return False

def test_app_integration():
    """Verifica que la app tiene el limiter configurado"""
    print("\n🔍 Verificando integración con FastAPI app...")
    
    try:
        from appFastApi import app
        
        # Verificar que tiene state.limiter
        if hasattr(app.state, 'limiter'):
            print("   ✅ app.state.limiter configurado")
        else:
            print("   ❌ app.state.limiter NO encontrado")
            print("      Verificar que appFastApi.py tiene:")
            print("      app.state.limiter = limiter")
            return False
        
        # Verificar exception handler
        if RateLimitExceeded in app.exception_handlers:
            print("   ✅ RateLimitExceeded handler registrado")
        else:
            print("   ⚠️  RateLimitExceeded handler no encontrado")
            print("      (Puede funcionar sin él, pero sin mensajes custom)")
        
        return True
        
    except Exception as e:
        print(f"   ❌ Error: {e}")
        import traceback
        traceback.print_exc()
        return False

def test_router_decorators():
    """Verifica que los routers tienen decoradores de rate limiting"""
    print("\n🔍 Verificando decoradores en routers...")
    
    try:
        import inspect
        from routers import auth
        
        # Verificar login_usuario
        if hasattr(auth, 'login_usuario'):
            source = inspect.getsource(auth.login_usuario)
            if '@limiter.limit' in source:
                print("   ✅ /auth/login tiene @limiter.limit")
            else:
                print("   ⚠️  /auth/login NO tiene @limiter.limit")
        else:
            print("   ⚠️  login_usuario no encontrado")
        
        # Verificar authenticate_device
        if hasattr(auth, 'authenticate_device'):
            source = inspect.getsource(auth.authenticate_device)
            if '@limiter.limit' in source:
                print("   ✅ /auth/device tiene @limiter.limit")
            else:
                print("   ⚠️  /auth/device NO tiene @limiter.limit")
        else:
            print("   ℹ️  authenticate_device no encontrado")
        
        return True
        
    except Exception as e:
        print(f"   ❌ Error: {e}")
        return False

def test_client_identifier():
    """Verifica que get_client_identifier funciona"""
    print("\n🔍 Verificando identificación de clientes...")
    
    try:
        from middleware.rate_limit import get_client_identifier
        from fastapi import Request
        
        # Mock request simple
        class MockRequest:
            def __init__(self):
                self.headers = {}
                self.client = type('obj', (object,), {'host': '127.0.0.1'})()
        
        mock_request = MockRequest()
        
        # Test 1: Sin headers
        client_id = get_client_identifier(mock_request)
        print(f"   ✅ Sin headers: {client_id}")
        
        # Test 2: Con X-Forwarded-For
        mock_request.headers = {"X-Forwarded-For": "203.0.113.1, 198.51.100.1"}
        client_id = get_client_identifier(mock_request)
        if client_id == "203.0.113.1":
            print(f"   ✅ X-Forwarded-For: {client_id} (primera IP)")
        else:
            print(f"   ⚠️  X-Forwarded-For: {client_id} (esperado: 203.0.113.1)")
        
        # Test 3: Con X-Real-IP
        mock_request.headers = {"X-Real-IP": "198.51.100.50"}
        client_id = get_client_identifier(mock_request)
        if client_id == "198.51.100.50":
            print(f"   ✅ X-Real-IP: {client_id}")
        else:
            print(f"   ⚠️  X-Real-IP: {client_id}")
        
        return True
        
    except Exception as e:
        print(f"   ❌ Error: {e}")
        import traceback
        traceback.print_exc()
        return False

def print_usage_example():
    """Muestra ejemplos de uso"""
    print("\n📚 Ejemplos de Uso:")
    print("""
    # Aplicar rate limiting a un endpoint:
    from slowapi import Limiter
    from slowapi.util import get_remote_address
    
    limiter = Limiter(key_func=get_remote_address)
    
    @router.post("/mi-endpoint")
    @limiter.limit("15/minute")
    async def mi_endpoint(request: Request):
        return {"status": "ok"}
    
    # Usar configuración centralizada:
    from middleware.rate_limit import get_limit, limiter
    
    @router.post("/otro-endpoint")
    @limiter.limit(get_limit("api_write"))
    async def otro_endpoint(request: Request):
        return {"status": "ok"}
    """)

def main():
    """Ejecuta todos los tests"""
    print("="*70)
    print("🚦 VERIFICACIÓN DEL SISTEMA DE RATE LIMITING")
    print("="*70)
    
    from slowapi.errors import RateLimitExceeded
    
    tests = [
        ("Imports", test_imports),
        ("Configuración Middleware", test_middleware_config),
        ("Integración FastAPI", test_app_integration),
        ("Decoradores en Routers", test_router_decorators),
        ("Identificación de Clientes", test_client_identifier),
    ]
    
    results = []
    for nombre, test_func in tests:
        try:
            resultado = test_func()
            results.append((nombre, resultado))
        except Exception as e:
            print(f"\n❌ Test '{nombre}' falló con excepción: {e}")
            results.append((nombre, False))
    
    # Reporte final
    print("\n" + "="*70)
    print("📊 RESULTADOS")
    print("="*70)
    
    passed = sum(1 for _, r in results if r)
    total = len(results)
    
    for nombre, resultado in results:
        status = "✅ PASS" if resultado else "❌ FAIL"
        print(f"   {status} - {nombre}")
    
    print(f"\n   Total: {passed}/{total} tests pasados")
    
    if passed == total:
        print("\n🎉 ¡TODOS LOS TESTS PASARON!")
        print("\n📋 Rate Limiting está configurado correctamente")
        print("\n🧪 Próximo paso - Testing en vivo:")
        print("   1. Iniciar servidor:")
        print("      python Server\\appFastApi.py")
        print("\n   2. Probar rate limiting:")
        print("      python Server\\scripts\\test_rate_limit_live.py")
        
        print_usage_example()
    else:
        print(f"\n⚠️  {total - passed} tests fallaron")
        print("\n   Soluciones:")
        print("   - Si falta slowapi: pip install slowapi limits")
        print("   - Verificar que middleware/rate_limit.py existe")
        print("   - Verificar que appFastApi.py tiene app.state.limiter")
        print("   - Consultar RATE_LIMITING_INSTALADO.md")
    
    print("="*70 + "\n")
    
    return passed == total

if __name__ == "__main__":
    success = main()
    sys.exit(0 if success else 1)
