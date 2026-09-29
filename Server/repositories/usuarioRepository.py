from utils.database_pool import get_db_pool
import aiomysql
from models.user import UserCreate, UserUpdate

async def get_usuario_por_mail(correo: str):
    pool = await get_db_pool()
    async with pool.acquire() as conn:
        async with conn.cursor(aiomysql.DictCursor) as cursor:
            await cursor.execute(
                "SELECT id,nombre, correo, password, rol FROM usuarios WHERE correo = %s", 
                (correo,)
            )
            return await cursor.fetchone()

async def get_all_usuarios():
    pool = await get_db_pool()
    async with pool.acquire() as conn:
        async with conn.cursor(aiomysql.DictCursor) as cursor:
            await cursor.execute("SELECT id, nombre, correo, rol, uid_tarjeta FROM usuarios ORDER BY nombre ASC")
            return await cursor.fetchall()

async def get_usuario_by_id(user_id: int):
    pool = await get_db_pool()
    async with pool.acquire() as conn:
        async with conn.cursor(aiomysql.DictCursor) as cursor:
            await cursor.execute(
                "SELECT id, nombre, correo, rol, uid_tarjeta FROM usuarios WHERE id = %s", 
                (user_id,)
            )
            return await cursor.fetchone()

async def create_usuario(user: UserCreate, hashed_password: str):
    pool = await get_db_pool()
    async with pool.acquire() as conn:
        async with conn.cursor() as cursor:
            sql = "INSERT INTO usuarios (nombre, correo, password, rol, uid_tarjeta) VALUES (%s, %s, %s, %s, %s)"
            await cursor.execute(sql, (user.nombre, user.correo, hashed_password, user.rol, user.uid_tarjeta))
            await conn.commit()
            return cursor.lastrowid

async def update_usuario(user_id: int, user_update: UserUpdate, hashed_password: str | None = None):
    pool = await get_db_pool()
    async with pool.acquire() as conn:
        async with conn.cursor(aiomysql.DictCursor) as cursor:
            
            fields = user_update.model_dump(exclude_unset=True)
            if hashed_password:
                fields['password'] = hashed_password
            
            if not fields:
                return None

            set_clause = ", ".join([f"{field} = %s" for field in fields.keys()])
            sql = f"UPDATE usuarios SET {set_clause} WHERE id = %s"
            
            values = list(fields.values())
            values.append(user_id)
            
            await cursor.execute(sql, tuple(values))
            await conn.commit()
            
            if cursor.rowcount == 0:
                return None
            
            # Devolver el usuario actualizado
            await cursor.execute("SELECT id, nombre, correo, rol, uid_tarjeta FROM usuarios WHERE id = %s", (user_id,))
            return await cursor.fetchone()


async def delete_usuario(user_id: int):
    pool = await get_db_pool()
    async with pool.acquire() as conn:
        async with conn.cursor() as cursor:
            try:
                await cursor.execute("DELETE FROM usuarios WHERE id = %s", (user_id,))
                await conn.commit()
                return cursor.rowcount > 0
            except Exception as e:
                # Verificar si es error de foreign key
                if "foreign key constraint fails" in str(e).lower() or "1451" in str(e):
                    raise ValueError("No se puede eliminar el usuario porque tiene asistencias registradas. Elimine primero las asistencias asociadas.")
                raise