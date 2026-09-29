from fastapi import APIRouter, Depends, HTTPException, status, Request
from typing import List
from models.user import UserCreate, UserUpdate, UserViewModel
from services import user_service
from routers.auth import get_current_active_admin

router = APIRouter(
    prefix="/api/v1/usuarios",
    tags=["Usuarios"],
)

# Dependencia para proteger las rutas
async def admin_required(request: Request, admin_user = Depends(get_current_active_admin)):
    """Valida que sea un admin. El admin_user es retornado por get_current_active_admin"""
    return admin_user

@router.get("/", response_model=List[UserViewModel])
async def read_users(admin_user = Depends(admin_required)):
    """
    Obtiene una lista de todos los usuarios.
    Solo accesible para administradores.
    """
    return await user_service.get_all_users()

@router.post("/", response_model=UserViewModel, status_code=status.HTTP_201_CREATED)
async def create_user(user: UserCreate, admin_user = Depends(admin_required)):
    """
    Crea un nuevo usuario.
    Solo accesible para administradores.
    """
    return await user_service.create_new_user(user)

@router.get("/{user_id}", response_model=UserViewModel)
async def read_user(user_id: int, admin_user = Depends(admin_required)):
    """
    Obtiene un usuario específico por su ID.
    Solo accesible para administradores.
    """
    return await user_service.get_user_by_id(user_id)

@router.put("/{user_id}", response_model=UserViewModel)
async def update_user(user_id: int, user: UserUpdate, admin_user = Depends(admin_required)):
    """
    Actualiza un usuario existente.
    Solo accesible para administradores.
    """
    return await user_service.update_existing_user(user_id, user)

@router.delete("/{user_id}", status_code=status.HTTP_204_NO_CONTENT)
async def delete_user(user_id: int, admin_user = Depends(admin_required)):
    """
    Elimina un usuario.
    Solo accesible para administradores.
    """
    await user_service.delete_existing_user(user_id)
    return {"ok": True} # No se envía cuerpo en 204, pero es buena práctica devolver algo
