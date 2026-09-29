from pydantic import BaseModel, EmailStr, validator, Field
from typing import Optional
import re

class UserLogin(BaseModel):
    correo: EmailStr
    password: str = Field(..., min_length=8, max_length=100)
    
    @validator('correo')
    def email_lowercase(cls, v):
        """Normalizar email a minúsculas y validar formato"""
        email = v.lower().strip()
        # Prevenir inyección XSS en email
        dangerous_chars = ['<', '>', '"', "'", '\\', ';']
        if any(char in email for char in dangerous_chars):
            raise ValueError('Email contiene caracteres no permitidos')
        return email
    
    @validator('password')
    def password_not_common(cls, v):
        """Verificar que no sea una contraseña común (primera capa)"""
        common = ['12345678', 'password', 'qwerty', 'admin123', '00000000']
        if v.lower() in common:
            raise ValueError('Contraseña demasiado común')
        # Prevenir inyección SQL en password
        if any(pattern in v.lower() for pattern in ['--', ';', 'drop', 'union', 'select']):
            raise ValueError('Password contiene caracteres no permitidos')
        return v

class TokenData(BaseModel):
    id: str | None = None
    rol: str | None = None

class UserInDB(BaseModel):
    id: int
    nombre: str
    correo: str
    rol: str
    password: str

class UserBase(BaseModel):
    nombre: str = Field(..., strip_whitespace=True, min_length=3, max_length=100)
    correo: EmailStr
    rol: str = Field(default='profesor', strip_whitespace=True)
    uid_tarjeta: Optional[str] = Field(None, strip_whitespace=True, max_length=50)

    @validator('rol')
    def rol_must_be_valid(cls, v):
        valid_roles = {'profesor', 'conserje', 'administrador'}
        if v not in valid_roles:
            raise ValueError(f'Rol debe ser uno de: {", ".join(valid_roles)}')
        return v

class UserCreate(UserBase):
    password: str = Field(..., min_length=8, max_length=100)

class UserUpdate(BaseModel):
    nombre: Optional[str] = Field(None, strip_whitespace=True, min_length=3, max_length=100)
    correo: Optional[EmailStr] = None
    rol: Optional[str] = Field(None, strip_whitespace=True)
    uid_tarjeta: Optional[str] = Field(None, strip_whitespace=True, max_length=50)
    password: Optional[str] = Field(None, min_length=8, max_length=100)

    @validator('rol')
    def rol_must_be_valid(cls, v):
        if v is None:
            return v
        valid_roles = {'profesor', 'conserje', 'administrador'}
        if v not in valid_roles:
            raise ValueError(f'Rol debe ser uno de: {", ".join(valid_roles)}')
        return v

class UserViewModel(UserBase):
    id: int

    class Config:
        from_attributes = True

