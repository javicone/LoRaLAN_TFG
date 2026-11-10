"""
Utilidades para registro de usuarios y gestión de contraseñas.
"""

import sys
from pathlib import Path
sys.path.insert(0, str(Path(__file__).resolve().parent.parent))

from security.password import hash_password
import asyncio
from utils.database_pool import get_db_pool, close_db_pool
import aiomysql

async def create_user(nombre: str, correo: str, password: str, rol: str = "profesor"):
    """
    Crea un nuevo usuario con contraseña hasheada.
    
    Args:
        nombre: Nombre completo del usuario
        correo: Email (usado para login)
        password: Contraseña en texto plano (será hasheada)
        rol: Rol del usuario (profesor, conserje, administrador)
    """
    print(f"\n🔐 Creando usuario: {correo}")
    
    try:
        # Hash de la contraseña
        hashed_password = hash_password(password)
        
        # Insertar en BD
        pool = await get_db_pool()
        async with pool.acquire() as conn:
            async with conn.cursor() as cursor:
                await cursor.execute(
                    """
                    INSERT INTO usuarios (nombre, correo, password, rol)
                    VALUES (%s, %s, %s, %s)
                    """,
                    (nombre, correo, hashed_password, rol)
                )
                await conn.commit()
                
        print(f"✅ Usuario creado exitosamente")
        print(f"   Nombre: {nombre}")
        print(f"   Email:  {correo}")
        print(f"   Rol:    {rol}")
        
    except Exception as e:
        print(f"❌ Error creando usuario: {e}")
        raise
    finally:
        await close_db_pool()

if __name__ == "__main__":
    import argparse
    parser = argparse.ArgumentParser(description="Crea un usuario con la contraseña en bcrypt.")
    parser.add_argument("nombre")
    parser.add_argument("correo")
    parser.add_argument("password")
    parser.add_argument("--rol", default="profesor", choices=["profesor", "conserje", "administrador"])
    args = parser.parse_args()
    asyncio.run(create_user(args.nombre, args.correo, args.password, args.rol))
