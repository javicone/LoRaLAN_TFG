from fastapi import HTTPException, status
from models.user import UserCreate, UserUpdate, UserViewModel
from repositories import usuarioRepository
from security.password import hash_password, verify_password
import logging

logger = logging.getLogger(__name__)

async def get_all_users() -> list[UserViewModel]:
    """Obtiene todos los usuarios del sistema."""
    try:
        users_data = await usuarioRepository.get_all_usuarios()
        return [UserViewModel.model_validate(user) for user in users_data]
    except Exception as e:
        logger.error(f"Error al obtener todos los usuarios: {e}")
        raise HTTPException(status_code=status.HTTP_500_INTERNAL_SERVER_ERROR, detail="Error del servidor al obtener usuarios.")

async def get_user_by_id(user_id: int) -> UserViewModel:
    """Obtiene un usuario por su ID."""
    user = await usuarioRepository.get_usuario_by_id(user_id)
    if not user:
        raise HTTPException(status_code=status.HTTP_404_NOT_FOUND, detail=f"Usuario con id {user_id} no encontrado.")
    return UserViewModel.model_validate(user)

async def create_new_user(user_data: UserCreate) -> UserViewModel:
    """Crea un nuevo usuario."""
    # Verificar si el correo ya existe
    existing_user = await usuarioRepository.get_usuario_por_mail(user_data.correo)
    if existing_user:
        raise HTTPException(
            status_code=status.HTTP_409_CONFLICT,
            detail="El correo electrónico ya está registrado."
        )
    
    # Hashear la contraseña
    hashed_password = hash_password(user_data.password)
    
    try:
        # Crear el usuario en la base de datos
        new_user_id = await usuarioRepository.create_usuario(user_data, hashed_password)
        if not new_user_id:
            raise HTTPException(status_code=status.HTTP_500_INTERNAL_SERVER_ERROR, detail="No se pudo crear el usuario.")
        
        # Devolver el usuario recién creado
        created_user = await get_user_by_id(new_user_id)
        return created_user
    except Exception as e:
        logger.error(f"Error al crear nuevo usuario: {e}")
        raise HTTPException(status_code=status.HTTP_500_INTERNAL_SERVER_ERROR, detail="Error del servidor al crear el usuario.")

async def update_existing_user(user_id: int, user_update_data: UserUpdate) -> UserViewModel:
    """Actualiza un usuario existente."""
    # Verificar que el usuario exista
    user_to_update = await usuarioRepository.get_usuario_by_id(user_id)
    if not user_to_update:
        raise HTTPException(status_code=status.HTTP_404_NOT_FOUND, detail=f"Usuario con id {user_id} no encontrado.")

    # Si se cambia el correo, verificar que no exista ya
    if user_update_data.correo and user_update_data.correo != user_to_update['correo']:
        existing_user = await usuarioRepository.get_usuario_por_mail(user_update_data.correo)
        if existing_user:
            raise HTTPException(
                status_code=status.HTTP_409_CONFLICT,
                detail="El nuevo correo electrónico ya está en uso por otro usuario."
            )

    # Hashear la nueva contraseña si se proporciona
    hashed_password = None
    if user_update_data.password:
        hashed_password = hash_password(user_update_data.password)

    try:
        updated_user = await usuarioRepository.update_usuario(user_id, user_update_data, hashed_password)
        if not updated_user:
             raise HTTPException(status_code=status.HTTP_404_NOT_FOUND, detail="No se pudo actualizar o no hubo cambios.")
        return UserViewModel.model_validate(updated_user)
    except Exception as e:
        logger.error(f"Error al actualizar usuario {user_id}: {e}")
        raise HTTPException(status_code=status.HTTP_500_INTERNAL_SERVER_ERROR, detail="Error del servidor al actualizar el usuario.")

async def delete_existing_user(user_id: int):
    """Elimina un usuario."""
    # No permitir eliminar el usuario con ID 1 (superadmin)
    if user_id == 1 or user_id == 3: # Suponiendo que el admin principal es el 1 o 3
         raise HTTPException(status_code=status.HTTP_403_FORBIDDEN, detail="No se puede eliminar al administrador principal.")

    try:
        deleted = await usuarioRepository.delete_usuario(user_id)
        if not deleted:
            raise HTTPException(status_code=status.HTTP_404_NOT_FOUND, detail=f"Usuario con id {user_id} no encontrado para eliminar.")
    except ValueError as e:
        raise HTTPException(status_code=status.HTTP_409_CONFLICT, detail=str(e))
    
    return {"detail": f"Usuario con id {user_id} eliminado exitosamente."}
