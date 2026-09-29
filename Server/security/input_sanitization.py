"""
Módulo de sanitización de inputs para prevenir inyecciones.

Este módulo proporciona funciones para limpiar y validar datos de entrada
del usuario, previniendo:
- Inyección SQL
- XSS (Cross-Site Scripting)
- Command Injection
- Path Traversal
- Prompt Injection (si se integra con LLMs en el futuro)

Author: Security Team
Date: 2026-02-11
"""

import re
import html
from typing import Optional, List
import logging

logger = logging.getLogger(__name__)

# Patrones peligrosos comunes
DANGEROUS_PATTERNS = {
    'sql_injection': [
        r"(union\s+select|insert\s+into|drop\s+table|delete\s+from|update\s+\w+\s+set)",
        r"(exec\s*\(|execute\s*\(|sp_executesql)",
        r"(--|;--|/\*|\*/|xp_cmdshell)",
        r"(0x[0-9a-f]+|char\s*\()",
    ],
    'xss': [
        r"(<script|<iframe|<object|<embed|<applet)",
        r"(javascript:|data:text/html|vbscript:)",
        r"(on\w+\s*=|onclick|onerror|onload)",
    ],
    'command_injection': [
        r"(\||&&|;|\$\(|`)",
        r"(\.\.\/|\.\.\\)",  # Path traversal
    ],
    'prompt_injection': [
        r"(ignore\s+(previous|all)\s+(instruction|prompt))",
        r"(system\s*:|admin\s*:|root\s*:)",
        r"(override\s+(security|policy|rule))",
    ]
}

# Caracteres permitidos por tipo de campo
ALLOWED_CHARS = {
    'alphanumeric': r'^[a-zA-Z0-9_-]+$',
    'alphanumeric_space': r'^[a-zA-Z0-9\s_-]+$',
    'email': r'^[a-zA-Z0-9._%+-]+@[a-zA-Z0-9.-]+\.[a-zA-Z]{2,}$',
    'hex': r'^[0-9A-Fa-f]+$',
    'numeric': r'^\d+$',
    'device_id': r'^[a-zA-Z0-9_-]{3,50}$',
}


def sanitize_string(
    text: str,
    max_length: int = 255,
    allow_special_chars: bool = False,
    strip_html: bool = True
) -> str:
    """
    Sanitiza un string eliminando caracteres peligrosos.
    
    Args:
        text: Texto a sanitizar
        max_length: Longitud máxima permitida
        allow_special_chars: Si False, solo permite alfanuméricos y espacios
        strip_html: Si True, escapa caracteres HTML
    
    Returns:
        String sanitizado y seguro
    
    Raises:
        ValueError: Si el texto contiene patrones peligrosos
    
    Example:
        >>> sanitize_string("<script>alert('XSS')</script>")
        "&lt;script&gt;alert('XSS')&lt;/script&gt;"
    """
    if not text:
        return ""
    
    # Truncar a longitud máxima
    text = text[:max_length]
    
    # Eliminar espacios en blanco al inicio/final
    text = text.strip()
    
    # Escape HTML si está habilitado
    if strip_html:
        text = html.escape(text)
    
    # Verificar patrones peligrosos
    for category, patterns in DANGEROUS_PATTERNS.items():
        for pattern in patterns:
            if re.search(pattern, text, re.IGNORECASE):
                logger.warning(
                    f"⚠️  Patrón peligroso detectado ({category}): {pattern[:50]}"
                )
                raise ValueError(
                    f"Input contiene caracteres no permitidos (categoría: {category})"
                )
    
    # Si no se permiten caracteres especiales, validar
    if not allow_special_chars:
        if not re.match(ALLOWED_CHARS['alphanumeric_space'], text):
            # Eliminar caracteres no permitidos en lugar de rechazar
            text = re.sub(r'[^a-zA-Z0-9\s_-]', '', text)
            logger.info(f"🧹 Caracteres especiales removidos del input")
    
    return text


def validate_email(email: str) -> str:
    """
    Valida y sanitiza un email.
    
    Args:
        email: Email a validar
    
    Returns:
        Email sanitizado en minúsculas
    
    Raises:
        ValueError: Si el email no es válido
    """
    email = email.strip().lower()
    
    # Validar longitud
    if len(email) > 255:
        raise ValueError("Email demasiado largo (max 255 caracteres)")
    
    if len(email) < 5:
        raise ValueError("Email demasiado corto")
    
    # Validar formato
    if not re.match(ALLOWED_CHARS['email'], email):
        raise ValueError("Formato de email inválido")
    
    # Verificar patrones peligrosos
    dangerous_chars = ['<', '>', '"', "'", '\\', '/', ';']
    if any(char in email for char in dangerous_chars):
        raise ValueError("Email contiene caracteres no permitidos")
    
    return email


def validate_uid(uid: str) -> str:
    """
    Valida un UID de tarjeta NFC (hexadecimal).
    
    Args:
        uid: UID a validar
    
    Returns:
        UID sanitizado en mayúsculas
    
    Raises:
        ValueError: Si el UID no es válido
    """
    uid = uid.strip().upper()
    
    # Validar longitud (UIDs NFC típicamente 8-20 caracteres hex)
    if len(uid) < 8 or len(uid) > 20:
        raise ValueError("UID debe tener entre 8 y 20 caracteres")
    
    # Validar formato hexadecimal
    if not re.match(ALLOWED_CHARS['hex'], uid):
        raise ValueError("UID debe ser hexadecimal (0-9, A-F)")
    
    return uid


def validate_device_id(device_id: str) -> str:
    """
    Valida un device_id para dispositivos ESP32.
    
    Args:
        device_id: ID del dispositivo
    
    Returns:
        device_id sanitizado
    
    Raises:
        ValueError: Si el device_id no es válido
    """
    device_id = device_id.strip()
    
    # Validar formato
    if not re.match(ALLOWED_CHARS['device_id'], device_id):
        raise ValueError(
            "device_id debe ser alfanumérico (3-50 caracteres, guiones y guiones bajos permitidos)"
        )
    
    # Prevenir inyección de comandos
    dangerous = ['..', '/', '\\', '$', '`', '|', '&', ';']
    if any(d in device_id for d in dangerous):
        raise ValueError("device_id contiene caracteres no permitidos")
    
    return device_id


def validate_aula_name(aula: str) -> str:
    """
    Valida nombre de aula.
    
    Args:
        aula: Nombre del aula
    
    Returns:
        Nombre sanitizado
    
    Raises:
        ValueError: Si el nombre no es válido
    """
    aula = aula.strip()
    
    # Validar longitud
    if len(aula) < 3 or len(aula) > 50:
        raise ValueError("Nombre de aula debe tener entre 3 y 50 caracteres")
    
    # Permitir letras, números, espacios, guiones
    if not re.match(r'^[a-zA-Z0-9\s\-_.]+$', aula):
        raise ValueError("Nombre de aula contiene caracteres no permitidos")
    
    return aula


def sanitize_sql_param(value: str) -> str:
    """
    Sanitiza un parámetro antes de usar en SQL.
    
    NOTA: SIEMPRE usar parámetros preparados. Esta función es una
    capa adicional de seguridad, NO un reemplazo.
    
    Args:
        value: Valor a sanitizar
    
    Returns:
        Valor sanitizado
    """
    if not value:
        return ""
    
    value = value.strip()
    
    # Eliminar caracteres SQL peligrosos
    dangerous_sql = [';', '--', '/*', '*/', 'xp_', 'sp_']
    for dangerous in dangerous_sql:
        if dangerous in value.lower():
            logger.warning(f"⚠️  SQL injection attempt blocked: {dangerous}")
            raise ValueError("Input contiene caracteres SQL no permitidos")
    
    return value


def check_injection_patterns(text: str, categories: Optional[List[str]] = None) -> bool:
    """
    Verifica si un texto contiene patrones de inyección.
    
    Args:
        text: Texto a verificar
        categories: Lista de categorías a verificar (sql_injection, xss, command_injection, prompt_injection)
                   Si es None, verifica todas
    
    Returns:
        True si se detectaron patrones peligrosos, False de lo contrario
    """
    if not text:
        return False
    
    categories_to_check = categories or DANGEROUS_PATTERNS.keys()
    
    for category in categories_to_check:
        if category not in DANGEROUS_PATTERNS:
            continue
        
        for pattern in DANGEROUS_PATTERNS[category]:
            if re.search(pattern, text, re.IGNORECASE):
                logger.warning(
                    f"🚨 Patrón de inyección detectado - Categoría: {category}, "
                    f"Patrón: {pattern[:30]}, Texto: {text[:50]}"
                )
                return True
    
    return False


def sanitize_for_log(text: str, max_length: int = 100) -> str:
    """
    Sanitiza texto antes de escribir en logs (previene log injection).
    
    Args:
        text: Texto a sanitizar
        max_length: Longitud máxima
    
    Returns:
        Texto seguro para logs
    """
    if not text:
        return ""
    
    # Truncar
    text = str(text)[:max_length]
    
    # Eliminar saltos de línea (previene log injection)
    text = text.replace('\n', ' ').replace('\r', ' ')
    
    # Eliminar caracteres de control
    text = re.sub(r'[\x00-\x1f\x7f-\x9f]', '', text)
    
    return text


def validate_numeric_range(
    value: float,
    min_value: float = float('-inf'),
    max_value: float = float('inf'),
    field_name: str = "value"
) -> float:
    """
    Valida que un valor numérico esté en un rango aceptable.
    
    Args:
        value: Valor a validar
        min_value: Valor mínimo permitido
        max_value: Valor máximo permitido
        field_name: Nombre del campo (para mensajes de error)
    
    Returns:
        Valor validado
    
    Raises:
        ValueError: Si el valor está fuera del rango
    """
    if value < min_value or value > max_value:
        raise ValueError(
            f"{field_name} debe estar entre {min_value} y {max_value} "
            f"(recibido: {value})"
        )
    
    return value


# Configuración de validación por tipo de sensor
SENSOR_RANGES = {
    'temperatura': {'min': -50.0, 'max': 100.0, 'unit': '°C'},
    'humedad': {'min': 0.0, 'max': 100.0, 'unit': '%'},
    'presion': {'min': 800.0, 'max': 1200.0, 'unit': 'hPa'},
    'luz': {'min': 0.0, 'max': 100000.0, 'unit': 'lux'},
    'personas': {'min': 0, 'max': 500, 'unit': 'personas'},
}


def validate_sensor_data(sensor_type: str, value: float) -> float:
    """
    Valida datos de sensores según rangos realistas.
    
    Args:
        sensor_type: Tipo de sensor (temperatura, humedad, presion, luz, personas)
        value: Valor del sensor
    
    Returns:
        Valor validado
    
    Raises:
        ValueError: Si el valor está fuera del rango realista
    """
    if sensor_type not in SENSOR_RANGES:
        raise ValueError(f"Tipo de sensor desconocido: {sensor_type}")
    
    config = SENSOR_RANGES[sensor_type]
    
    return validate_numeric_range(
        value,
        min_value=config['min'],
        max_value=config['max'],
        field_name=f"{sensor_type} ({config['unit']})"
    )
