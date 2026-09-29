#!/usr/bin/env python3
"""
Script de verificación del sistema de hashing de contraseñas.

Ejecutar para verificar que:
1. El módulo security.password funciona correctamente
2. Bcrypt está instalado y operativo
3. Las funciones de hash/verify funcionan como esperado
"""

import sys
from pathlib import Path

# Añadir Server/ al path
sys.path.insert(0, str(Path(__file__).resolve().parent.parent))

def test_imports():
    """Verifica que todos los imports necesarios están disponibles"""
    print("\n🔍 Verificando imports...")
    
    try:
        from security.password import hash_password, verify_password, is_hashed, needs_rehash
        print("   ✅ security.password importado correctamente")
        return True
    except ImportError as e:
        print(f"   ❌ Error importando security.password: {e}")
        return False

def test_bcrypt_available():
    """Verifica que bcrypt está instalado"""
    print("\n🔍 Verificando bcrypt...")
    
    try:
        import bcrypt
        print(f"   ✅ bcrypt instalado: versión {bcrypt.__version__}")
        return True
    except ImportError:
        print("   ❌ bcrypt NO está instalado")
        print("\n   Solución:")
        print("   pip install passlib[bcrypt]")
        return False

def test_hash_verify():
    """Prueba las funciones de hash y verificación"""
    print("\n🔍 Probando hash y verificación...")
    
    try:
        from security.password import hash_password, verify_password
        
        # Test 1: Hash básico
        password = "TestPassword123!"
        hashed = hash_password(password)
        
        if not hashed.startswith("$2b$"):
            print(f"   ❌ Hash no tiene formato bcrypt: {hashed[:20]}...")
            return False
        
        print(f"   ✅ Hash generado correctamente:")
        print(f"      Original: {password}")
        print(f"      Hash:     {hashed[:40]}...")
        
        # Test 2: Verificación correcta
        if not verify_password(password, hashed):
            print("   ❌ Verificación falló con contraseña correcta")
            return False
        
        print("   ✅ Verificación exitosa con contraseña correcta")
        
        # Test 3: Verificación incorrecta
        if verify_password("WrongPassword", hashed):
            print("   ❌ Verificación pasó con contraseña incorrecta")
            return False
        
        print("   ✅ Verificación rechazó contraseña incorrecta")
        
        # Test 4: Hashes diferentes para misma contraseña (salt)
        hashed2 = hash_password(password)
        if hashed == hashed2:
            print("   ⚠️  Dos hashes iguales (salt no está funcionando)")
            return False
        
        print("   ✅ Salt funcionando (hashes diferentes para misma contraseña)")
        
        return True
        
    except Exception as e:
        print(f"   ❌ Error durante las pruebas: {e}")
        import traceback
        traceback.print_exc()
        return False

def test_is_hashed():
    """Prueba la función de detección de hashes"""
    print("\n🔍 Probando detección de hashes...")
    
    try:
        from security.password import is_hashed, hash_password
        
        # Test 1: Detectar hash bcrypt
        bcrypt_hash = hash_password("test")
        if not is_hashed(bcrypt_hash):
            print(f"   ❌ No detectó hash bcrypt: {bcrypt_hash[:20]}...")
            return False
        
        print("   ✅ Detecta hashes bcrypt correctamente")
        
        # Test 2: Rechazar texto plano
        if is_hashed("plaintext_password"):
            print("   ❌ Detectó texto plano como hash")
            return False
        
        print("   ✅ Rechaza texto plano correctamente")
        
        return True
        
    except Exception as e:
        print(f"   ❌ Error: {e}")
        return False

def test_auth_service():
    """Verifica que auth_service importa correctamente las funciones"""
    print("\n🔍 Verificando integración con auth_service...")
    
    try:
        from services.auth_service import authenticate_user
        import inspect
        
        # Verificar que usa las funciones de password
        source = inspect.getsource(authenticate_user)
        
        if "verify_password" in source:
            print("   ✅ auth_service usa verify_password")
        else:
            print("   ⚠️  auth_service NO usa verify_password")
            print("      (Puede estar usando comparación directa)")
        
        if "is_hashed" in source:
            print("   ✅ auth_service detecta contraseñas hasheadas")
        else:
            print("   ℹ️  auth_service no detecta formato de hash")
        
        return True
        
    except Exception as e:
        print(f"   ❌ Error: {e}")
        return False

def main():
    """Ejecuta todos los tests"""
    print("="*70)
    print("🔐 VERIFICACIÓN DEL SISTEMA DE HASHING DE CONTRASEÑAS")
    print("="*70)
    
    tests = [
        ("Imports", test_imports),
        ("Bcrypt Disponible", test_bcrypt_available),
        ("Hash y Verificación", test_hash_verify),
        ("Detección de Hashes", test_is_hashed),
        ("Integración Auth Service", test_auth_service),
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
        print("\n📋 Próximos pasos:")
        print("   1. Hacer backup de la BD:")
        print("      mysqldump -u root -p control-asistencia > backup.sql")
        print("\n   2. Vista previa de migración:")
        print("      python Server\\scripts\\migrate_passwords.py --preview")
        print("\n   3. Ejecutar migración:")
        print("      python Server\\scripts\\migrate_passwords.py")
    else:
        print(f"\n⚠️  {total - passed} tests fallaron")
        print("\n   Soluciones:")
        print("   - Si falta bcrypt: pip install passlib[bcrypt]")
        print("   - Si hay errores de import: verificar estructura de carpetas")
        print("   - Consultar BCRYPT_INSTALADO.md para más ayuda")
    
    print("="*70 + "\n")
    
    return passed == total

if __name__ == "__main__":
    success = main()
    sys.exit(0 if success else 1)
