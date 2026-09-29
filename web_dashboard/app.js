// Endpoint de AWS API Gateway Cloud Exclusivo (eu-north-1)
const AWS_API_BASE_URL = "https://d3h13f6kjb.execute-api.eu-north-1.amazonaws.com/default/GolpesAriete_SyncBackend?api_key=GolpesAriete2026SecureKey!";

// Dispositivo actualmente seleccionado
let currentDeviceId = "esp32_01";
let registeredDevices = [];

// Estado de la aplicación local
let currentData = {
    device_id: "esp32_01",
    device_name: "Planta 1",
    state: "STOPPED",
    is_running: false,
    has_level: true,
    use_sensor: false,
    time_on: 5,
    time_off: 5,
    cycle_count: 0,
    remaining_sec: 0,
    request_count: 0,
    target_time_on: 5,
    target_time_off: 5
};

const POLLING_INTERVAL_MS = 1500;

document.addEventListener("DOMContentLoaded", () => {
    fetchDeviceList();
    fetchSystemStatus();
    setInterval(fetchSystemStatus, POLLING_INTERVAL_MS);
    setInterval(fetchDeviceList, 10000); // Refrescar lista de dispositivos automáticamente cada 10s
});

// Obtener dinámicamente todos los ESP32 registrados en AWS DynamoDB
async function fetchDeviceList() {
    try {
        const response = await fetch(`${AWS_API_BASE_URL}&device_id=all`, {
            method: "GET",
            headers: {
                "Accept": "application/json"
            }
        });

        if (response.ok) {
            const devices = await response.json();
            if (Array.isArray(devices) && devices.length > 0) {
                registeredDevices = devices;
                renderDeviceSelector();
            }
        }
    } catch (e) {
        console.warn("Error obteniendo lista de dispositivos:", e);
    }
}

// Renderizar dinámicamente el selector de plantas/dispositivos
function renderDeviceSelector() {
    const select = document.getElementById("deviceSelect");
    if (!select) return;

    const previousValue = select.value || currentDeviceId;
    select.innerHTML = "";

    registeredDevices.forEach(dev => {
        const devId = dev.device_id;
        const devName = dev.device_name || `Planta (${devId})`;
        
        const option = document.createElement("option");
        option.value = devId;
        option.textContent = `${devName} [${devId}]`;
        select.appendChild(option);
    });

    if (registeredDevices.some(d => d.device_id === previousValue)) {
        select.value = previousValue;
    } else if (registeredDevices.length > 0) {
        currentDeviceId = registeredDevices[0].device_id;
        select.value = currentDeviceId;
    }
}

// Cambiar de Planta / Dispositivo
function onDeviceChange() {
    const select = document.getElementById("deviceSelect");
    if (select) {
        currentDeviceId = select.value;
        fetchSystemStatus();
    }
}

// Editar Nombre Personalizado del Dispositivo / Planta
async function editDeviceName() {
    const currentName = currentData.device_name || `Planta (${currentDeviceId})`;
    const newName = prompt(`Ingresa el nuevo nombre para la ubicación (${currentDeviceId}):`, currentName);

    if (newName && newName.trim() !== "" && newName !== currentName) {
        try {
            const payload = {
                client_type: "web",
                device_id: currentDeviceId,
                device_name: newName.trim()
            };

            await sendPostToAWS(payload);
            fetchDeviceList();
            fetchSystemStatus();
        } catch (err) {
            alert("Error al guardar el nuevo nombre: " + err.message);
        }
    }
}

// Consultar el estado del dispositivo seleccionado desde AWS
async function fetchSystemStatus() {
    try {
        const url = `${AWS_API_BASE_URL}&device_id=${encodeURIComponent(currentDeviceId)}`;
        const response = await fetch(url, {
            method: "GET",
            headers: {
                "Accept": "application/json"
            }
        });

        if (!response.ok) {
            throw new Error(`HTTP error! status: ${response.status}`);
        }

        const data = await response.json();
        currentData = data;

        updateUI(data);
        updateConnectionBadge(true);

    } catch (error) {
        console.warn("Error al consultar AWS API Gateway:", error);
        updateConnectionBadge(false);
    }
}

// Actualizar elementos visuales en el DOM
function updateUI(data) {
    // 1. Badge y Switch de Sensor de Nivel / Flujo
    const levelBadge = document.getElementById("levelBadge");
    const levelText = document.getElementById("levelText");
    const chkUseSensor = document.getElementById("chkUseSensor");

    const useSensor = data.use_sensor !== undefined ? data.use_sensor : false;
    const hasLevel = data.has_level !== undefined ? data.has_level : true;

    if (chkUseSensor && document.activeElement !== chkUseSensor) {
        chkUseSensor.checked = useSensor;
    }

    if (!useSensor) {
        levelBadge.className = "flex items-center space-x-2 px-3 py-1.5 rounded-full text-xs font-semibold bg-slate-700 text-slate-400 border border-slate-600";
        levelText.textContent = "Sensor: Desactivado (Opcional)";
    } else if (hasLevel && data.state !== "NO_LEVEL") {
        levelBadge.className = "flex items-center space-x-2 px-3 py-1.5 rounded-full text-xs font-semibold bg-emerald-500/20 text-emerald-400 border border-emerald-500/30";
        levelText.textContent = "Nivel: OK";
    } else {
        levelBadge.className = "flex items-center space-x-2 px-3 py-1.5 rounded-full text-xs font-semibold bg-rose-500/20 text-rose-400 border border-rose-500/30 animate-pulse";
        levelText.textContent = "ALERTA: Sin Nivel";
    }

    // 2. Contador de Ciclos y Peticiones API
    const cycleCountDisplay = document.getElementById("cycleCountDisplay");
    if (cycleCountDisplay) {
        cycleCountDisplay.textContent = data.cycle_count !== undefined ? data.cycle_count : 0;
    }

    const requestCountText = document.getElementById("requestCountText");
    if (requestCountText) {
        requestCountText.textContent = data.request_count !== undefined ? data.request_count : 0;
    }

    // 3. Tiempo Restante
    const remainingTimeText = document.getElementById("remainingTimeText");
    if (remainingTimeText) {
        remainingTimeText.textContent = `${data.remaining_sec !== undefined ? data.remaining_sec : 0} s`;
    }

    // 4. Inputs de Tiempo (si el usuario no está escribiendo activamente)
    const inputTimeOn = document.getElementById("inputTimeOn");
    const inputTimeOff = document.getElementById("inputTimeOff");

    if (inputTimeOn && document.activeElement !== inputTimeOn) {
        inputTimeOn.value = data.target_time_on || data.time_on || 5;
    }
    if (inputTimeOff && document.activeElement !== inputTimeOff) {
        inputTimeOff.value = data.target_time_off || data.time_off || 5;
    }

    // 5. Indicador de Estado del Relé
    const stateLabel = document.getElementById("stateLabel");
    const stateIconBg = document.getElementById("stateIconBg");
    const stateIcon = document.getElementById("stateIcon");

    const state = data.state || (data.is_running ? "ON" : "STOPPED");

    if (useSensor && (!hasLevel || state === "NO_LEVEL")) {
        stateLabel.textContent = "SIN NIVEL (PARADO OFF)";
        stateLabel.className = "mt-4 text-xl font-bold uppercase tracking-wider text-rose-400";
        stateIconBg.className = "w-24 h-24 rounded-full bg-rose-500/20 border-2 border-rose-500 flex items-center justify-center shadow-lg shadow-rose-500/30 transition-all duration-300 animate-bounce";
        stateIcon.className = "fa-solid fa-triangle-exclamation text-4xl text-rose-400";
    } else if (state === "ON") {
        stateLabel.textContent = "ENCENDIDO (ON)";
        stateLabel.className = "mt-4 text-xl font-bold uppercase tracking-wider text-emerald-400";
        stateIconBg.className = "w-24 h-24 rounded-full bg-emerald-500/20 border-2 border-emerald-500 flex items-center justify-center shadow-lg shadow-emerald-500/30 transition-all duration-300 animate-pulse";
        stateIcon.className = "fa-solid fa-lightbulb text-4xl text-emerald-400";
    } else if (state === "OFF") {
        stateLabel.textContent = "APAGADO (OFF)";
        stateLabel.className = "mt-4 text-xl font-bold uppercase tracking-wider text-amber-400";
        stateIconBg.className = "w-24 h-24 rounded-full bg-amber-500/20 border-2 border-amber-500 flex items-center justify-center shadow-lg shadow-amber-500/30 transition-all duration-300";
        stateIcon.className = "fa-solid fa-moon text-4xl text-amber-400";
    } else {
        stateLabel.textContent = "DETENIDO";
        stateLabel.className = "mt-4 text-xl font-bold uppercase tracking-wider text-slate-400";
        stateIconBg.className = "w-24 h-24 rounded-full bg-slate-700/50 border-2 border-slate-600 flex items-center justify-center shadow-inner transition-all duration-300";
        stateIcon.className = "fa-solid fa-stop text-4xl text-slate-400";
    }

    // 6. Estado del botón Iniciar / Detener
    const btnStart = document.getElementById("btnStart");
    const btnStop = document.getElementById("btnStop");

    if (data.is_running) {
        btnStart.classList.add("opacity-50", "cursor-not-allowed");
        btnStop.classList.remove("opacity-50", "cursor-not-allowed");
    } else {
        btnStart.classList.remove("opacity-50", "cursor-not-allowed");
        btnStop.classList.add("opacity-50", "cursor-not-allowed");
    }
}

// Cambiar estado Iniciar / Detener
async function setSystemState(shouldRun) {
    try {
        const payload = {
            client_type: "web",
            device_id: currentDeviceId,
            is_running: shouldRun
        };

        await sendPostToAWS(payload);
        fetchSystemStatus();
    } catch (err) {
        alert("Error al enviar comando a AWS: " + err.message);
    }
}

// Activar/Desactivar Sensor de Nivel Opcional
async function toggleSensorSetting() {
    try {
        const chkUseSensor = document.getElementById("chkUseSensor");
        const payload = {
            client_type: "web",
            device_id: currentDeviceId,
            use_sensor: chkUseSensor.checked
        };

        await sendPostToAWS(payload);
        fetchSystemStatus();
    } catch (err) {
        alert("Error al actualizar opción de sensor: " + err.message);
    }
}

// Guardar Tiempos ON / OFF
async function saveTimers(event) {
    event.preventDefault();

    const timeOnVal = parseInt(document.getElementById("inputTimeOn").value, 10);
    const timeOffVal = parseInt(document.getElementById("inputTimeOff").value, 10);

    if (isNaN(timeOnVal) || timeOnVal <= 0 || isNaN(timeOffVal) || timeOffVal <= 0) {
        alert("Por favor ingresa valores de tiempo válidos en segundos (mayores a 0).");
        return;
    }

    try {
        const payload = {
            client_type: "web",
            device_id: currentDeviceId,
            time_on: timeOnVal,
            time_off: timeOffVal
        };

        const btn = document.getElementById("btnSaveConfig");
        const originalText = btn.innerHTML;
        btn.innerHTML = `<i class="fa-solid fa-spinner animate-spin"></i> <span>Guardando...</span>`;

        await sendPostToAWS(payload);

        setTimeout(() => {
            btn.innerHTML = originalText;
            fetchSystemStatus();
        }, 500);

    } catch (err) {
        alert("Error al guardar la configuración en AWS: " + err.message);
    }
}

// Resetear contador de ciclos
async function resetCycles() {
    if (!confirm(`¿Estás seguro de que deseas resetear el contador de ciclos del dispositivo (${currentDeviceId}) a 0?`)) {
        return;
    }

    try {
        const payload = {
            client_type: "web",
            device_id: currentDeviceId,
            cmd_reset_cycles: true
        };

        await sendPostToAWS(payload);
        fetchSystemStatus();
    } catch (err) {
        alert("Error al resetear ciclos: " + err.message);
    }
}

// Función auxiliar para realizar llamadas POST a AWS
async function sendPostToAWS(payload) {
    const response = await fetch(AWS_API_BASE_URL, {
        method: "POST",
        headers: {
            "Content-Type": "application/json"
        },
        body: JSON.stringify(payload)
    });

    if (!response.ok) {
        throw new Error(`Error en el servidor AWS: ${response.status}`);
    }

    return await response.json();
}

// Actualizar badge de conexión en el Header
function updateConnectionBadge(isConnected) {
    const badge = document.getElementById("connectionBadge");
    const text = document.getElementById("connectionText");

    if (isConnected) {
        badge.className = "flex items-center space-x-2 px-3 py-1.5 rounded-full text-xs font-semibold bg-emerald-500/20 text-emerald-400 border border-emerald-500/30";
        text.textContent = "Conectado AWS";
    } else {
        badge.className = "flex items-center space-x-2 px-3 py-1.5 rounded-full text-xs font-semibold bg-rose-500/20 text-rose-400 border border-rose-500/30";
        text.textContent = "Sin Conexión AWS";
    }
}
