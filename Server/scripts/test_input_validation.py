#!/usr/bin/env python3
"""
Script de prueba para el sistema de sanitización y validación de inputs.

Ejecutar para verificar que:
1. El módulo de sanitización funciona correctamente
2. Los validadores Pydantic detectan inputs maliciosos
3. El middleware bloquea requests peligrosas
"""

import sys
from pathlib import Path

# Añadir Server/ al path
sys.path.insert(0, str(Path(__file__).resolve().parent.parent))


def test_sanitization_module():
    """Prueba el módulo security/input_sanitization.py"""
    print("\n" + "="*70)
    print("🧪 TEST: Módulo de Sanitización")
    print("="*70 + "\n")
    
    from security.input_sanitization import (
        sanitize_string,
        validate_email,
        validate_uid,
        validate_device_id,
        validate_aula_name,
        check_injection_patterns,
        validate_sensor_data,
    )
    
    tests_passed = 0
    tests_failed = 0
    
    # Test 1: Sanitize XSS
    print("1️⃣  Test: Sanitizar XSS")
    try:
        result = sanitize_string("<script>alert('XSS')</script>")
        if "&lt;script&gt;" in result:
            print("   ✅ PASS - HTML escapado correctamente")
            tests_passed += 1
        else:
            print(f"   ❌ FAIL - Resultado: {result}")
            tests_failed += 1
    except Exception as e:
        print(f"   ❌ FAIL - Error: {e}")
        tests_failed += 1
    
    # Test 2: Detectar SQL Injection
    print("\n2️⃣  Test: Detectar SQL Injection")
    malicious_inputs = [
        "admin' OR '1'='1",
        "'; DROP TABLE usuarios; --",
        "1 UNION SELECT * FROM passwords",
    ]
    
    for malicious in malicious_inputs:
        try:
            sanitize_string(malicious, allow_special_chars=True)
            print(f"   ❌ FAIL - No detectó: {malicious[:30]}")
            tests_failed += 1
        except ValueError:
            print(f"   ✅ PASS - Bloqueó: {malicious[:30]}")
            tests_passed += 1
    
    # Test 3: Validar Email
    print("\n3️⃣  Test: Validar Email")
    valid_emails = ["user@example.com", "test.user@universidad.es"]
    invalid_emails = ["<script>@evil.com", "admin'--@test.com", "notanemail"]
    
    for email in valid_emails:
        try:
            result = validate_email(email)
            print(f"   ✅ PASS - Email válido: {result}")
            tests_passed += 1
        except ValueError as e:
            print(f"   ❌ FAIL - Rechazó email válido: {email}")
            tests_failed += 1
    
    for email in invalid_emails:
        try:
            result = validate_email(email)
            print(f"   ❌ FAIL - No rechazó email inválido: {email}")
            tests_failed += 1
        except ValueError:
            print(f"   ✅ PASS - Bloqueó email inválido: {email}")
            tests_passed += 1
    
    # Test 4: Validar UID
    print("\n4️⃣  Test: Validar UID de tarjeta NFC")
    valid_uids = ["04A2F33A5B6D80", "AABBCCDD", "12345678"]
    invalid_uids = ["short", "não_hex", "../../../etc/passwd"]
    
    for uid in valid_uids:
        try:
            result = validate_uid(uid)
            print(f"   ✅ PASS - UID válido: {result}")
            tests_passed += 1
        except ValueError:
            print(f"   ❌ FAIL - Rechazó UID válido: {uid}")
            tests_failed += 1
    
    for uid in invalid_uids:
        try:
            result = validate_uid(uid)
            print(f"   ❌ FAIL - No rechazó UID inválido: {uid}")
            tests_failed += 1
        except ValueError:
            print(f"   ✅ PASS - Bloqueó UID inválido: {uid}")
            tests_passed += 1
    
    # Test 5: Validar Device ID
    print("\n5️⃣  Test: Validar Device ID")
    valid_devices = ["ESP32_Aula101", "DEVICE-001", "sensor_temp_1"]
    invalid_devices = ["../etc/passwd", "device|rm -rf", "$PAYLOAD"]
    
    for device in valid_devices:
        try:
            result = validate_device_id(device)
            print(f"   ✅ PASS - Device válido: {result}")
            tests_passed += 1
        except ValueError:
            print(f"   ❌ FAIL - Rechazó device válido: {device}")
            tests_failed += 1
    
    for device in invalid_devices:
        try:
            result = validate_device_id(device)
            print(f"   ❌ FAIL - No rechazó device inválido: {device}")
            tests_failed += 1
        except ValueError:
            print(f"   ✅ PASS - Bloqueó device inválido: {device}")
            tests_passed += 1
    
    # Test 6: Validar Datos de Sensores
    print("\n6️⃣  Test: Validar Rangos de Sensores")
    sensor_tests = [
        ('temperatura', 22.5, True),   # Válido
        ('temperatura', -100.0, False), # Fuera de rango
        ('humedad', 45.0, True),        # Válido
        ('humedad', 150.0, False),      # Fuera de rango
        ('personas', 25, True),         # Válido
        ('personas', -5, False),        # Negativo
    ]
    
    for sensor_type, value, should_pass in sensor_tests:
        try:
            result = validate_sensor_data(sensor_type, value)
            if should_pass:
                print(f"   ✅ PASS - {sensor_type}={value} válido")
                tests_passed += 1
            else:
                print(f"   ❌ FAIL - No rechazó {sensor_type}={value}")
                tests_failed += 1
        except ValueError:
            if not should_pass:
                print(f"   ✅ PASS - Bloqueó {sensor_type}={value}")
                tests_passed += 1
            else:
                print(f"   ❌ FAIL - Rechazó valor válido {sensor_type}={value}")
                tests_failed += 1
    
    # Test 7: Detectar Patterns
    print("\n7️⃣  Test: Detector de Patrones de Inyección")
    patterns_to_detect = [
        ("<script>alert(1)</script>", True),
        ("'; DROP TABLE users; --", True),
        ("ignore previous instructions", True),
        ("Normal user input", False),
        ("Email: user@test.com", False),
    ]
    
    for text, should_detect in patterns_to_detect:
        detected = check_injection_patterns(text)
        if detected == should_detect:
            status = "detectó" if detected else "no detectó"
            print(f"   ✅ PASS - {status} correctamente: {text[:30]}")
            tests_passed += 1
        else:
            print(f"   ❌ FAIL - {text[:30]}")
            tests_failed += 1
    
    # Reporte final
    print("\n" + "="*70)
    print("📊 RESULTADOS")
    print("="*70)
    print(f"   ✅ Tests pasados: {tests_passed}")
    print(f"   ❌ Tests fallidos: {tests_failed}")
    print(f"   📈 Tasa de éxito: {tests_passed/(tests_passed+tests_failed)*100:.1f}%")
    print("="*70 + "\n")
    
    return tests_failed == 0


def test_pydantic_models():
    """Prueba los modelos Pydantic con validadores"""
    print("\n" + "="*70)
    print("🧪 TEST: Modelos Pydantic con Validadores")
    print("="*70 + "\n")
    
    from models.user import UserLogin
    from models.asistencia import RegistroAsistencia
    from models.aula import DatosAula, ControlRelay
    from pydantic import ValidationError
    
    tests_passed = 0
    tests_failed = 0
    
    # Test 1: UserLogin con email malicioso
    print("1️⃣  Test: UserLogin - Email Injection")
    try:
        UserLogin(correo="<script>@evil.com", password="password123")
        print("   ❌ FAIL - No bloqueó email malicioso")
        tests_failed += 1
    except ValidationError as e:
        print("   ✅ PASS - Bloqueó email malicioso")
        tests_passed += 1
    
    # Test 2: UserLogin con contraseña común
    print("\n2️⃣  Test: UserLogin - Contraseña Común")
    try:
        UserLogin(correo="user@test.com", password="12345678")
        print("   ❌ FAIL - No bloqueó contraseña común")
        tests_failed += 1
    except ValidationError:
        print("   ✅ PASS - Bloqueó contraseña común")
        tests_passed += 1
    
    # Test 3: RegistroAsistencia con UID inválido
    print("\n3️⃣  Test: RegistroAsistencia - UID Inválido")
    try:
        RegistroAsistencia(uid="NOT_HEX!", device_id="ESP32_001")
        print("   ❌ FAIL - No bloqueó UID no-hexadecimal")
        tests_failed += 1
    except ValidationError:
        print("   ✅ PASS - Bloqueó UID inválido")
        tests_passed += 1
    
    # Test 4: RegistroAsistencia con device_id malicioso
    print("\n4️⃣  Test: RegistroAsistencia - Device ID con Path Traversal")
    try:
        RegistroAsistencia(uid="AABBCCDD", device_id="../etc/passwd")
        print("   ❌ FAIL - No bloqueó path traversal")
        tests_failed += 1
    except ValidationError:
        print("   ✅ PASS - Bloqueó path traversal")
        tests_passed += 1
    
    # Test 5: DatosAula con temperatura fuera de rango
    print("\n5️⃣  Test: DatosAula - Temperatura Fuera de Rango")
    try:
        DatosAula(
            aula="Test",
            temperatura=200.0,  # ❌ Fuera de rango
            luz=300.0,
            presion=1013.0,
            humedad=50.0,
            personas=10,
            clase_en_curso=True
        )
        print("   ❌ FAIL - No bloqueó temperatura imposible")
        tests_failed += 1
    except ValidationError:
        print("   ✅ PASS - Bloqueó temperatura fuera de rango")
        tests_passed += 1
    
    # Test 6: DatosAula válido
    print("\n6️⃣  Test: DatosAula - Datos Válidos")
    try:
        aula = DatosAula(
            aula="Aula_101",
            temperatura=22.5,
            luz=350.0,
            presion=1013.25,
            humedad=45.0,
            personas=25,
            clase_en_curso=True
        )
        print(f"   ✅ PASS - Datos válidos aceptados: {aula.aula}")
        tests_passed += 1
    except ValidationError as e:
        print(f"   ❌ FAIL - Rechazó datos válidos: {e}")
        tests_failed += 1
    
    # Reporte
    print("\n" + "="*70)
    print("📊 RESULTADOS")
    print("="*70)
    print(f"   ✅ Tests pasados: {tests_passed}")
    print(f"   ❌ Tests fallidos: {tests_failed}")
    print("="*70 + "\n")
    
    return tests_failed == 0


def main():
    """Ejecuta todos los tests"""
    print("="*70)
    print("🛡️  SISTEMA DE PREVENCIÓN DE INYECCIONES")
    print("="*70)
    
    # Test 1: Módulo de sanitización
    sanitization_ok = test_sanitization_module()
    
    # Test 2: Modelos Pydantic
    models_ok = test_pydantic_models()
    
    # Reporte final
    print("\n" + "="*70)
    print("🎯 RESUMEN GENERAL")
    print("="*70)
    
    if sanitization_ok and models_ok:
        print("   🎉 ¡TODOS LOS TESTS PASARON!")
        print("\n   ✅ Sistema de sanitización funcionando")
        print("   ✅ Modelos Pydantic con validadores activos")
        print("\n   🔒 Protecciones implementadas:")
        print("      - SQL Injection")
        print("      - XSS (Cross-Site Scripting)")
        print("      - Command Injection")
        print("      - Path Traversal")
        print("      - Prompt Injection (futuro LLMs)")
        print("\n   📋 Próximo paso:")
        print("      1. Activar middleware en appFastApi.py")
        print("      2. Reiniciar servidor")
        print("      3. Monitorear logs para intentos de inyección")
    else:
        print("   ⚠️  ALGUNOS TESTS FALLARON")
        if not sanitization_ok:
            print("      ❌ Módulo de sanitización tiene errores")
        if not models_ok:
            print("      ❌ Modelos Pydantic tienen errores")
    
    print("="*70 + "\n")
    
    return sanitization_ok and models_ok


if __name__ == "__main__":
    success = main()
    sys.exit(0 if success else 1)
