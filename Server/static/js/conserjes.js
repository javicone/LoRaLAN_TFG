// ================================================================
// CONSERJES — Panel de Control de Aulas
// Consulta HTTP periódica (sin WebSocket) + control de relés
// ================================================================

// ========================================
// GLOBAL STATE
// ========================================

let aulasData = {};
let ultimoEstado = '';
const REFRESCO_MS = 5000;

// ========================================
// ESTADO — CONSULTA PERIÓDICA
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
        updateDeviceCount(datos.aulas.filter(a => a.conectado).length);

        // Solo se repinta si algo cambió (hace_s cambia en cada consulta)
        const huella = JSON.stringify(datos.aulas.map(({ hace_s, ...resto }) => resto));
        if (huella === ultimoEstado) return;
        ultimoEstado = huella;

        aulasData = {};
        datos.aulas.forEach(a => { aulasData[a.aula || a.device_id] = a; });
        renderAulas();
    } catch (e) {
        console.error('❌ Error consultando estado:', e);
        updateConnectionStatus(false);
    }
}

// ========================================
// UI STATUS UPDATES
// ========================================

function updateConnectionStatus(connected) {
    const el = document.getElementById('ws-status');
    if (!el) return;

    if (connected) {
        el.innerHTML = '<i class="bi bi-circle-fill"></i> Conectado';
        el.className = 'ws-badge ws-badge--on';
    } else {
        el.innerHTML = '<i class="bi bi-circle-fill"></i> Sin servidor';
        el.className = 'ws-badge ws-badge--off';
    }
}

function updateDeviceCount(count) {
    const el = document.getElementById('device-count-text');
    if (el) {
        el.textContent = `${count} dispositivo${count !== 1 ? 's' : ''}`;
    }
}

// ========================================
// ACCIONES
// ========================================

// La orden queda en la cola del gateway y llega al nodo por radio. El
// interruptor vuelve a mostrar el estado real cuando el nodo lo confirma.
// ponytail: sin indicador de "pendiente"; añadirlo si la espera confunde.
async function cambiarRelay(aulaName, relayNum, nuevoEstado) {
    const aula = aulasData[aulaName];
    if (!aula) return false;

    try {
        const r = await fetch('/panel/rele', {
            method: 'POST',
            credentials: 'include',
            headers: { 'Content-Type': 'application/json' },
            body: JSON.stringify({ device_id: aula.device_id, relay_num: relayNum, relay_state: nuevoEstado })
        });
        if (!r.ok) {
            const detalle = r.status === 404 ? 'el nodo no está en la red LoRaLAN' : `HTTP ${r.status}`;
            alert(`No se pudo enviar la orden: ${detalle}`);
            ultimoEstado = '';
            cargarEstado();   // devuelve el interruptor a su estado real
            return false;
        }
        console.log(`📤 relay${relayNum}=${nuevoEstado} encolado para ${aula.device_id}`);
        return true;
    } catch (e) {
        alert('Sin conexión con el servidor');
        return false;
    }
}

// ========================================
// RENDER — Room Cards
// ========================================

function renderAulas() {
    const panel = document.getElementById('panel');
    if (!panel) return;

    const keys = Object.keys(aulasData);

    if (keys.length === 0) {
        panel.innerHTML = `
            <div class="panel-empty">
                <i class="bi bi-router"></i>
                <h3>Sin dispositivos conectados</h3>
                <p>Los dispositivos ESP32 aparecerán aquí cuando se conecten</p>
            </div>`;
        return;
    }

    panel.innerHTML = '';

    keys.forEach((aulaKey, index) => {
        const aula = aulasData[aulaKey];
        const name = aula.nombre || aula.aula || aulaKey;
        const escapedName = name.replace(/'/g, "\\'");

        // Sensor values with fallbacks
        const temp     = aula.temperatura != null ? `${aula.temperatura}°C` : '—';
        const humedad  = aula.humedad     != null ? `${aula.humedad}%`      : '—';
        const luz      = aula.luz         != null ? `${aula.luz} lux`       : '—';
        const presion  = aula.presion     != null ? `${aula.presion} hPa`   : '—';
        const personas = aula.personas    != null ? `${aula.personas}`      : '—';

        const claseEnCurso = aula.clase_en_curso || false;
        const relay1 = aula.relay1 || false;
        const relay2 = aula.relay2 || false;

        const delay = Math.min(index * 0.06, 0.35);

        const card = document.createElement('article');
        card.className = 'room-card';
        card.style.animationDelay = `${delay}s`;

        card.innerHTML = `
            <div class="room-card__header">
                <h3 class="room-card__name">${name}</h3>
                <span class="room-card__badge room-card__badge--${claseEnCurso ? 'active' : 'inactive'}">
                    ${claseEnCurso ? '<i class="bi bi-mortarboard"></i> Clase en curso' : 'Sin clase'}
                </span>
            </div>

            <div class="room-card__sensors">
                <div class="sensor">
                    <div class="sensor__icon"><i class="bi bi-thermometer-half"></i></div>
                    <div class="sensor__data">
                        <span class="sensor__value">${temp}</span>
                        <span class="sensor__label">Temp</span>
                    </div>
                </div>
                <div class="sensor">
                    <div class="sensor__icon"><i class="bi bi-droplet-half"></i></div>
                    <div class="sensor__data">
                        <span class="sensor__value">${humedad}</span>
                        <span class="sensor__label">Humedad</span>
                    </div>
                </div>
                <div class="sensor">
                    <div class="sensor__icon"><i class="bi bi-sun"></i></div>
                    <div class="sensor__data">
                        <span class="sensor__value">${luz}</span>
                        <span class="sensor__label">Luz</span>
                    </div>
                </div>
                <div class="sensor">
                    <div class="sensor__icon"><i class="bi bi-speedometer2"></i></div>
                    <div class="sensor__data">
                        <span class="sensor__value">${presion}</span>
                        <span class="sensor__label">Presión</span>
                    </div>
                </div>
                <div class="sensor">
                    <div class="sensor__icon"><i class="bi bi-people"></i></div>
                    <div class="sensor__data">
                        <span class="sensor__value">${personas}</span>
                        <span class="sensor__label">Personas</span>
                    </div>
                </div>
            </div>

            <div class="room-card__controls">
                <div class="relay-row">
                    <div class="relay-row__info">
                        <i class="bi bi-fire"></i>
                        <span>Calefacción</span>
                        <span class="relay-row__state relay-row__state--${relay1 ? 'on' : 'off'}">${relay1 ? 'ON' : 'OFF'}</span>
                    </div>
                    <label class="toggle">
                        <input type="checkbox" ${relay1 ? 'checked' : ''} onchange="cambiarRelay('${escapedName}', 1, this.checked)">
                        <span class="toggle__track"></span>
                    </label>
                </div>
                <div class="relay-row">
                    <div class="relay-row__info">
                        <i class="bi bi-lightbulb"></i>
                        <span>Iluminación</span>
                        <span class="relay-row__state relay-row__state--${relay2 ? 'on' : 'off'}">${relay2 ? 'ON' : 'OFF'}</span>
                    </div>
                    <label class="toggle">
                        <input type="checkbox" ${relay2 ? 'checked' : ''} onchange="cambiarRelay('${escapedName}', 2, this.checked)">
                        <span class="toggle__track"></span>
                    </label>
                </div>
            </div>
        `;

        panel.appendChild(card);
    });
}

// ========================================
// LOGOUT
// ========================================

function cerrarSesion() {
    sessionStorage.clear();
    localStorage.clear();

    fetch('/logout', { method: 'GET', credentials: 'include' })
        .then(() => { window.location.href = '/'; })
        .catch(() => { window.location.href = '/'; });
}

// ========================================
// INITIALIZATION
// ========================================

document.addEventListener('DOMContentLoaded', () => {
    console.log('🚀 Inicializando panel conserjes');
    cargarEstado();
    setInterval(cargarEstado, REFRESCO_MS);

    const btnActualizar = document.getElementById('btn-actualizar');
    if (btnActualizar) {
        btnActualizar.addEventListener('click', () => { ultimoEstado = ''; cargarEstado(); });
    }

    const btnLogout = document.getElementById('btn-logout');
    if (btnLogout) {
        btnLogout.addEventListener('click', (e) => {
            e.preventDefault();
            cerrarSesion();
        });
    }
});
