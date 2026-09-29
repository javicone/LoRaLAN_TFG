from fastapi import APIRouter, Request, HTTPException
from fastapi.responses import HTMLResponse, RedirectResponse
from fastapi.templating import Jinja2Templates
import logging
from pathlib import Path
from jose import jwt, JWTError  # Librería real para decodificar
from services.auth_service import SECRET_KEY, ALGORITHM # Configuración correcta

router = APIRouter()
logger = logging.getLogger(__name__)
templates = Jinja2Templates(directory=str(Path(__file__).parent.parent / "templates"))

@router.get("/", response_class=HTMLResponse)
async def root(request: Request):
    """Ruta raíz simplificada sin dependencia de DB"""
    try:
        return templates.TemplateResponse(request, "index.html", {})
    except Exception as e:
        logging.error(f"Error en ruta raíz: {e}")
        return HTMLResponse(content=f"<h1>Error: {e}</h1>", status_code=500)

@router.get("/adminPanel", response_class=HTMLResponse)
async def administrador_view(request: Request):
    """Vista de asistencias para profesores"""
    
    # 1.OBTENER EL TOKEN DE LA COOKIE
    token_str = request.cookies.get("access_token")
    
    if not token_str:
        # Si no hay cookie, redirigir al login
        return RedirectResponse(url="/")

    try:
        # 2. DECODIFICAR LA CADENA
        payload = jwt.decode(token_str, SECRET_KEY, algorithms=[ALGORITHM])
        
        # 3. Verificar rol
        rol = payload.get("rol")
        
        # NOTA: Ajusta la lista ["usuario", "admin"] según cómo llames al rol de profesor en tu BD.
        # Si el rol es 'profesor', añádelo a la lista.
        if rol not in ["usuario", "administrador", "profesor"]:
            logging.error(f"Acceso denegado a /profesor para rol: {rol}")
            return RedirectResponse(url="/")

        return templates.TemplateResponse(request, "administrador.html", {
            "user": payload
        })

    except JWTError as e:
        logging.error(f"❌ Token inválido o expirado: {e}")
        return RedirectResponse(url="/")
    except Exception as e:
        logging.error(f"Error en asistencias profesor: {e}")
        raise HTTPException(status_code=500, detail=str(e))



@router.get("/profesor/asistencias", response_class=HTMLResponse)
async def asistencias_profesor(request: Request):
    """Vista de asistencias para profesores"""
    
    # 1.OBTENER EL TOKEN DE LA COOKIE
    token_str = request.cookies.get("access_token")
    
    if not token_str:
        # Si no hay cookie, redirigir al login
        return RedirectResponse(url="/")

    try:
        # 2. DECODIFICAR LA CADENA
        payload = jwt.decode(token_str, SECRET_KEY, algorithms=[ALGORITHM])
        
        # 3. Verificar rol
        rol = payload.get("rol")
        
        # NOTA: Ajusta la lista ["usuario", "admin"] según cómo llames al rol de profesor en tu BD.
        # Si el rol es 'profesor', añádelo a la lista.
        if rol not in ["usuario", "admin", "profesor"]:
            logger.warning(f"Acceso denegado a /profesor para rol: {rol}")
            return RedirectResponse(url="/")

        return templates.TemplateResponse(request, "asistencias_profesor.html", {
            "user": payload
        })

    except JWTError as e:
        logger.error(f"❌ Token inválido o expirado: {e}")
        return RedirectResponse(url="/")
    except Exception as e:
        logger.error(f"Error en asistencias profesor: {e}")
        raise HTTPException(status_code=500, detail=str(e))



@router.get("/conserje", response_class=HTMLResponse)
async def panel_conserjes(request: Request):
    """Panel de conserjes"""
    # 1.  OBTENER EL TOKEN DE LA COOKIE
    token_str = request.cookies.get("access_token")
    if not token_str:
        # Si no hay cookie, redirigir al login
        return RedirectResponse(url="/")
    try:
        # 2. DECODIFICAR LA CADENA (token_str), NO EL MÓDULO
        payload = jwt.decode(token_str, SECRET_KEY, algorithms=[ALGORITHM])
        
        # 3. Verificar rol
        rol = payload.get("rol")
        scope = payload.get("scope")
        
        if rol != "conserje" and scope != "conserje":
             logger.warning(f"Acceso denegado a /conserje para rol: {rol}")
             return RedirectResponse(url="/")

        # 4. Renderizar plantilla pasando el usuario
        return templates.TemplateResponse(request, "conserjes.html", {
            "user": payload,
        })

    except JWTError as e:
        logging.error(f"❌ Token inválido o expirado: {e}")
        return RedirectResponse(url="/")
    except Exception as e:
        logging.error(f"❌ Error en panel conserjes: {e}")
        # En caso de error grave, mejor redirigir que mostrar un 500 al usuario
        return RedirectResponse(url="/")

@router.get("/gestion-aulas", response_class=HTMLResponse)
async def gestion_aulas(request: Request):
    """Gestión de aulas y dispositivos ESP32"""
    try:
        return templates.TemplateResponse(request, "gestion_aulas.html", {})
    except Exception as e:
        logging.error(f"Error en gestión de aulas: {e}")
        raise HTTPException(status_code=500, detail=str(e))


@router.get("/gestion-datos", response_class=HTMLResponse)
async def gestion_datos_view(request: Request):
    """Panel de gestión de datos de la base de datos (CRUD)"""
    # Verificar autenticación y rol de administrador
    token_str = request.cookies.get("access_token")
    
    if not token_str:
        return RedirectResponse(url="/")
    
    try:
        payload = jwt.decode(token_str, SECRET_KEY, algorithms=[ALGORITHM])
        rol = payload.get("rol")
        
        # Solo administradores pueden acceder
        if rol != "administrador":
            logger.warning(f"Acceso denegado a /gestion-datos para rol: {rol}")
            return RedirectResponse(url="/dashboard")
        
        return templates.TemplateResponse(request, "gestion_datos.html", {
            "user": payload
        })
    
    except JWTError as e:
        logger.error(f"❌ Token inválido o expirado: {e}")
        return RedirectResponse(url="/")
    except Exception as e:
        logger.error(f"Error en gestión de datos: {e}")
        raise HTTPException(status_code=500, detail=str(e))