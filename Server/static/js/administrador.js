// ========================================
// CONFIGURACIÓN Y VARIABLES GLOBALES
// ========================================

let aulaSeleccionada = null;
let aulasData = {};
let ultimoEstado = '';
const REFRESCO_MS = 5000;

// ========================================
// ESTADO - CONSULTA HTTP PERIÓDICA (sin WebSocket)
// ========================================

// El servidor no empuja cambios: el panel pregunta cada REFRESCO_MS. Los datos
// de los nodos llegan por LoRaLAN con un sondeo de varios segundos, así que
// consultar más a menudo no daría datos más frescos.
async function cargarEstado() {
    try {
        const r = await fetch('/panel/estado', { credentials: 'include' });
        if (r.status === 401 || r.status === 403) {
            window.location.href = '/';
            return;
        }
        if (!r.ok) throw new Error(`HTTP ${r.status}`);

        const datos = await r.json();
        updateConnectionStatus(true);

        // Solo se repinta si algo cambió (hace_s cambia en cada consulta)
        const huella = JSON.stringify(datos.aulas.map(({ hace_s, ...resto }) => resto));
        aulasData = {};
        datos.aulas.forEach(a => { aulasData[a.aula || a.device_id] = a; });
        if (huella !== ultimoEstado) {
            ultimoEstado = huella;
            renderAulas();
        }
        refrescarAulaSeleccionada();
    } catch (e) {
        console.error('❌ Error consultando estado:', e);
        updateConnectionStatus(false);
    }
}

function updateConnectionStatus(connected) {
    const statusEl = document.getElementById('ws-status');
    if (statusEl) {
        statusEl.textContent = connected ? '🟢 Conectado' : '🔴 Sin servidor';
        statusEl.className = connected ? 'status-connected' : 'status-disconnected';
    }
}

// ========================================
// FUNCIONES DE ENVÍO
// ========================================

// POST JSON al panel. Devuelve true si el servidor lo aceptó.
async function postPanel(ruta, cuerpo) {
    try {
        const r = await fetch(ruta, {
            method: 'POST',
            credentials: 'include',
            headers: { 'Content-Type': 'application/json' },
            body: JSON.stringify(cuerpo)
        });
        if (r.ok) return true;
        const motivos = {
            404: 'el nodo no está en la red LoRaLAN',
            409: 'ese nombre de aula ya está asignado a otro nodo',
            403: 'no tienes permiso'
        };
        mostrarNotificacion('error', `No se pudo completar: ${motivos[r.status] || 'HTTP ' + r.status}`);
    } catch (e) {
        mostrarNotificacion('error', 'Sin conexión con el servidor');
    }
    return false;
}

// La orden queda en la cola del gateway y llega al nodo por radio; el estado
// real se ve cuando el nodo lo confirma.
function enviarToggleRelay(deviceId, relayNum, relayState) {
    return postPanel('/panel/rele', { device_id: deviceId, relay_num: relayNum, relay_state: relayState });
}

// ========================================
// FUNCIONES DE UI - NOTIFICACIONES
// ========================================

function mostrarNotificacion(tipo, mensaje) {
    // Crear notificación
    const notif = document.createElement('div');
    notif.className = `notificacion notif-${tipo}`;
    
    const iconos = {
        'success': '✅',
        'error': '❌',
        'warning': '⚠️',
        'info': 'ℹ️'
    };
    
    notif.innerHTML = `${iconos[tipo] || ''} ${mensaje}`;
    
    // Añadir al DOM
    let container = document.getElementById('notificaciones');
    if (!container) {
        container = document.createElement('div');
        container.id = 'notificaciones';
        container.style.cssText = 'position: fixed; top: 20px; right: 20px; z-index: 1000;';
        document.body.appendChild(container);
    }
    
    container.appendChild(notif);
    
    // Estilos inline para la notificación
    notif.style.cssText = `
        padding: 12px 20px;
        margin-bottom: 10px;
        border-radius: 8px;
        color: white;
        font-weight: 500;
        box-shadow: 0 4px 12px rgba(0,0,0,0.15);
        animation: slideIn 0.3s ease;
    `;
    
    const colores = {
        'success': '#28a745',
        'error': '#dc3545',
        'warning': '#ffc107',
        'info': '#17a2b8'
    };
    notif.style.backgroundColor = colores[tipo] || '#333';
    if (tipo === 'warning') notif.style.color = '#333';
    
    // Remover después de 4 segundos
    setTimeout(() => {
        notif.style.animation = 'slideOut 0.3s ease';
        setTimeout(() => notif.remove(), 300);
    }, 4000);
}

function mostrarResultadoTest(aula) {
    // Diagnóstico por LoRaLAN: el nodo ya no responde a una consulta de prueba
    // (cada consulta gastaría tiempo de aire); se muestra lo último que subió.
    const diagSection = document.getElementById('diagnostico-section');
    if (diagSection) {
        diagSection.style.display = 'block';
    }

    document.getElementById('diag-rssi').textContent = aula.rssi != null ? `${aula.rssi} dBm` : '--';
    document.getElementById('diag-uptime').textContent = aula.hace_s != null ? `hace ${aula.hace_s} s` : '--';
    document.getElementById('diag-heap').textContent = aula.snr != null ? `${aula.snr} dB` : '--';

    // Estado de sensores: con lectura (true), sin lectura (false), no reportado (undefined)
    const leido = (v) => v === undefined ? undefined : v !== null;
    actualizarEstadoSensor('status-nfc', undefined);
    actualizarEstadoSensor('status-radar', undefined);
    actualizarEstadoSensor('status-bme', leido(aula.temperatura));
    actualizarEstadoSensor('status-luz', leido(aula.luz));

    actualizarEstadoRelay('status-relay1', aula.relay1);
    actualizarEstadoRelay('status-relay2', aula.relay2);

    const timestamp = new Date().toLocaleTimeString('es-ES');
    document.getElementById('diag-timestamp').textContent = `Última consulta: ${timestamp}`;
}

function formatUptime(segundos) {
    if (!segundos) return '--';
    const dias = Math.floor(segundos / 86400);
    const horas = Math.floor((segundos % 86400) / 3600);
    const minutos = Math.floor((segundos % 3600) / 60);
    
    if (dias > 0) return `${dias}d ${horas}h ${minutos}m`;
    if (horas > 0) return `${horas}h ${minutos}m`;
    return `${minutos}m`;
}

function actualizarEstadoSensor(elementId, estado) {
    const element = document.getElementById(elementId);
    if (!element) return;
    
    const statusText = element.querySelector('.status-text');
    
    if (estado === true) {
        element.style.borderLeftColor = '#28a745';
        element.style.background = '#d4edda';
        if (statusText) statusText.textContent = '✅ Operativo';
    } else if (estado === false) {
        element.style.borderLeftColor = '#dc3545';
        element.style.background = '#f8d7da';
        if (statusText) statusText.textContent = '❌ Error';
    } else {
        element.style.borderLeftColor = '#ffc107';
        element.style.background = '#fff3cd';
        if (statusText) statusText.textContent = '⚠️ Sin datos';
    }
}

function actualizarEstadoRelay(elementId, estado) {
    const element = document.getElementById(elementId);
    if (!element) return;
    
    const stateText = element.querySelector('.relay-state');
    
    if (estado === true) {
        element.style.borderColor = '#28a745';
        element.style.background = '#d4edda';
        if (stateText) stateText.textContent = '🟢 ENCENDIDO';
    } else if (estado === false) {
        element.style.borderColor = '#6c757d';
        element.style.background = '#f8f9fa';
        if (stateText) stateText.textContent = '⚫ APAGADO';
    } else {
        element.style.borderColor = '#e0e0e0';
        element.style.background = '#f8f9fa';
        if (stateText) stateText.textContent = '--';
    }
}

// ========================================
// FUNCIONES DE ACTUALIZACIÓN DE ESTADO
// ========================================

function textoEnlace(aula) {
    if (!aula.gateway) return 'N/A';
    const rssi = aula.rssi != null ? `, RSSI ${aula.rssi} dBm` : '';
    return `${aula.gateway}${rssi}`;
}

// Tras cada consulta, refresca los datos del aula abierta en el formulario.
function refrescarAulaSeleccionada() {
    if (!aulaSeleccionada) return;
    const aula = Object.values(aulasData).find(a => a.device_id === aulaSeleccionada.device_id);
    if (!aula) return;

    document.getElementById('sensor-temp').textContent = aula.temperatura != null ? `${aula.temperatura}°C` : 'N/A';
    document.getElementById('sensor-humedad').textContent = aula.humedad != null ? `${aula.humedad}%` : 'N/A';
    document.getElementById('sensor-presion').textContent = aula.presion != null ? `${aula.presion} hPa` : 'N/A';
    document.getElementById('sensor-luz').textContent = aula.luz != null ? `${aula.luz} lux` : 'N/A';
    document.getElementById('sensor-personas').textContent = aula.personas != null ? aula.personas : 'N/A';
    document.getElementById('ip-address').value = textoEnlace(aula);
}

// ========================================
// RENDERIZADO Y SELECCIÓN DE AULAS
// ========================================

function renderAulas() {
    const container = document.getElementById('aulas-list');
    if (!container) return;
    
    container.innerHTML = '';
    
    const aulasKeys = Object.keys(aulasData);
    
    if (aulasKeys.length === 0) {
        container.innerHTML = '<p class="empty-message">No hay dispositivos conectados</p>';
        return;
    }
    
    console.log('🎨 Renderizando aulas:', aulasData);

    aulasKeys.forEach(aulaKey => {
        const aula = aulasData[aulaKey];
        const card = document.createElement('div');
        const isSelected = aulaSeleccionada && aulaSeleccionada.device_id === aula.device_id;
        card.className = `aula-card ${isSelected ? 'active' : ''} ${aula.conectado ? '' : 'disconnected'}`;
        card.onclick = () => seleccionarAula(aulaKey, aula);
        
        const estado = aula.conectado ? 'conectado' : 'desconectado';
        const estadoClass = aula.conectado ? 'disponible' : 'desconectada';
        
        const personas = aula.personas != null ? aula.personas : 0;
        const hayClase = personas > 0;
        
        card.innerHTML = `
            <div class="aula-header">
                <span class="aula-name">${aulaKey}</span>
                <span class="status-badge ${estadoClass}">${estado.toUpperCase()}</span>
            </div>
            <div class="aula-info">
                <div class="info-item">
                    <span class="info-label">Device ID:</span>
                    <span>${aula.device_id || 'N/A'}</span>
                </div>
                <div class="info-item">
                    <span class="info-label">Clase:</span>
                    <span>${hayClase ? '📚 EN CLASE' : '🚫 SIN CLASE'}</span>
                </div>
                <div class="info-item">
                    <span class="info-label">Calefacción:</span>
                    <span>${aula.relay1 ? '🔥 ON' : '❄️ OFF'}</span>
                </div>
                <div class="info-item">
                    <span class="info-label">Iluminación:</span>
                    <span>${aula.relay2 ? '💡 ON' : '🌑 OFF'}</span>
                </div>
            </div>
        `;
        
        container.appendChild(card);
    });
}

function seleccionarAula(aulaKey, aula) {
    aulaSeleccionada = { ...aula, aulaKey: aulaKey };

    document.getElementById('empty-state').classList.add('hidden');
    document.getElementById('form-container').classList.add('active');

    // Ocultar sección de diagnóstico al cambiar de aula
    const diagSection = document.getElementById('diagnostico-section');
    if (diagSection) diagSection.style.display = 'none';

    document.getElementById('device-id').value = aula.device_id || '';
    document.getElementById('aula-nombre').value = aula.aula || '';
    document.getElementById('calefaccion-enabled').checked = aula.relay1 || false;
    document.getElementById('iluminacion-enabled').checked = aula.relay2 || false;
    refrescarAulaSeleccionada();

    renderAulas();
}

// ========================================
// EVENT LISTENERS
// ========================================

function setupEventListeners() {
    // Guardar cambios: nombre del aula (en BD) y relés (orden al nodo)
    const form = document.getElementById('aula-form');
    if (form) {
        form.addEventListener('submit', async (e) => {
            e.preventDefault();
            if (!aulaSeleccionada) return;

            const deviceId = aulaSeleccionada.device_id;
            const nombre = document.getElementById('aula-nombre').value.trim();
            const relay1 = document.getElementById('calefaccion-enabled').checked;
            const relay2 = document.getElementById('iluminacion-enabled').checked;

            let ok = true;
            if (nombre !== (aulaSeleccionada.aula || '')) {
                ok = await postPanel('/panel/aula', { device_id: deviceId, nombre });
                if (ok) aulaSeleccionada.aula = nombre;
            }
            if (ok && relay1 !== !!aulaSeleccionada.relay1) ok = await enviarToggleRelay(deviceId, 1, relay1);
            if (ok && relay2 !== !!aulaSeleccionada.relay2) ok = await enviarToggleRelay(deviceId, 2, relay2);

            if (ok) mostrarNotificacion('success', 'Cambios enviados');
            ultimoEstado = '';
            cargarEstado();
        });
    }

    // Resetear nodo
    const btnReset = document.getElementById('btn-reset');
    if (btnReset) {
        btnReset.addEventListener('click', async () => {
            if (!aulaSeleccionada) return;
            if (!confirm(`¿Resetear ${aulaSeleccionada.aulaKey}?`)) return;

            if (await postPanel('/panel/reset', { device_id: aulaSeleccionada.device_id })) {
                mostrarNotificacion('info', 'Orden de reset enviada al gateway');
            }
        });
    }

    // Cancelar edición
    const btnCancel = document.getElementById('btn-cancel');
    if (btnCancel) {
        btnCancel.addEventListener('click', () => {
            aulaSeleccionada = null;
            document.getElementById('form-container').classList.remove('active');
            document.getElementById('empty-state').classList.remove('hidden');
            const diagSection = document.getElementById('diagnostico-section');
            if (diagSection) diagSection.style.display = 'none';
            renderAulas();
        });
    }

    // Diagnóstico: lo último que el nodo subió por radio
    const btnDiagnostico = document.getElementById('btn-diagnostico');
    if (btnDiagnostico) {
        btnDiagnostico.addEventListener('click', () => {
            if (!aulaSeleccionada) return;
            const aula = Object.values(aulasData).find(a => a.device_id === aulaSeleccionada.device_id);
            if (aula) mostrarResultadoTest(aula);
        });
    }

    // Refrescar sensores / actualizar lista: consulta inmediata
    ['btn-refresh', 'btn-actualizar'].forEach(id => {
        const btn = document.getElementById(id);
        if (btn) btn.addEventListener('click', () => { ultimoEstado = ''; cargarEstado(); });
    });

    // Los interruptores de relé del formulario se aplican con "Guardar Cambios":
    // así el refresco periódico no pisa lo que el usuario está editando.
}

// ========================================
// INICIALIZACIÓN
// ========================================

document.addEventListener('DOMContentLoaded', () => {
    console.log('🚀 Inicializando panel admin');

    // Añadir estilos para animaciones
    const style = document.createElement('style');
    style.textContent = `
        @keyframes slideIn {
            from { transform: translateX(100%); opacity: 0; }
            to { transform: translateX(0); opacity: 1; }
        }
        @keyframes slideOut {
            from { transform: translateX(0); opacity: 1; }
            to { transform: translateX(100%); opacity: 0; }
        }
        .aula-card.disconnected { opacity: 0.6; }
        .empty-message { text-align: center; color: #666; padding: 20px; }
    `;
    document.head.appendChild(style);

    setupEventListeners();
    renderAulas();
    cargarEstado();
    setInterval(cargarEstado, REFRESCO_MS);
});

function cerrarSesion() {
    sessionStorage.clear();
    localStorage.clear();

    fetch('/logout', {
        method: 'GET',
        credentials: 'include'
    })
    .then(() => {
        window.location.href = '/';
    })
    .catch(() => {
        window.location.href = '/';
    });
}

// =================================================================
// UTILIDADES
// =================================================================

function escapeHTML(str) {
    if (!str) return '';
    const div = document.createElement('div');
    div.appendChild(document.createTextNode(str));
    return div.innerHTML;
}

function showToast(mensaje, tipo) {
    mostrarNotificacion(tipo, mensaje);
}

// =================================================================
// GESTIÓN DE USUARIOS
// =================================================================

const userManagementModal = document.getElementById('user-management-modal');
const userFormModal = document.getElementById('user-form-modal');
const btnGestionarUsuarios = document.getElementById('btn-gestionar-usuarios');
const modalCloseBtn = document.getElementById('modal-close-btn');
const userFormCloseBtn = document.getElementById('user-form-close-btn');
const btnAddUser = document.getElementById('btn-add-user');
const userListTbody = document.getElementById('user-list-tbody');
const userForm = document.getElementById('user-form');
const userFormTitle = document.getElementById('user-form-title');
const btnCancelUserForm = document.getElementById('btn-cancel-user-form');

// Form fields
const userIdInput = document.getElementById('user-id');
const userNameInput = document.getElementById('user-nombre');
const userEmailInput = document.getElementById('user-correo');
const userRolInput = document.getElementById('user-rol');
const userUidInput = document.getElementById('user-uid');
const userPasswordInput = document.getElementById('user-password');


// --- API Calls ---
const apiFetch = async (url, options = {}) => {
    // Intentar obtener token de localStorage o de la cookie (HttpOnly se envía automáticamente)
    const token = localStorage.getItem('accessToken') || localStorage.getItem('token');
    const headers = {
        'Content-Type': 'application/json',
        ...(token ? { 'Authorization': `Bearer ${token}` } : {}),
        ...options.headers,
    };

    const response = await fetch(url, { ...options, headers });

    if (!response.ok) {
        const errorData = await response.json().catch(() => ({ detail: 'Error desconocido' }));
        const errorMessage = errorData.detail || `Error ${response.status}`;
        mostrarNotificacion('error', errorMessage);
        throw new Error(errorMessage);
    }
    // For 204 No Content
    if (response.status === 204) {
        return null;
    }
    return response.json();
};

const getUsers = () => apiFetch('/api/v1/usuarios');
const createUser = (data) => apiFetch('/api/v1/usuarios', { method: 'POST', body: JSON.stringify(data) });
const updateUser = (id, data) => apiFetch(`/api/v1/usuarios/${id}`, { method: 'PUT', body: JSON.stringify(data) });
const deleteUser = (id) => apiFetch(`/api/v1/usuarios/${id}`, { method: 'DELETE' });


// --- Modal Control ---
const openUserListModal = async () => {
    try {
        const users = await getUsers();
        renderUserList(users);
        userManagementModal.style.display = 'flex';
    } catch (error) {
        console.error('Error al abrir modal de usuarios:', error);
    }
};

const openUserFormModal = (user = null) => {
    userForm.reset();
    if (user) {
        userFormTitle.textContent = 'Editar Usuario';
        userIdInput.value = user.id;
        userNameInput.value = user.nombre;
        userEmailInput.value = user.correo;
        userRolInput.value = user.rol;
        userUidInput.value = user.uid_tarjeta || '';
        userPasswordInput.placeholder = 'Dejar en blanco para no cambiar';
    } else {
        userFormTitle.textContent = 'Añadir Usuario';
        userIdInput.value = '';
        userPasswordInput.placeholder = 'Mínimo 8 caracteres';
    }
    userFormModal.style.display = 'flex';
};

const closeModals = () => {
    userManagementModal.style.display = 'none';
    userFormModal.style.display = 'none';
};

// --- Rendering ---
const renderUserList = (users) => {
    userListTbody.innerHTML = '';
    if (users.length === 0) {
        userListTbody.innerHTML = '<tr><td colspan="4" style="text-align: center;">No hay usuarios registrados.</td></tr>';
        return;
    }
    users.forEach(user => {
        const tr = document.createElement('tr');
        tr.innerHTML = `
            <td>${escapeHTML(user.nombre)}</td>
            <td>${escapeHTML(user.correo)}</td>
            <td>${escapeHTML(user.rol)}</td>
            <td class="user-actions">
                <button class="btn btn-secondary btn-edit-user" data-user-id="${user.id}">Editar</button>
                <button class="btn btn-danger btn-delete-user" data-user-id="${user.id}">Eliminar</button>
            </td>
        `;
        userListTbody.appendChild(tr);
    });
};


// --- Event Listeners ---
btnGestionarUsuarios.addEventListener('click', openUserListModal);
modalCloseBtn.addEventListener('click', closeModals);
userFormCloseBtn.addEventListener('click', closeModals);
btnCancelUserForm.addEventListener('click', () => userFormModal.style.display = 'none');
btnAddUser.addEventListener('click', () => openUserFormModal());

userListTbody.addEventListener('click', async (e) => {
    const target = e.target;
    const userId = target.dataset.userId;

    if (target.classList.contains('btn-edit-user')) {
        try {
            const user = await apiFetch(`/api/v1/usuarios/${userId}`);
            openUserFormModal(user);
        } catch (error) {
            console.error(`Error al obtener datos del usuario ${userId}:`, error);
        }
    }

    if (target.classList.contains('btn-delete-user')) {
        if (confirm('¿Estás seguro de que quieres eliminar este usuario? Esta acción no se puede deshacer.')) {
            try {
                await deleteUser(userId);
                showToast('Usuario eliminado correctamente.', 'success');
                openUserListModal(); // Recargar lista
            } catch (error) {
                console.error(`Error al eliminar usuario ${userId}:`, error);
            }
        }
    }
});

userForm.addEventListener('submit', async (e) => {
    e.preventDefault();
    const id = userIdInput.value;
    const isUpdating = !!id;

    const userData = {
        nombre: userNameInput.value,
        correo: userEmailInput.value,
        rol: userRolInput.value,
        uid_tarjeta: userUidInput.value || null,
    };

    if (userPasswordInput.value) {
        userData.password = userPasswordInput.value;
    } else if (!isUpdating) {
        showToast('La contraseña es obligatoria para nuevos usuarios.', 'error');
        return;
    }

    try {
        if (isUpdating) {
            await updateUser(id, userData);
            showToast('Usuario actualizado correctamente.', 'success');
        } else {
            await createUser(userData);
            showToast('Usuario creado correctamente.', 'success');
        }
        closeModals();
        openUserListModal(); // Recargar lista
    } catch (error) {
        console.error('Error al guardar usuario:', error);
        // El error ya se muestra en el toast desde apiFetch
    }
});

// Cerrar modal al hacer clic fuera
window.addEventListener('click', (e) => {
    if (e.target === userManagementModal || e.target === userFormModal) {
        closeModals();
    }
});
