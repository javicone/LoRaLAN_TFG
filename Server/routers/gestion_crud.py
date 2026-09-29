"""
Router CRUD para gestión de datos desde el panel de administración
Endpoint base: /api/v1
"""
from fastapi import APIRouter, Depends, HTTPException, status
from pydantic import BaseModel, Field, validator
from typing import Optional, List
from datetime import datetime
from routers.auth import get_current_active_admin
from repositories import aulaRepository
from utils.database_pool import get_db_pool
import aiomysql
import logging

logger = logging.getLogger(__name__)

router = APIRouter(
    prefix="/api/v1",
    tags=["Gestión de Datos"],
)

# =============================================
# MODELOS PYDANTIC
# =============================================

# --- Aulas ---
class AulaBase(BaseModel):
    nombre: str = Field(..., min_length=3, max_length=50)

class AulaCreate(AulaBase):
    pass

class AulaUpdate(BaseModel):
    nombre: Optional[str] = Field(None, min_length=3, max_length=50)

class AulaResponse(AulaBase):
    id: int

# --- Clases ---
class ClaseBase(BaseModel):
    asignatura: str = Field(..., min_length=2, max_length=100)
    aula_id: int
    profesor_id: int
    hora_inicio: datetime
    hora_fin: datetime
    
    @validator('hora_fin')
    def hora_fin_after_inicio(cls, v, values):
        if 'hora_inicio' in values and v <= values['hora_inicio']:
            raise ValueError('hora_fin debe ser posterior a hora_inicio')
        return v

class ClaseCreate(ClaseBase):
    pass

class ClaseUpdate(BaseModel):
    asignatura: Optional[str] = Field(None, min_length=2, max_length=100)
    aula_id: Optional[int] = None
    profesor_id: Optional[int] = None
    hora_inicio: Optional[datetime] = None
    hora_fin: Optional[datetime] = None

class ClaseResponse(BaseModel):
    id: int
    asignatura: str
    aula_id: int
    aula_nombre: Optional[str] = None
    profesor_id: int
    profesor_nombre: Optional[str] = None
    hora_inicio: datetime
    hora_fin: datetime

# --- Asistencias ---
class AsistenciaBase(BaseModel):
    usuario_id: int
    aula_id: int
    fecha_hora: datetime

class AsistenciaCreate(AsistenciaBase):
    pass

class AsistenciaUpdate(BaseModel):
    usuario_id: Optional[int] = None
    aula_id: Optional[int] = None
    fecha_hora: Optional[datetime] = None

class AsistenciaResponse(BaseModel):
    id: int
    usuario_id: int
    usuario_nombre: Optional[str] = None
    aula_id: int
    aula_nombre: Optional[str] = None
    fecha_hora: datetime


# =============================================
# ENDPOINTS AULAS CRUD
# =============================================

@router.get("/aulas-crud", response_model=List[AulaResponse])
async def get_all_aulas(admin = Depends(get_current_active_admin)):
    """Obtiene todas las aulas"""
    try:
        aulas = await aulaRepository.get_all_aulas()
        return aulas
    except Exception as e:
        logger.error(f"Error al obtener aulas: {e}")
        raise HTTPException(status_code=500, detail="Error al obtener aulas")

@router.get("/aulas-crud/{aula_id}", response_model=AulaResponse)
async def get_aula(aula_id: int, admin = Depends(get_current_active_admin)):
    """Obtiene un aula por ID"""
    aula = await aulaRepository.get_aula_by_id(aula_id)
    if not aula:
        raise HTTPException(status_code=404, detail="Aula no encontrada")
    return aula

@router.post("/aulas-crud", response_model=AulaResponse, status_code=status.HTTP_201_CREATED)
async def create_aula(aula: AulaCreate, admin = Depends(get_current_active_admin)):
    """Crea una nueva aula"""
    # Verificar nombre único
    existing = await aulaRepository.get_aula_by_nombre(aula.nombre)
    if existing:
        raise HTTPException(status_code=409, detail="Ya existe un aula con ese nombre")
    
    try:
        new_id = await aulaRepository.create_aula(aula.nombre)
        return await aulaRepository.get_aula_by_id(new_id)
    except Exception as e:
        logger.error(f"Error al crear aula: {e}")
        raise HTTPException(status_code=500, detail="Error al crear aula")

@router.put("/aulas-crud/{aula_id}", response_model=AulaResponse)
async def update_aula(aula_id: int, aula_update: AulaUpdate, admin = Depends(get_current_active_admin)):
    """Actualiza un aula existente"""
    existing = await aulaRepository.get_aula_by_id(aula_id)
    if not existing:
        raise HTTPException(status_code=404, detail="Aula no encontrada")
    
    # Verificar nombre único si se cambia
    if aula_update.nombre and aula_update.nombre != existing['nombre']:
        nombre_exists = await aulaRepository.get_aula_by_nombre(aula_update.nombre)
        if nombre_exists:
            raise HTTPException(status_code=409, detail="Ya existe un aula con ese nombre")
    
    try:
        updated = await aulaRepository.update_aula(
            aula_id, 
            aula_update.nombre
        )
        if not updated:
            raise HTTPException(status_code=400, detail="No se realizaron cambios")
        return updated
    except Exception as e:
        logger.error(f"Error al actualizar aula: {e}")
        raise HTTPException(status_code=500, detail="Error al actualizar aula")

@router.delete("/aulas-crud/{aula_id}", status_code=status.HTTP_204_NO_CONTENT)
async def delete_aula(aula_id: int, admin = Depends(get_current_active_admin)):
    """Elimina un aula"""
    existing = await aulaRepository.get_aula_by_id(aula_id)
    if not existing:
        raise HTTPException(status_code=404, detail="Aula no encontrada")
    
    success, message = await aulaRepository.delete_aula(aula_id)
    if not success:
        raise HTTPException(status_code=409, detail=message)


# =============================================
# ENDPOINTS CLASES CRUD
# =============================================

async def _get_clases_with_names():
    """Obtiene clases con nombres de aula y profesor"""
    pool = await get_db_pool()
    async with pool.acquire() as conn:
        async with conn.cursor(aiomysql.DictCursor) as cursor:
            await cursor.execute("""
                SELECT c.id, c.asignatura, c.aula_id, c.profesor_id, 
                       c.hora_inicio, c.hora_fin,
                       a.nombre as aula_nombre, u.nombre as profesor_nombre
                FROM clases c
                LEFT JOIN aulas a ON c.aula_id = a.id
                LEFT JOIN usuarios u ON c.profesor_id = u.id
                ORDER BY c.hora_inicio DESC
            """)
            return await cursor.fetchall()

async def _get_clase_with_names(clase_id: int):
    """Obtiene una clase con nombres de aula y profesor"""
    pool = await get_db_pool()
    async with pool.acquire() as conn:
        async with conn.cursor(aiomysql.DictCursor) as cursor:
            await cursor.execute("""
                SELECT c.id, c.asignatura, c.aula_id, c.profesor_id, 
                       c.hora_inicio, c.hora_fin,
                       a.nombre as aula_nombre, u.nombre as profesor_nombre
                FROM clases c
                LEFT JOIN aulas a ON c.aula_id = a.id
                LEFT JOIN usuarios u ON c.profesor_id = u.id
                WHERE c.id = %s
            """, (clase_id,))
            return await cursor.fetchone()

@router.get("/clases-crud", response_model=List[ClaseResponse])
async def get_all_clases(admin = Depends(get_current_active_admin)):
    """Obtiene todas las clases"""
    try:
        clases = await _get_clases_with_names()
        return clases
    except Exception as e:
        logger.error(f"Error al obtener clases: {e}")
        raise HTTPException(status_code=500, detail="Error al obtener clases")

@router.get("/clases-crud/{clase_id}", response_model=ClaseResponse)
async def get_clase(clase_id: int, admin = Depends(get_current_active_admin)):
    """Obtiene una clase por ID"""
    clase = await _get_clase_with_names(clase_id)
    if not clase:
        raise HTTPException(status_code=404, detail="Clase no encontrada")
    return clase

@router.post("/clases-crud", response_model=ClaseResponse, status_code=status.HTTP_201_CREATED)
async def create_clase(clase: ClaseCreate, admin = Depends(get_current_active_admin)):
    """Crea una nueva clase"""
    pool = await get_db_pool()
    async with pool.acquire() as conn:
        async with conn.cursor() as cursor:
            try:
                sql = """INSERT INTO clases (asignatura, aula_id, profesor_id, hora_inicio, hora_fin) 
                         VALUES (%s, %s, %s, %s, %s)"""
                await cursor.execute(sql, (
                    clase.asignatura, clase.aula_id, clase.profesor_id,
                    clase.hora_inicio, clase.hora_fin
                ))
                await conn.commit()
                new_id = cursor.lastrowid
                return await _get_clase_with_names(new_id)
            except Exception as e:
                logger.error(f"Error al crear clase: {e}")
                raise HTTPException(status_code=500, detail="Error al crear clase")

@router.put("/clases-crud/{clase_id}", response_model=ClaseResponse)
async def update_clase(clase_id: int, clase_update: ClaseUpdate, admin = Depends(get_current_active_admin)):
    """Actualiza una clase existente"""
    existing = await _get_clase_with_names(clase_id)
    if not existing:
        raise HTTPException(status_code=404, detail="Clase no encontrada")
    
    pool = await get_db_pool()
    async with pool.acquire() as conn:
        async with conn.cursor() as cursor:
            updates = []
            values = []
            
            data = clase_update.model_dump(exclude_unset=True)
            for field, value in data.items():
                if value is not None:
                    updates.append(f"{field} = %s")
                    values.append(value)
            
            if not updates:
                raise HTTPException(status_code=400, detail="No se proporcionaron datos para actualizar")
            
            values.append(clase_id)
            sql = f"UPDATE clases SET {', '.join(updates)} WHERE id = %s"
            
            await cursor.execute(sql, tuple(values))
            await conn.commit()
            
            if cursor.rowcount == 0:
                raise HTTPException(status_code=400, detail="No se realizaron cambios")
            
            return await _get_clase_with_names(clase_id)

@router.delete("/clases-crud/{clase_id}", status_code=status.HTTP_204_NO_CONTENT)
async def delete_clase(clase_id: int, admin = Depends(get_current_active_admin)):
    """Elimina una clase"""
    existing = await _get_clase_with_names(clase_id)
    if not existing:
        raise HTTPException(status_code=404, detail="Clase no encontrada")
    
    pool = await get_db_pool()
    async with pool.acquire() as conn:
        async with conn.cursor() as cursor:
            await cursor.execute("DELETE FROM clases WHERE id = %s", (clase_id,))
            await conn.commit()


# =============================================
# ENDPOINTS ASISTENCIAS CRUD
# =============================================

async def _get_asistencias_with_names():
    """Obtiene asistencias con nombres de usuario y aula"""
    pool = await get_db_pool()
    async with pool.acquire() as conn:
        async with conn.cursor(aiomysql.DictCursor) as cursor:
            await cursor.execute("""
                SELECT a.id, a.usuario_id, a.aula_id, a.fecha_hora,
                       u.nombre as usuario_nombre, au.nombre as aula_nombre
                FROM asistencias a
                LEFT JOIN usuarios u ON a.usuario_id = u.id
                LEFT JOIN aulas au ON a.aula_id = au.id
                ORDER BY a.fecha_hora DESC
                LIMIT 500
            """)
            return await cursor.fetchall()

async def _get_asistencia_with_names(asistencia_id: int):
    """Obtiene una asistencia con nombres"""
    pool = await get_db_pool()
    async with pool.acquire() as conn:
        async with conn.cursor(aiomysql.DictCursor) as cursor:
            await cursor.execute("""
                SELECT a.id, a.usuario_id, a.aula_id, a.fecha_hora,
                       u.nombre as usuario_nombre, au.nombre as aula_nombre
                FROM asistencias a
                LEFT JOIN usuarios u ON a.usuario_id = u.id
                LEFT JOIN aulas au ON a.aula_id = au.id
                WHERE a.id = %s
            """, (asistencia_id,))
            return await cursor.fetchone()

@router.get("/asistencias-crud", response_model=List[AsistenciaResponse])
async def get_all_asistencias(admin = Depends(get_current_active_admin)):
    """Obtiene las últimas 500 asistencias"""
    try:
        asistencias = await _get_asistencias_with_names()
        return asistencias
    except Exception as e:
        logger.error(f"Error al obtener asistencias: {e}")
        raise HTTPException(status_code=500, detail="Error al obtener asistencias")

@router.get("/asistencias-crud/{asistencia_id}", response_model=AsistenciaResponse)
async def get_asistencia(asistencia_id: int, admin = Depends(get_current_active_admin)):
    """Obtiene una asistencia por ID"""
    asistencia = await _get_asistencia_with_names(asistencia_id)
    if not asistencia:
        raise HTTPException(status_code=404, detail="Asistencia no encontrada")
    return asistencia

@router.post("/asistencias-crud", response_model=AsistenciaResponse, status_code=status.HTTP_201_CREATED)
async def create_asistencia(asistencia: AsistenciaCreate, admin = Depends(get_current_active_admin)):
    """Crea una nueva asistencia manual"""
    pool = await get_db_pool()
    async with pool.acquire() as conn:
        async with conn.cursor() as cursor:
            try:
                sql = "INSERT INTO asistencias (usuario_id, aula_id, fecha_hora) VALUES (%s, %s, %s)"
                await cursor.execute(sql, (
                    asistencia.usuario_id, asistencia.aula_id, asistencia.fecha_hora
                ))
                await conn.commit()
                new_id = cursor.lastrowid
                return await _get_asistencia_with_names(new_id)
            except Exception as e:
                logger.error(f"Error al crear asistencia: {e}")
                raise HTTPException(status_code=500, detail="Error al crear asistencia")

@router.put("/asistencias-crud/{asistencia_id}", response_model=AsistenciaResponse)
async def update_asistencia(asistencia_id: int, asistencia_update: AsistenciaUpdate, admin = Depends(get_current_active_admin)):
    """Actualiza una asistencia existente"""
    existing = await _get_asistencia_with_names(asistencia_id)
    if not existing:
        raise HTTPException(status_code=404, detail="Asistencia no encontrada")
    
    pool = await get_db_pool()
    async with pool.acquire() as conn:
        async with conn.cursor() as cursor:
            updates = []
            values = []
            
            data = asistencia_update.model_dump(exclude_unset=True)
            for field, value in data.items():
                if value is not None:
                    updates.append(f"{field} = %s")
                    values.append(value)
            
            if not updates:
                raise HTTPException(status_code=400, detail="No se proporcionaron datos para actualizar")
            
            values.append(asistencia_id)
            sql = f"UPDATE asistencias SET {', '.join(updates)} WHERE id = %s"
            
            await cursor.execute(sql, tuple(values))
            await conn.commit()
            
            return await _get_asistencia_with_names(asistencia_id)

@router.delete("/asistencias-crud/{asistencia_id}", status_code=status.HTTP_204_NO_CONTENT)
async def delete_asistencia(asistencia_id: int, admin = Depends(get_current_active_admin)):
    """Elimina una asistencia"""
    existing = await _get_asistencia_with_names(asistencia_id)
    if not existing:
        raise HTTPException(status_code=404, detail="Asistencia no encontrada")
    
    pool = await get_db_pool()
    async with pool.acquire() as conn:
        async with conn.cursor() as cursor:
            await cursor.execute("DELETE FROM asistencias WHERE id = %s", (asistencia_id,))
            await conn.commit()
