// ========================================
// GESTIÓN DE DATOS - JavaScript
// Sistema de Control de Asistencia UEx
// ========================================

// ============ CONFIGURACIÓN ============
const API_BASE = '/api/v1';
let currentSection = 'usuarios';
let currentData = [];
let editingId = null;
let deleteId = null;

// Configuracion de cada seccion
const SECTIONS_CONFIG = {
    usuarios: {
        title: 'Gestion de Usuarios',
        subtitle: 'Administra los usuarios del sistema',
        endpoint: `${API_BASE}/usuarios`,
        columns: [
            { key: 'id', label: 'ID', type: 'id' },
            { key: 'nombre', label: 'Nombre', type: 'text' },
            { key: 'correo', label: 'Correo', type: 'email' },
            { key: 'rol', label: 'Rol', type: 'badge' },
            { key: 'uid_tarjeta', label: 'UID Tarjeta', type: 'mono' }
        ],
        fields: [
            { name: 'nombre', label: 'Nombre Completo', type: 'text', required: true, placeholder: 'Ej: Juan Perez' },
            { name: 'correo', label: 'Correo Electronico', type: 'email', required: true, placeholder: 'usuario@example.com' },
            { name: 'rol', label: 'Rol', type: 'select', required: true, options: [
                { value: 'profesor', label: 'Profesor' },
                { value: 'conserje', label: 'Conserje' },
                { value: 'administrador', label: 'Administrador' }
            ]},
            { name: 'uid_tarjeta', label: 'UID Tarjeta NFC', type: 'text', required: false, placeholder: 'Ej: 1A2B3C4D' },
            { name: 'password', label: 'Contrasena', type: 'password', required: false, placeholder: 'Dejar vacio para no cambiar', hint: 'Minimo 8 caracteres' }
        ],
        stats: [
            { icon: 'bi-people-fill', label: 'Total Usuarios', key: 'total', color: 'green' },
            { icon: 'bi-mortarboard-fill', label: 'Profesores', key: 'profesores', color: 'blue', filterable: 'profesor' },
            { icon: 'bi-person-badge-fill', label: 'Conserjes', key: 'conserjes', color: 'orange', filterable: 'conserje' },
            { icon: 'bi-shield-fill', label: 'Administradores', key: 'administradores', color: 'purple', filterable: 'administrador' }
        ]
    },
    aulas: {
        title: 'Gestion de Aulas',
        subtitle: 'Configura las aulas del sistema',
        endpoint: `${API_BASE}/aulas-crud`,
        columns: [
            { key: 'id', label: 'ID', type: 'id' },
            { key: 'nombre', label: 'Nombre del Aula', type: 'text' }
        ],
        fields: [
            { name: 'nombre', label: 'Nombre del Aula', type: 'text', required: true, placeholder: 'Ej: Aula 101' }
        ],
        stats: [
            { icon: 'bi-building', label: 'Total Aulas', key: 'total', color: 'green' }
        ]
    },
    clases: {
        title: 'Gestion de Clases',
        subtitle: 'Programa las clases y horarios',
        endpoint: `${API_BASE}/clases-crud`,
        columns: [
            { key: 'id', label: 'ID', type: 'id' },
            { key: 'asignatura', label: 'Asignatura', type: 'text' },
            { key: 'aula_nombre', label: 'Aula', type: 'badge' },
            { key: 'profesor_nombre', label: 'Profesor', type: 'text' },
            { key: 'hora_inicio', label: 'Inicio', type: 'datetime' },
            { key: 'hora_fin', label: 'Fin', type: 'datetime' }
        ],
        fields: [
            { name: 'asignatura', label: 'Asignatura', type: 'text', required: true, placeholder: 'Ej: Matematicas I' },
            { name: 'aula_id', label: 'Aula', type: 'select', required: true, options: [], dynamic: 'aulas' },
            { name: 'profesor_id', label: 'Profesor', type: 'select', required: true, options: [], dynamic: 'profesores' },
            { name: 'hora_inicio', label: 'Hora de Inicio', type: 'datetime-local', required: true },
            { name: 'hora_fin', label: 'Hora de Fin', type: 'datetime-local', required: true }
        ],
        stats: [
            { icon: 'bi-journal-bookmark-fill', label: 'Total Clases', key: 'total', color: 'green' },
            { icon: 'bi-calendar-check', label: 'Hoy', key: 'hoy', color: 'blue' },
            { icon: 'bi-calendar-week', label: 'Esta Semana', key: 'semana', color: 'orange' }
        ]
    },
    asistencias: {
        title: 'Registro de Asistencias',
        subtitle: 'Historial de asistencias registradas',
        endpoint: `${API_BASE}/asistencias-crud`,
        columns: [
            { key: 'id', label: 'ID', type: 'id' },
            { key: 'usuario_nombre', label: 'Usuario', type: 'text' },
            { key: 'aula_nombre', label: 'Aula', type: 'badge' },
            { key: 'fecha_hora', label: 'Fecha y Hora', type: 'datetime' }
        ],
        fields: [
            { name: 'usuario_id', label: 'Usuario', type: 'select', required: true, options: [], dynamic: 'usuarios' },
            { name: 'aula_id', label: 'Aula', type: 'select', required: true, options: [], dynamic: 'aulas' },
            { name: 'fecha_hora', label: 'Fecha y Hora', type: 'datetime-local', required: true }
        ],
        stats: [
            { icon: 'bi-clipboard-check-fill', label: 'Total Registros', key: 'total', color: 'green' },
            { icon: 'bi-calendar-check', label: 'Hoy', key: 'hoy', color: 'blue' },
            { icon: 'bi-calendar-month', label: 'Este Mes', key: 'mes', color: 'orange' }
        ],
        readonly: false // Se pueden crear/editar asistencias manualmente
    }
};

// Variable para filtro activo
let activeFilter = null;

// ============ INICIALIZACION ============
document.addEventListener('DOMContentLoaded', () => {
    initNavigation();
    initModals();
    initSearch();
    initLogout();
    initFilterPanel();
    initMobileMenu();
    loadSection(currentSection);
});

function initMobileMenu() {
    const sidebar = document.getElementById('sidebar');
    const sidebarOverlay = document.getElementById('sidebar-overlay');
    const menuToggle = document.getElementById('btn-menu-toggle');
    
    if (!menuToggle) return;
    
    // Abrir sidebar
    menuToggle.addEventListener('click', () => {
        sidebar.classList.add('open');
        sidebarOverlay.classList.add('show');
    });
    
    // Cerrar sidebar al hacer clic en overlay
    sidebarOverlay.addEventListener('click', () => {
        sidebar.classList.remove('open');
        sidebarOverlay.classList.remove('show');
    });
    
    // Cerrar sidebar al seleccionar una seccion
    const navItems = document.querySelectorAll('.nav-item');
    navItems.forEach(item => {
        item.addEventListener('click', () => {
            if (window.innerWidth <= 768) {
                sidebar.classList.remove('open');
                sidebarOverlay.classList.remove('show');
            }
        });
    });
}

function initNavigation() {
    const navItems = document.querySelectorAll('.nav-item');
    navItems.forEach(item => {
        item.addEventListener('click', () => {
            const section = item.dataset.section;
            if (section !== currentSection) {
                navItems.forEach(nav => nav.classList.remove('active'));
                item.classList.add('active');
                currentSection = section;
                loadSection(section);
            }
        });
    });
}

function initModals() {
    // Modal de formulario
    document.getElementById('modal-close').addEventListener('click', closeModal);
    document.getElementById('btn-cancel-form').addEventListener('click', closeModal);
    document.getElementById('entity-form').addEventListener('submit', handleFormSubmit);
    
    // Boton eliminar en modal de edicion
    document.getElementById('btn-delete-from-modal').addEventListener('click', () => {
        if (editingId) {
            const item = currentData.find(i => i.id === editingId);
            let itemName = '';
            if (item) {
                if (item.nombre) itemName = item.nombre;
                else if (item.asignatura) itemName = item.asignatura;
                else itemName = `ID #${editingId}`;
            }
            deleteId = editingId;
            document.getElementById('confirm-message').textContent = 
                `Estas seguro de eliminar "${itemName}"? Esta accion no se puede deshacer.`;
            showModal('modal-confirm');
        }
    });
    
    // Modal de confirmacion
    document.getElementById('btn-cancel-delete').addEventListener('click', closeConfirmModal);
    document.getElementById('btn-confirm-delete').addEventListener('click', handleDelete);
    
    // Cerrar modales al hacer clic fuera
    document.querySelectorAll('.modal-overlay').forEach(modal => {
        modal.addEventListener('click', (e) => {
            if (e.target === modal) {
                closeModal();
                closeConfirmModal();
            }
        });
    });
    
    // Botón añadir nuevo
    document.getElementById('btn-add-new').addEventListener('click', openAddModal);
}

function initSearch() {
    const searchInput = document.getElementById('search-input');
    let searchTimeout;
    
    searchInput.addEventListener('input', (e) => {
        clearTimeout(searchTimeout);
        searchTimeout = setTimeout(() => {
            filterData(e.target.value);
        }, 300);
    });
}

function initLogout() {
    document.getElementById('btn-logout').addEventListener('click', () => {
        if (typeof cerrarSesion === 'function') {
            cerrarSesion();
        } else {
            document.cookie = 'access_token=; Path=/; Expires=Thu, 01 Jan 1970 00:00:01 GMT;';
            window.location.href = '/';
        }
    });
}

function initFilterPanel() {
    document.getElementById('filter-panel-close').addEventListener('click', closeFilterPanel);
}

// ============ CARGA DE DATOS ============
async function loadSection(section) {
    const config = SECTIONS_CONFIG[section];
    
    // Actualizar título y subtítulo
    document.getElementById('section-title').textContent = config.title;
    document.getElementById('section-subtitle').textContent = config.subtitle;
    document.getElementById('search-input').value = '';
    
    // Mostrar loading
    showLoading();
    
    try {
        // Cargar datos
        const response = await fetchWithAuth(config.endpoint);
        if (!response.ok) {
            if (response.status === 404) {
                currentData = [];
            } else {
                throw new Error('Error al cargar datos');
            }
        } else {
            currentData = await response.json();
        }
        
        // Renderizar tabla y stats
        renderTable(currentData);
        renderStats(currentData);
        updateBadges();
        
    } catch (error) {
        console.error('Error:', error);
        showToast('error', 'Error', 'No se pudieron cargar los datos');
        currentData = [];
        renderTable([]);
    }
}

async function loadDynamicOptions(field) {
    try {
        if (field.dynamic === 'aulas') {
            const response = await fetchWithAuth(`${API_BASE}/aulas-crud`);
            if (response.ok) {
                const aulas = await response.json();
                field.options = aulas.map(a => ({ value: a.id, label: a.nombre }));
            }
        } else if (field.dynamic === 'profesores') {
            const response = await fetchWithAuth(`${API_BASE}/usuarios`);
            if (response.ok) {
                const usuarios = await response.json();
                field.options = usuarios
                    .filter(u => u.rol === 'profesor')
                    .map(u => ({ value: u.id, label: u.nombre }));
            }
        } else if (field.dynamic === 'usuarios') {
            const response = await fetchWithAuth(`${API_BASE}/usuarios`);
            if (response.ok) {
                const usuarios = await response.json();
                field.options = usuarios.map(u => ({ value: u.id, label: `${u.nombre} (${u.rol})` }));
            }
        }
    } catch (error) {
        console.error('Error cargando opciones dinámicas:', error);
    }
}

// ============ RENDERIZADO ============
function renderTable(data) {
    const config = SECTIONS_CONFIG[currentSection];
    const thead = document.getElementById('table-head');
    const tbody = document.getElementById('table-body');
    const emptyState = document.getElementById('empty-state');
    const loadingState = document.getElementById('loading-state');
    const tableSection = document.querySelector('.table-section');
    
    loadingState.style.display = 'none';
    
    if (data.length === 0) {
        thead.innerHTML = '';
        tbody.innerHTML = '';
        emptyState.style.display = 'block';
        return;
    }
    
    emptyState.style.display = 'none';
    
    // Renderizar headers
    thead.innerHTML = `
        <tr>
            ${config.columns.map(col => `<th>${col.label}</th>`).join('')}
            <th>Acciones</th>
        </tr>
    `;
    
    // Renderizar filas
    tbody.innerHTML = data.map((item, index) => `
        <tr style="animation-delay: ${index * 0.03}s">
            ${config.columns.map(col => renderCell(item[col.key], col.type, item)).join('')}
            <td>
                <div class="actions-cell">
                    <button class="btn-action btn-edit" onclick="openEditModal(${item.id})" title="Editar">
                        <i class="bi bi-pencil-fill"></i>
                    </button>
                    <button class="btn-action btn-delete" onclick="confirmDelete(${item.id})" title="Eliminar">
                        <i class="bi bi-trash3-fill"></i>
                    </button>
                </div>
            </td>
        </tr>
    `).join('');
}

function renderCell(value, type, item) {
    if (value === null || value === undefined) {
        return '<td><span style="color: var(--texto-terciario)">—</span></td>';
    }
    
    switch (type) {
        case 'id':
            return `<td><span class="cell-id">#${value}</span></td>`;
        case 'email':
            return `<td><span class="cell-email">${escapeHtml(value)}</span></td>`;
        case 'badge':
            return `<td><span class="cell-badge ${value.toLowerCase()}">${escapeHtml(value)}</span></td>`;
        case 'datetime':
            return `<td><span class="cell-datetime">${formatDateTime(value)}</span></td>`;
        case 'mono':
            return `<td><code style="font-family: var(--font-mono); font-size: 13px; color: var(--texto-secundario);">${escapeHtml(value)}</code></td>`;
        default:
            return `<td>${escapeHtml(value)}</td>`;
    }
}

function renderStats(data) {
    const config = SECTIONS_CONFIG[currentSection];
    const statsRow = document.getElementById('stats-row');
    
    const stats = calculateStats(data);
    
    statsRow.innerHTML = config.stats.map(stat => {
        const isClickable = stat.filterable ? 'clickable' : '';
        const dataFilter = stat.filterable ? `data-filter="${stat.filterable}"` : '';
        
        return `
        <div class="stat-card ${isClickable}" ${dataFilter} ${stat.filterable ? `onclick="showFilteredList('${stat.filterable}', '${stat.label}')"` : ''}>
            <div class="stat-icon ${stat.color}"><i class="bi ${stat.icon}"></i></div>
            <div class="stat-info">
                <h4>${stat.label}</h4>
                <div class="stat-value">${stats[stat.key] || 0}</div>
            </div>
            ${stat.filterable ? '<i class="bi bi-chevron-right stat-arrow"></i>' : ''}
        </div>
        `;
    }).join('');
}

function calculateStats(data) {
    const stats = { total: data.length };
    
    if (currentSection === 'usuarios') {
        stats.profesores = data.filter(u => u.rol === 'profesor').length;
        stats.conserjes = data.filter(u => u.rol === 'conserje').length;
        stats.administradores = data.filter(u => u.rol === 'administrador').length;
    } else if (currentSection === 'clases' || currentSection === 'asistencias') {
        const today = new Date().toDateString();
        const weekStart = new Date();
        weekStart.setDate(weekStart.getDate() - weekStart.getDay());
        const monthStart = new Date();
        monthStart.setDate(1);
        
        const dateKey = currentSection === 'clases' ? 'hora_inicio' : 'fecha_hora';
        
        stats.hoy = data.filter(item => {
            const date = new Date(item[dateKey]);
            return date.toDateString() === today;
        }).length;
        
        stats.semana = data.filter(item => {
            const date = new Date(item[dateKey]);
            return date >= weekStart;
        }).length;
        
        stats.mes = data.filter(item => {
            const date = new Date(item[dateKey]);
            return date >= monthStart;
        }).length;
    }
    
    return stats;
}

function updateBadges() {
    // Actualizar badges de navegación con conteo
    Object.keys(SECTIONS_CONFIG).forEach(async section => {
        try {
            const response = await fetchWithAuth(SECTIONS_CONFIG[section].endpoint);
            if (response.ok) {
                const data = await response.json();
                const badge = document.getElementById(`badge-${section}`);
                if (badge) badge.textContent = data.length;
            }
        } catch (error) {
            // Silenciar errores de badges
        }
    });
}

// ============ MODALES ============
async function openAddModal() {
    editingId = null;
    document.getElementById('modal-title').textContent = `Anadir ${getSingular(currentSection)}`;
    document.getElementById('btn-delete-from-modal').style.display = 'none';
    await renderFormFields();
    document.getElementById('entity-form').reset();
    showModal('modal-form');
}

async function openEditModal(id) {
    editingId = id;
    document.getElementById('modal-title').textContent = `Editar ${getSingular(currentSection)}`;
    document.getElementById('btn-delete-from-modal').style.display = 'flex';
    await renderFormFields();
    
    // Cargar datos del item
    const item = currentData.find(i => i.id === id);
    if (item) {
        populateForm(item);
    }
    
    showModal('modal-form');
}

async function renderFormFields() {
    const config = SECTIONS_CONFIG[currentSection];
    const fieldsContainer = document.getElementById('form-fields');
    
    // Cargar opciones dinámicas si es necesario
    for (const field of config.fields) {
        if (field.dynamic) {
            await loadDynamicOptions(field);
        }
    }
    
    fieldsContainer.innerHTML = config.fields.map(field => {
        // En edición, no mostrar contraseña como requerida
        const isRequired = editingId && field.name === 'password' ? false : field.required;
        const requiredMark = isRequired ? '<span style="color: var(--danger)">*</span>' : '';
        
        let inputHtml = '';
        
        if (field.type === 'select') {
            inputHtml = `
                <select name="${field.name}" id="field-${field.name}" ${isRequired ? 'required' : ''}>
                    <option value="">Seleccionar...</option>
                    ${field.options.map(opt => `
                        <option value="${opt.value}">${escapeHtml(opt.label)}</option>
                    `).join('')}
                </select>
            `;
        } else if (field.type === 'textarea') {
            inputHtml = `
                <textarea name="${field.name}" id="field-${field.name}" 
                    placeholder="${field.placeholder || ''}"
                    ${isRequired ? 'required' : ''}
                    rows="4"></textarea>
            `;
        } else {
            inputHtml = `
                <input type="${field.type}" name="${field.name}" id="field-${field.name}" 
                    placeholder="${field.placeholder || ''}"
                    ${isRequired ? 'required' : ''}
                    ${field.type === 'password' && !editingId ? 'minlength="8"' : ''}>
            `;
        }
        
        return `
            <div class="form-group">
                <label for="field-${field.name}">${field.label} ${requiredMark}</label>
                ${inputHtml}
                ${field.hint ? `<p class="form-hint">${field.hint}</p>` : ''}
            </div>
        `;
    }).join('');
}

function populateForm(item) {
    const config = SECTIONS_CONFIG[currentSection];
    
    config.fields.forEach(field => {
        const input = document.getElementById(`field-${field.name}`);
        if (!input) return;
        
        let value = item[field.name];
        
        // Formato especial para datetime-local
        if (field.type === 'datetime-local' && value) {
            const date = new Date(value);
            value = date.toISOString().slice(0, 16);
        }
        
        if (value !== null && value !== undefined) {
            input.value = value;
        }
    });
}

function confirmDelete(id) {
    deleteId = id;
    const config = SECTIONS_CONFIG[currentSection];
    const item = currentData.find(i => i.id === id);
    
    let itemName = '';
    if (item) {
        if (item.nombre) itemName = item.nombre;
        else if (item.asignatura) itemName = item.asignatura;
        else itemName = `ID #${id}`;
    }
    
    document.getElementById('confirm-message').textContent = 
        `¿Estás seguro de eliminar "${itemName}"? Esta acción no se puede deshacer.`;
    
    showModal('modal-confirm');
}

function showModal(modalId) {
    const modal = document.getElementById(modalId);
    modal.style.display = 'flex';
    setTimeout(() => modal.classList.add('show'), 10);
}

function closeModal() {
    const modal = document.getElementById('modal-form');
    modal.classList.remove('show');
    setTimeout(() => modal.style.display = 'none', 300);
    editingId = null;
}

function closeConfirmModal() {
    const modal = document.getElementById('modal-confirm');
    modal.classList.remove('show');
    setTimeout(() => modal.style.display = 'none', 300);
    deleteId = null;
}

// ============ OPERACIONES CRUD ============
async function handleFormSubmit(e) {
    e.preventDefault();
    
    const config = SECTIONS_CONFIG[currentSection];
    const formData = new FormData(e.target);
    const data = Object.fromEntries(formData.entries());
    
    // Limpiar campos vacíos (para no enviar password vacío en update)
    Object.keys(data).forEach(key => {
        if (data[key] === '') delete data[key];
    });
    
    try {
        let response;
        
        if (editingId) {
            // Actualizar
            response = await fetchWithAuth(`${config.endpoint}/${editingId}`, {
                method: 'PUT',
                headers: { 'Content-Type': 'application/json' },
                body: JSON.stringify(data)
            });
        } else {
            // Crear
            response = await fetchWithAuth(config.endpoint, {
                method: 'POST',
                headers: { 'Content-Type': 'application/json' },
                body: JSON.stringify(data)
            });
        }
        
        if (!response.ok) {
            const error = await response.json();
            throw new Error(error.detail || 'Error en la operación');
        }
        
        showToast('success', 'Éxito', editingId ? 'Registro actualizado correctamente' : 'Registro creado correctamente');
        closeModal();
        loadSection(currentSection);
        
    } catch (error) {
        console.error('Error:', error);
        showToast('error', 'Error', error.message);
    }
}

async function handleDelete() {
    if (!deleteId) return;
    
    const config = SECTIONS_CONFIG[currentSection];
    const currentFilter = activeFilter; // Guardar filtro activo antes de borrar
    
    try {
        const response = await fetchWithAuth(`${config.endpoint}/${deleteId}`, {
            method: 'DELETE'
        });
        
        if (!response.ok && response.status !== 204) {
            const error = await response.json();
            throw new Error(error.detail || 'Error al eliminar');
        }
        
        showToast('success', 'Eliminado', 'El registro ha sido eliminado');
        closeConfirmModal();
        closeModal(); // Cerrar modal de edicion si estaba abierto
        await loadSection(currentSection);
        
        // Si habia un panel de filtro abierto, actualizarlo
        if (currentFilter && currentSection === 'usuarios') {
            const labels = { profesor: 'Profesores', conserje: 'Conserjes', administrador: 'Administradores' };
            showFilteredList(currentFilter, labels[currentFilter] || currentFilter);
        }
        
    } catch (error) {
        console.error('Error:', error);
        showToast('error', 'Error', error.message);
    }
}

// ============ BÚSQUEDA Y FILTRADO ============
function filterData(query) {
    if (!query.trim()) {
        renderTable(currentData);
        return;
    }
    
    const searchTerm = query.toLowerCase();
    const config = SECTIONS_CONFIG[currentSection];
    
    const filtered = currentData.filter(item => {
        return config.columns.some(col => {
            const value = item[col.key];
            if (value === null || value === undefined) return false;
            return String(value).toLowerCase().includes(searchTerm);
        });
    });
    
    renderTable(filtered);
}

// ============ UTILIDADES ============
async function fetchWithAuth(url, options = {}) {
    const defaultOptions = {
        credentials: 'include',
        headers: {
            ...options.headers
        }
    };
    
    return fetch(url, { ...defaultOptions, ...options });
}

function showLoading() {
    document.getElementById('table-body').innerHTML = '';
    document.getElementById('table-head').innerHTML = '';
    document.getElementById('empty-state').style.display = 'none';
    document.getElementById('loading-state').style.display = 'block';
}

function showToast(type, title, message) {
    const container = document.getElementById('toast-container');
    
    const icons = {
        success: 'bi-check-circle-fill',
        error: 'bi-x-circle-fill',
        warning: 'bi-exclamation-triangle-fill',
        info: 'bi-info-circle-fill'
    };
    
    const toast = document.createElement('div');
    toast.className = `toast ${type}`;
    toast.innerHTML = `
        <span class="toast-icon"><i class="bi ${icons[type]}"></i></span>
        <div class="toast-content">
            <div class="toast-title">${escapeHtml(title)}</div>
            <div class="toast-message">${escapeHtml(message)}</div>
        </div>
        <button class="toast-close" onclick="this.parentElement.remove()"><i class="bi bi-x-lg"></i></button>
    `;
    
    container.appendChild(toast);
    
    // Auto-remove after 5 seconds
    setTimeout(() => {
        toast.classList.add('fade-out');
        setTimeout(() => toast.remove(), 300);
    }, 5000);
}

function escapeHtml(text) {
    if (text === null || text === undefined) return '';
    const div = document.createElement('div');
    div.textContent = String(text);
    return div.innerHTML;
}

function formatDateTime(dateString) {
    if (!dateString) return '—';
    const date = new Date(dateString);
    return date.toLocaleString('es-ES', {
        day: '2-digit',
        month: '2-digit',
        year: 'numeric',
        hour: '2-digit',
        minute: '2-digit'
    });
}

function getSingular(section) {
    const singulars = {
        usuarios: 'Usuario',
        aulas: 'Aula',
        clases: 'Clase',
        asistencias: 'Asistencia'
    };
    return singulars[section] || section;
}

// Funcion global para cerrar sesion (si no existe)
if (typeof cerrarSesion !== 'function') {
    window.cerrarSesion = function() {
        document.cookie = 'access_token=; Path=/; Expires=Thu, 01 Jan 1970 00:00:01 GMT;';
        window.location.href = '/';
    };
}

// ============ PANEL DE FILTRO POR ROL ============
function showFilteredList(rol, label) {
    const filtered = currentData.filter(u => u.rol === rol);
    const panel = document.getElementById('filter-panel');
    const title = document.getElementById('filter-panel-title');
    const list = document.getElementById('filter-panel-list');
    
    title.textContent = label;
    activeFilter = rol;
    
    if (filtered.length === 0) {
        list.innerHTML = `
            <div class="filter-empty">
                <i class="bi bi-inbox"></i>
                <p>No hay ${label.toLowerCase()} registrados</p>
            </div>
        `;
    } else {
        list.innerHTML = filtered.map(user => `
            <div class="filter-item">
                <div class="filter-item-main" onclick="openEditModal(${user.id}); closeFilterPanel();">
                    <div class="filter-item-avatar">
                        <i class="bi bi-person-circle"></i>
                    </div>
                    <div class="filter-item-info">
                        <div class="filter-item-name">${escapeHtml(user.nombre)}</div>
                        <div class="filter-item-email">${escapeHtml(user.correo)}</div>
                    </div>
                    <div class="filter-item-badge ${user.rol}">${user.rol}</div>
                </div>
                <button class="filter-item-delete" onclick="event.stopPropagation(); confirmDeleteFromPanel(${user.id}, '${escapeHtml(user.nombre).replace(/'/g, "\\'")}');" title="Eliminar usuario">
                    <i class="bi bi-trash3-fill"></i>
                </button>
            </div>
        `).join('');
    }
    
    panel.style.display = 'flex';
    setTimeout(() => panel.classList.add('show'), 10);
}

function confirmDeleteFromPanel(id, nombre) {
    deleteId = id;
    document.getElementById('confirm-message').textContent = 
        `Estas seguro de eliminar a "${nombre}"? Esta accion no se puede deshacer.`;
    showModal('modal-confirm');
}

function closeFilterPanel() {
    const panel = document.getElementById('filter-panel');
    panel.classList.remove('show');
    setTimeout(() => {
        panel.style.display = 'none';
        activeFilter = null;
    }, 300);
}
