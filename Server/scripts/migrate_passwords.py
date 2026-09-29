#!/usr/bin/env python3
"""
Script para migrar contraseñas existentes de texto plano a bcrypt.

⚠️  EJECUTAR UNA SOLA VEZ en producción
⚠️  HACER BACKUP de la BD antes de ejecutar

Uso:
    cd Server
    python scripts/migrate_passwords.py [--preview]

El script:
1. Lee todas las contraseñas de la tabla usuarios
2. Detecta cuáles NO están hasheadas (no empiezan con $2b$)
3. Las hashea usando bcrypt con rounds=12
4. Actualiza la BD con los nuevos hashes
5. Muestra estadísticas del proceso
"""

import asyncio
import sys
from pathlib import Path
from datetime import datetime

# Añadir Server/ al path para imports
sys.path.insert(0, str(Path(__file__).resolve().parent.parent))

from utils.database_pool import get_db_pool, close_db_pool
from security.password import hash_password, is_hashed
import aiomysql
import logging

# Configurar logging
logging.basicConfig(
    level=logging.INFO,
    format='%(asctime)s - %(levelname)s - %(message)s'
)
logger = logging.getLogger(__name__)

async def migrate_passwords():
    """Migra todas las contraseñas de texto plano a bcrypt"""
    
    print("\n" + "="*70)
    print("🔐 MIGRACIÓN DE CONTRASEÑAS A BCRYPT")
    print("="*70)
    print("\n⚠️  ADVERTENCIAS:")
    print("   - Este script modificará la tabla 'usuarios'")
    print("   - Las contraseñas en texto plano serán reemplazadas por hashes bcrypt")
    print("   - ASEGÚRATE de tener un backup reciente de la base de datos")
    print("\n📋 Pasos que se ejecutarán:")
    print("   1. Conectar a la base de datos")
    print("   2. Leer todas las contraseñas")
    print("   3. Detectar cuáles NO están hasheadas")
    print("   4. Hashear con bcrypt (rounds=12)")
    print("   5. Actualizar la BD")
    print("="*70 + "\n")
    
    respuesta = input("¿Continuar con la migración? (escribir 'SI' para confirmar): ")
    if respuesta.upper() != 'SI':
        print("❌ Migración cancelada por el usuario")
        return
    
    print("\n🚀 Iniciando migración...\n")
    
    try:
        # Conectar a la BD
        logger.info("Conectando a la base de datos...")
        pool = await get_db_pool()
        logger.info("✅ Conexión establecida")
        
        async with pool.acquire() as conn:
            async with conn.cursor(aiomysql.DictCursor) as cursor:
                # 1. Obtener todos los usuarios
                logger.info("Leyendo usuarios de la base de datos...")
                await cursor.execute(
                    "SELECT id, nombre, correo, password, rol FROM usuarios ORDER BY id"
                )
                usuarios = await cursor.fetchall()
                
                if not usuarios:
                    print("\n⚠️  No hay usuarios en la base de datos")
                    return
                
                total_usuarios = len(usuarios)
                logger.info(f"📊 Encontrados {total_usuarios} usuarios\n")
                
                # 2. Clasificar contraseñas
                ya_hasheadas = []
                por_hashear = []
                
                for usuario in usuarios:
                    if is_hashed(usuario['password']):
                        ya_hasheadas.append(usuario)
                    else:
                        por_hashear.append(usuario)
                
                print(f"📊 Estado actual:")
                print(f"   ✅ Ya hasheadas:  {len(ya_hasheadas)} usuarios")
                print(f"   🔄 Por hashear:   {len(por_hashear)} usuarios")
                print(f"   📈 Total:         {total_usuarios} usuarios\n")
                
                if len(por_hashear) == 0:
                    print("✅ Todas las contraseñas ya están hasheadas con bcrypt")
                    print("   No es necesario realizar ninguna migración\n")
                    return
                
                # 3. Confirmar antes de modificar
                print(f"⚠️  Se modificarán {len(por_hashear)} contraseñas")
                print("\nUsuarios a migrar:")
                for i, usuario in enumerate(por_hashear[:10], 1):  # Mostrar max 10
                    print(f"   {i}. {usuario['correo']} ({usuario['rol']})")
                
                if len(por_hashear) > 10:
                    print(f"   ... y {len(por_hashear) - 10} más")
                
                print()
                confirmacion = input("¿Proceder con el hasheo? (escribir 'HASHEAR' para confirmar): ")
                if confirmacion.upper() != 'HASHEAR':
                    print("❌ Migración cancelada")
                    return
                
                # 4. Hashear contraseñas
                print(f"\n🔒 Hasheando {len(por_hashear)} contraseñas...\n")
                migradas = 0
                errores = 0
                
                for usuario in por_hashear:
                    try:
                        # Hashear contraseña
                        password_original = usuario['password']
                        nueva_hash = hash_password(password_original)
                        
                        # Actualizar en BD
                        await cursor.execute(
                            "UPDATE usuarios SET password = %s WHERE id = %s",
                            (nueva_hash, usuario['id'])
                        )
                        
                        migradas += 1
                        print(f"   ✅ {migradas}/{len(por_hashear)} - {usuario['correo']}")
                        
                    except Exception as e:
                        errores += 1
                        logger.error(f"   ❌ Error con {usuario['correo']}: {e}")
                
                # 5. Commit de cambios
                await conn.commit()
                logger.info("\n💾 Cambios guardados en la base de datos")
                
                # 6. Reporte final
                print("\n" + "="*70)
                print("🎉 MIGRACIÓN COMPLETADA")
                print("="*70)
                print(f"""
📊 Estadísticas:
   ✅ Contraseñas migradas:     {migradas}
   ❌ Errores:                  {errores}
   ℹ️  Ya estaban hasheadas:    {len(ya_hasheadas)}
   📈 Total de usuarios:        {total_usuarios}

🔐 Estado final:
   - {len(ya_hasheadas) + migradas} usuarios con bcrypt
   - {total_usuarios - (len(ya_hasheadas) + migradas)} sin migrar

⏰ Fecha: {datetime.now().strftime('%Y-%m-%d %H:%M:%S')}
                """)
                print("="*70 + "\n")
                
                if errores > 0:
                    logger.warning(f"⚠️  Hubo {errores} errores durante la migración")
                    logger.warning("   Revisar los logs arriba para más detalles")
                else:
                    logger.info("✅ ¡Migración exitosa sin errores!")
                
                # 7. Verificación opcional
                print("\n🔍 Verificando estado final...")
                await cursor.execute(
                    "SELECT COUNT(*) as total FROM usuarios WHERE password LIKE '$2b$%'"
                )
                result = await cursor.fetchone()
                bcrypt_count = result['total']
                
                print(f"   📊 Usuarios con bcrypt: {bcrypt_count}/{total_usuarios}")
                
                if bcrypt_count == total_usuarios:
                    print("   ✅ ¡100% de usuarios migrados exitosamente!")
                else:
                    print(f"   ⚠️  Quedan {total_usuarios - bcrypt_count} usuarios sin migrar")
                
    except Exception as e:
        logger.error(f"\n❌ Error crítico durante la migración: {e}")
        logger.error("   Los cambios NO se han guardado")
        raise
    finally:
        await close_db_pool()
        logger.info("\n🔒 Conexión a BD cerrada")

async def preview_migration():
    """Vista previa de la migración sin hacer cambios"""
    print("\n🔍 MODO PREVIEW - Solo lectura\n")
    
    try:
        pool = await get_db_pool()
        async with pool.acquire() as conn:
            async with conn.cursor(aiomysql.DictCursor) as cursor:
                await cursor.execute("SELECT id, correo, password, rol FROM usuarios")
                usuarios = await cursor.fetchall()
                
                hasheadas = sum(1 for u in usuarios if is_hashed(u['password']))
                planas = len(usuarios) - hasheadas
                
                print(f"📊 Estado actual:")
                print(f"   Total usuarios: {len(usuarios)}")
                print(f"   Con bcrypt:     {hasheadas}")
                print(f"   Texto plano:    {planas}\n")
                
                if planas > 0:
                    print("Usuarios con contraseña en texto plano:")
                    for u in usuarios:
                        if not is_hashed(u['password']):
                            print(f"   - {u['correo']} ({u['rol']})")
                
    finally:
        await close_db_pool()

if __name__ == "__main__":
    import argparse
    
    parser = argparse.ArgumentParser(description='Migrar contraseñas a bcrypt')
    parser.add_argument(
        '--preview',
        action='store_true',
        help='Solo mostrar qué se migraría, sin hacer cambios'
    )
    
    args = parser.parse_args()
    
    if args.preview:
        asyncio.run(preview_migration())
    else:
        asyncio.run(migrate_passwords())
