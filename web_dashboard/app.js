// Endpoint de AWS API Gateway Cloud (eu-north-1)
const AWS_API_URL = "https://cosqwrexxk.execute-api.eu-north-1.amazonaws.com/default/LabEngineSync?api_key=FluidraLab2026SecureKey!";

// Estado de la aplicación local
let currentData = {
    state: "STOPPED",
    is_running: false,
    has_level: true,
    time_on: 5,
    time_off: 5,
    cycle_count: 0,
    remaining_sec: 0,
    target_time_on: 5,
    target_time_off: 5
};

// Intervalo de Polling (1.5 segundos)
const POLLING_INTERVAL_MS = 1500;

document.addEventListener("DOMContentLoaded", () => {
    // Primera carga de datos
    fetchSystemStatus();

    // Iniciar actualización periódica
    setInterval(fetchSystemStatus, POLLING_INTERVAL_MS);
});

// Función para obtener el estado actual desde AWS API Gateway
async function fetchSystemStatus() {
    try {
        const response = await fetch(AWS_API_URL, {
            method: "GET",
            headers: {
                "Accept": "application/json",
                "x-api-key": "FluidraLab2026SecureKey!"
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
    // 1. Badge de Sensor de Nivel
    const levelBadge = document.getElementById("levelBadge");
    const levelText = document.getElementById("levelText");

    const hasLevel = data.has_level !== undefined ? data.has_level : true;

    if (hasLevel && data.state !== "NO_LEVEL") {
        levelBadge.className = "flex items-center space-x-2 px-3 py-1.5 rounded-full text-xs font-semibold bg-emerald-500/20 text-emerald-400 border border-emerald-500/30";
        levelText.textContent = "Nivel: OK";
    } else {
        levelBadge.className = "flex items-center space-x-2 px-3 py-1.5 rounded-full text-xs font-semibold bg-rose-500/20 text-rose-400 border border-rose-500/30 animate-pulse";
        levelText.textContent = "ALERTA: Sin Nivel";
    }

    // 2. Contador de Ciclos y Peticiones
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

    if (state === "NO_LEVEL" || !hasLevel) {
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

    if (data.is_running && hasLevel) {
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
            is_running: shouldRun
        };

        await sendPostToAWS(payload);
        fetchSystemStatus();
    } catch (err) {
        alert("Error al enviar comando a AWS: " + err.message);
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
    if (!confirm("¿Estás seguro de que deseas resetear el contador de ciclos a 0?")) {
        return;
    }

    try {
        const payload = {
            client_type: "web",
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
    const response = await fetch(AWS_API_URL, {
        method: "POST",
        headers: {
            "Content-Type": "application/json",
            "x-api-key": "FluidraLab2026SecureKey!"
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
        text.textContent = "Conectado ESP32";
    } else {
        badge.className = "flex items-center space-x-2 px-3 py-1.5 rounded-full text-xs font-semibold bg-rose-500/20 text-rose-400 border border-rose-500/30";
        text.textContent = "Sin Conexión ESP32";
    }
}
