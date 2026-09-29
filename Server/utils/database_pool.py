import aiomysql
import asyncio
from database import DB_CONFIG
import logging
from typing import Optional

_pool: Optional[aiomysql.Pool] = None
_lock = asyncio.Lock()

async def get_db_pool() -> aiomysql.Pool:
    """
    Obtiene o crea el pool de conexiones con protección thread-safe.
    Usa double-checked locking para evitar race conditions.
    """
    global _pool
    
    # Primera comprobación sin lock (fast path)
    if _pool is not None:
        return _pool
    
    # Segunda comprobación con lock (slow path)
    async with _lock:
        if _pool is not None:
            return _pool
            
        cfg = dict(DB_CONFIG) if isinstance(DB_CONFIG, dict) else dict(vars(DB_CONFIG))
        
        # Mapear claves incompatibles
        if "database" in cfg and "db" not in cfg:
            cfg["db"] = cfg.pop("database")
        
        try:
            _pool = await aiomysql.create_pool(
                **cfg,
                autocommit=True,
                minsize=1,
                maxsize=10,
                pool_recycle=3600  # Reciclar conexiones cada hora
            )
            logging.info(f"✅ Pool de base de datos creado con {_pool.size} conexiones")
        except Exception as e:
            logging.error(f"❌ Error creando pool: {e}")
            raise RuntimeError(f"Error creando pool aiomysql: {list(cfg.keys())} — {e}")
    
    return _pool

async def close_db_pool() -> None:
    """
    Cierra el pool de conexiones
    """
    global _pool
    async with _lock:
        if _pool:
            _pool.close()
            await _pool.wait_closed()
            _pool = None
            logging.info("🔒 Pool de base de datos cerrado")