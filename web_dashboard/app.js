// Endpoint de AWS API Gateway Cloud Exclusivo (eu-north-1)
const AWS_API_BASE_URL = "https://d3h13f6kjb.execute-api.eu-north-1.amazonaws.com/default/GolpesAriete_SyncBackend?api_key=GolpesAriete2026SecureKey!";

let currentDeviceId = "esp32_01";
let registeredDevices = [];
let currentData = {};

const POLLING_INTERVAL_MS = 4000;

document.addEventListener("DOMContentLoaded", () => {
    fetchDeviceList();
    fetchSystemStatus();
    setInterval(fetchSystemStatus, POLLING_INTERVAL_MS);
    setInterval(fetchDeviceList, 20000);
});

async function fetchDeviceList() {
    try {
        const response = await fetch(`${AWS_API_BASE_URL}&device_id=all`, {
            method: "GET",
            headers: { "Accept": "application/json" }
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

function renderDeviceSelector() {
    const select = document.getElementById("deviceSelect");
    if (!select) return;

    const previousValue = select.value || currentDeviceId;
    select.innerHTML = "";

    registeredDevices.forEach(dev => {
        const devId = dev.device_id;
        const devName = dev.device_name || `Planta (${devId})`;
        const statusIcon = dev.is_online ? "🟢 ONLINE" : "🔴 OFFLINE";

        const option = document.createElement("option");
        option.value = devId;
        option.textContent = `${statusIcon} - ${devName} (${devId})`;
        select.appendChild(option);
    });

    if (!select.dataset.userSelected) {
        const onlineDev = registeredDevices.find(d => d.is_online);
        if (onlineDev) {
            currentDeviceId = onlineDev.device_id;
            select.value = currentDeviceId;
        } else if (registeredDevices.length > 0) {
            currentDeviceId = registeredDevices[0].device_id;
            select.value = currentDeviceId;
        }
    } else if (registeredDevices.some(d => d.device_id === previousValue)) {
        select.value = previousValue;
    }
}

function onDeviceChange() {
    const select = document.getElementById("deviceSelect");
    if (select) {
        select.dataset.userSelected = "true";
        currentDeviceId = select.value;
        fetchSystemStatus();
    }
}

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
            await fetch(AWS_API_BASE_URL, {
                method: "POST",
                headers: { "Content-Type": "application/json" },
                body: JSON.stringify(payload)
            });
            fetchDeviceList();
            fetchSystemStatus();
        } catch (e) {
            console.error("Error cambiando nombre:", e);
        }
    }
}

async function fetchSystemStatus() {
    try {
        const response = await fetch(`${AWS_API_BASE_URL}&device_id=${currentDeviceId}`, {
            method: "GET",
            headers: { "Accept": "application/json" }
        });

        if (!response.ok) throw new Error("HTTP error " + response.status);

        const data = await response.json();
        currentData = data;
        updateUI(data);

    } catch (error) {
        console.error("Error obteniendo estado:", error);
        updateConnectionBadge(false);
    }
}

function formatTime(seconds) {
    if (!seconds || seconds <= 0) return "-- s";
    if (seconds < 60) return `${seconds}s`;
    if (seconds < 3600) {
        const m = Math.floor(seconds / 60);
        const s = seconds % 60;
        return `${m}m ${s}s`;
    }
    const h = Math.floor(seconds / 3600);
    const m = Math.floor((seconds % 3600) / 60);
    const s = seconds % 60;
    return `${h}h ${m}m ${s}s`;
}

function updateUI(data) {
    updateConnectionBadge(data.is_online);

    // Versión firmware
    const fwElem = document.getElementById("firmwareVerText");
    if (fwElem) fwElem.textContent = data.firmware_ver || "v5.0";

    // Nombre Ensayo / Prueba
    const inputDevName = document.getElementById("inputDeviceName");
    if (inputDevName && !inputDevName.dataset.userEditing) {
        inputDevName.value = data.device_name || `Planta (${currentDeviceId})`;
    }

    // Peticiones AWS
    const reqText = document.getElementById("requestCountText");
    if (reqText && data.request_count !== undefined) reqText.textContent = data.request_count;

    // Frecuencia sincronización (sync_interval_ms)
    const selectSync = document.getElementById("selectSyncInterval");
    if (selectSync && data.sync_interval_ms && !selectSync.dataset.userEditing) {
        selectSync.value = data.sync_interval_ms.toString();
    }

    // Sensor nivel
    const chkUseSensor = document.getElementById("chkUseSensor");
    if (chkUseSensor && data.use_sensor !== undefined) {
        chkUseSensor.checked = data.use_sensor;
    }

    const levelText = document.getElementById("levelText");
    const levelBadge = document.getElementById("levelBadge");
    if (levelText && levelBadge) {
        if (data.use_sensor === false) {
            levelText.textContent = "Sensor: Desactivado";
            levelBadge.className = "flex items-center space-x-2 px-3 py-1.5 rounded-full text-xs font-semibold bg-slate-800 text-slate-400 border border-slate-700";
        } else if (data.has_level !== false) {
            levelText.textContent = "Nivel: OK";
            levelBadge.className = "flex items-center space-x-2 px-3 py-1.5 rounded-full text-xs font-semibold bg-emerald-500/20 text-emerald-400 border border-emerald-500/30";
        } else {
            levelText.textContent = "ALERTA: Sin Nivel!";
            levelBadge.className = "flex items-center space-x-2 px-3 py-1.5 rounded-full text-xs font-semibold bg-rose-500/20 text-rose-400 border border-rose-500/30 animate-pulse";
        }
    }

    // 1. CANAL ARIETE 1
    const a1 = data.ariete_1 || {
        state: data.state || "STOPPED",
        is_running: data.is_running || false,
        time_on: data.time_on || 5,
        time_off: data.time_off || 5,
        cycle_count: data.cycle_count || 0,
        remaining_sec: data.remaining_sec || 0
    };

    const a1_badge = document.getElementById("a1_statusBadge");
    if (a1_badge) {
        if (a1.state === "ON") {
            a1_badge.textContent = "🟢 ENCENDIDO (ON)";
            a1_badge.className = "px-3 py-1 rounded-full text-xs font-bold bg-emerald-500/20 text-emerald-400 border border-emerald-500/30";
        } else if (a1.state === "OFF") {
            a1_badge.textContent = "🟡 PAUSA (OFF)";
            a1_badge.className = "px-3 py-1 rounded-full text-xs font-bold bg-amber-500/20 text-amber-400 border border-amber-500/30";
        } else {
            a1_badge.textContent = "🔴 DETENIDO";
            a1_badge.className = "px-3 py-1 rounded-full text-xs font-bold bg-slate-700 text-slate-300";
        }
    }

    const a1_rem = document.getElementById("a1_remTime");
    if (a1_rem) a1_rem.textContent = formatTime(a1.remaining_sec);

    const a1_cyc = document.getElementById("a1_cycleCount");
    if (a1_cyc) a1_cyc.textContent = a1.cycle_count || 0;

    // 2. CANAL POLARIDAD 1
    const p1 = data.polarity_1 || { state: "STOPPED", is_running: false, cycle_count: 0, remaining_sec: 0, total_sec_a: 0, total_sec_b: 0, total_sec_sum: 0 };
    const p1_badge = document.getElementById("p1_statusBadge");
    if (p1_badge) {
        if (p1.state === "POLARITY_A") {
            p1_badge.textContent = "🟢 POLARIDAD A (DIRECTA)";
            p1_badge.className = "px-3 py-1 rounded-full text-xs font-bold bg-emerald-500/20 text-emerald-400 border border-emerald-500/30";
        } else if (p1.state === "POLARITY_B") {
            p1_badge.textContent = "🔵 POLARIDAD B (INVERSA)";
            p1_badge.className = "px-3 py-1 rounded-full text-xs font-bold bg-blue-500/20 text-blue-400 border border-blue-500/30";
        } else if (p1.state === "DEADBAND") {
            p1_badge.textContent = "⏸️ PAUSA SEGURIDAD (BANDA MUERTA)";
            p1_badge.className = "px-3 py-1 rounded-full text-xs font-bold bg-amber-500/20 text-amber-400 border border-amber-500/30 animate-pulse";
        } else {
            p1_badge.textContent = "🔴 DETENIDO";
            p1_badge.className = "px-3 py-1 rounded-full text-xs font-bold bg-slate-700 text-slate-300";
        }
    }

    const p1_rem = document.getElementById("p1_remTime");
    if (p1_rem) p1_rem.textContent = formatTime(p1.remaining_sec);

    const p1_cyc = document.getElementById("p1_cycleCount");
    if (p1_cyc) p1_cyc.textContent = p1.cycle_count || 0;

    const p1_totA = document.getElementById("p1_totalA");
    if (p1_totA) p1_totA.textContent = formatHoursMinutes(p1.total_sec_a);
    const p1_totB = document.getElementById("p1_totalB");
    if (p1_totB) p1_totB.textContent = formatHoursMinutes(p1.total_sec_b);
    const p1_totSum = document.getElementById("p1_totalSum");
    if (p1_totSum) p1_totSum.textContent = formatHoursMinutes(p1.total_sec_sum || ((p1.total_sec_a || 0) + (p1.total_sec_b || 0)));

    // 3. CANAL POLARIDAD 2
    const p2 = data.polarity_2 || { state: "STOPPED", is_running: false, cycle_count: 0, remaining_sec: 0, total_sec_a: 0, total_sec_b: 0, total_sec_sum: 0 };
    const p2_badge = document.getElementById("p2_statusBadge");
    if (p2_badge) {
        if (p2.state === "POLARITY_A") {
            p2_badge.textContent = "🟢 POLARIDAD A (DIRECTA)";
            p2_badge.className = "px-3 py-1 rounded-full text-xs font-bold bg-emerald-500/20 text-emerald-400 border border-emerald-500/30";
        } else if (p2.state === "POLARITY_B") {
            p2_badge.textContent = "🔵 POLARIDAD B (INVERSA)";
            p2_badge.className = "px-3 py-1 rounded-full text-xs font-bold bg-cyan-500/20 text-cyan-400 border border-cyan-500/30";
        } else if (p2.state === "DEADBAND") {
            p2_badge.textContent = "⏸️ PAUSA SEGURIDAD (BANDA MUERTA)";
            p2_badge.className = "px-3 py-1 rounded-full text-xs font-bold bg-amber-500/20 text-amber-400 border border-amber-500/30 animate-pulse";
        } else {
            p2_badge.textContent = "🔴 DETENIDO";
            p2_badge.className = "px-3 py-1 rounded-full text-xs font-bold bg-slate-700 text-slate-300";
        }
    }

    const p2_rem = document.getElementById("p2_remTime");
    if (p2_rem) p2_rem.textContent = formatTime(p2.remaining_sec);

    const p2_cyc = document.getElementById("p2_cycleCount");
    if (p2_cyc) p2_cyc.textContent = p2.cycle_count || 0;

    const p2_totA = document.getElementById("p2_totalA");
    if (p2_totA) p2_totA.textContent = formatHoursMinutes(p2.total_sec_a);
    const p2_totB = document.getElementById("p2_totalB");
    if (p2_totB) p2_totB.textContent = formatHoursMinutes(p2.total_sec_b);
    const p2_totSum = document.getElementById("p2_totalSum");
    if (p2_totSum) p2_totSum.textContent = formatHoursMinutes(p2.total_sec_sum || ((p2.total_sec_a || 0) + (p2.total_sec_b || 0)));

    // OTA Progress UI Bar
    const otaProgressContainer = document.getElementById("otaProgressContainer");
    const otaProgressBar = document.getElementById("otaProgressBar");
    const otaPercentText = document.getElementById("otaPercentText");
    const otaStatusText = document.getElementById("otaStatusText");

    if (otaProgressContainer && (data.state === "UPDATING_OTA" || (data.ota_progress > 0 && data.ota_progress < 100))) {
        otaProgressContainer.classList.remove("hidden");
        const pct = Math.min(100, Math.max(0, parseInt(data.ota_progress || 0, 10)));
        if (otaProgressBar) otaProgressBar.style.width = `${pct}%`;
        if (otaPercentText) otaPercentText.textContent = `${pct}%`;
        if (otaStatusText) {
            const msg = data.ota_status || "Descargando paquetes de firmware...";
            otaStatusText.innerHTML = `<i class="fa-solid fa-spinner fa-spin text-purple-400"></i> <span>${msg}</span>`;
        }
    } else if (otaProgressContainer && data.ota_progress >= 100) {
        otaProgressContainer.classList.remove("hidden");
        if (otaProgressBar) otaProgressBar.style.width = `100%`;
        if (otaPercentText) otaPercentText.textContent = `100%`;
        if (otaStatusText) {
            otaStatusText.innerHTML = `<i class="fa-solid fa-circle-check text-emerald-400"></i> <span>¡Firmware instalado! Reiniciando ESP32...</span>`;
        }
        setTimeout(() => {
            if (otaProgressContainer) otaProgressContainer.classList.add("hidden");
        }, 5000);
    } else if (otaProgressContainer) {
        otaProgressContainer.classList.add("hidden");
    }
}

function formatHoursMinutes(seconds) {
    if (!seconds || seconds <= 0) return "0h 0m";
    if (seconds < 60) return `${seconds}s`;
    if (seconds < 3600) {
        const m = Math.floor(seconds / 60);
        const s = seconds % 60;
        return `${m}m ${s}s`;
    }
    const h = Math.floor(seconds / 3600);
    const m = Math.floor((seconds % 3600) / 60);
    return `${h}h ${m}`;
}

function updateConnectionBadge(isOnline) {
    const badge = document.getElementById("connectionBadge");
    const text = document.getElementById("connectionText");
    if (!badge || !text) return;

    if (isOnline) {
        badge.className = "flex items-center space-x-2 px-3 py-1.5 rounded-full text-xs font-semibold bg-emerald-500/20 text-emerald-400 border border-emerald-500/30";
        text.textContent = "ESP32 ONLINE";
    } else {
        badge.className = "flex items-center space-x-2 px-3 py-1.5 rounded-full text-xs font-semibold bg-rose-500/20 text-rose-400 border border-rose-500/30";
        text.textContent = "ESP32 OFFLINE";
    }
}

// FUNCIONES DE CONTROL DE CANALES

async function sendCommandPayload(cmdPayload) {
    try {
        const payload = {
            client_type: "web",
            device_id: currentDeviceId,
            ...cmdPayload
        };
        await fetch(AWS_API_BASE_URL, {
            method: "POST",
            headers: { "Content-Type": "application/json" },
            body: JSON.stringify(payload)
        });
        setTimeout(fetchSystemStatus, 500);
    } catch (e) {
        console.error("Error enviando comando:", e);
    }
}

function setAriete1Running(isRunning) {
    sendCommandPayload({
        cmd_ariete_1: { is_running: isRunning },
        target_running: isRunning
    });
}

function saveAriete1Config() {
    const ton = parseInt(document.getElementById("a1_timeOn").value) || 5;
    const toff = parseInt(document.getElementById("a1_timeOff").value) || 5;

    sendCommandPayload({
        cmd_ariete_1: { time_on: ton, time_off: toff }
    });
}

function setPolarity1Running(isRunning) {
    sendCommandPayload({
        cmd_polarity_1: { is_running: isRunning }
    });
}

function resetPolarity1Totals() {
    if (confirm("¿Estás seguro de que deseas resetear las horas acumuladas de la Polaridad 1?")) {
        sendCommandPayload({
            cmd_polarity_1: { reset_totals: true }
        });
    }
}

function convertToSeconds(value, unit) {
    const val = parseInt(value) || 1;
    if (unit === "hours") return val * 3600;
    if (unit === "min") return val * 60;
    return val;
}

function savePolarity1Config() {
    const timeAVal = document.getElementById("p1_timeA").value;
    const unitA = document.getElementById("p1_unitA").value;
    const timeBVal = document.getElementById("p1_timeB").value;
    const unitB = document.getElementById("p1_unitB").value;
    const timeDead = parseInt(document.getElementById("p1_timeDead").value) || 3;

    const timeASec = convertToSeconds(timeAVal, unitA);
    const timeBSec = convertToSeconds(timeBVal, unitB);

    sendCommandPayload({
        cmd_polarity_1: { time_a: timeASec, time_b: timeBSec, time_dead: timeDead }
    });
}

function setPolarity2Running(isRunning) {
    sendCommandPayload({
        cmd_polarity_2: { is_running: isRunning }
    });
}

function resetPolarity2Totals() {
    if (confirm("¿Estás seguro de que deseas resetear las horas acumuladas de la Polaridad 2?")) {
        sendCommandPayload({
            cmd_polarity_2: { reset_totals: true }
        });
    }
}

function savePolarity2Config() {
    const timeAVal = document.getElementById("p2_timeA").value;
    const unitA = document.getElementById("p2_unitA").value;
    const timeBVal = document.getElementById("p2_timeB").value;
    const unitB = document.getElementById("p2_unitB").value;
    const timeDead = parseInt(document.getElementById("p2_timeDead").value) || 3;

    const timeASec = convertToSeconds(timeAVal, unitA);
    const timeBSec = convertToSeconds(timeBVal, unitB);

    sendCommandPayload({
        cmd_polarity_2: { time_a: timeASec, time_b: timeBSec, time_dead: timeDead }
    });
}

// Despachar Orden de Actualización de Firmware por OTA al ESP32
async function triggerOTA() {
    const urlInput = document.getElementById("inputOtaUrl");
    const otaUrl = urlInput ? urlInput.value.trim() : "";

    if (!otaUrl || !otaUrl.startsWith("http")) {
        alert("Por favor ingresa una URL válida (HTTP/HTTPS) que apunte al archivo .bin de firmware.");
        return;
    }

    if (!confirm(`¿Deseas enviar la orden de actualización OTA al dispositivo (${currentDeviceId}) desde la URL:\n${otaUrl}?`)) {
        return;
    }

    try {
        const btn = document.getElementById("btnTriggerOta");
        const originalText = btn ? btn.innerHTML : "";
        if (btn) {
            btn.disabled = true;
            btn.innerHTML = `<i class="fa-solid fa-spinner fa-spin"></i> <span>Enviando orden OTA...</span>`;
        }

        await sendCommandPayload({
            ota_url: otaUrl
        });

        if (btn) {
            btn.innerHTML = `<i class="fa-solid fa-check text-emerald-400"></i> <span>¡Orden OTA enviada!</span>`;
        }
        alert(`¡Orden de actualización OTA enviada exitosamente a AWS!\nEn el próximo latido (5s), el ESP32 descargará e instalará el nuevo firmware.`);

        setTimeout(() => {
            if (btn) {
                btn.disabled = false;
                btn.innerHTML = originalText;
            }
            fetchSystemStatus();
        }, 3000);

    } catch (err) {
        alert("Error al despachar orden OTA: " + err.message);
        const btn = document.getElementById("btnTriggerOta");
        if (btn) btn.disabled = false;
    }
}

async function toggleSensorSetting() {
    const chkUseSensor = document.getElementById("chkUseSensor");
    if (!chkUseSensor) return;
    try {
        await sendCommandPayload({
            use_sensor: chkUseSensor.checked
        });
    } catch (e) {
        console.error("Error cambiando uso de sensor:", e);
    }
}

async function saveSyncInterval() {
    const selectSync = document.getElementById("selectSyncInterval");
    if (!selectSync) return;
    selectSync.dataset.userEditing = "true";
    const syncVal = parseInt(selectSync.value, 10) || 2000;
    try {
        await sendCommandPayload({
            sync_interval_ms: syncVal
        });
        setTimeout(() => { delete selectSync.dataset.userEditing; }, 3000);
    } catch (e) {
        console.error("Error al guardar cadencia de sincronización:", e);
        delete selectSync.dataset.userEditing;
    }
}

async function saveDeviceNameFromInput() {
    const inputDevName = document.getElementById("inputDeviceName");
    if (!inputDevName) return;
    const newName = inputDevName.value.trim();
    if (!newName) return;

    try {
        const btn = document.getElementById("btnSaveTestName");
        const originalText = btn ? btn.innerHTML : "";
        if (btn) btn.innerHTML = `<i class="fa-solid fa-check text-emerald-300"></i> <span>¡Guardado!</span>`;

        await sendCommandPayload({
            device_name: newName
        });

        fetchDeviceList();
        fetchSystemStatus();

        setTimeout(() => {
            if (btn) btn.innerHTML = originalText;
            delete inputDevName.dataset.userEditing;
        }, 1500);
    } catch (e) {
        console.error("Error guardando nombre del ensayo:", e);
        if (inputDevName) delete inputDevName.dataset.userEditing;
    }
}
