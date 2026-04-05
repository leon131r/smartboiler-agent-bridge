// Конфигурация
const config = {
    updateInterval: 3000,  // 3 секунды
    chartHistoryPoints: 24 // точек на графике
};

// Состояние приложения
let appState = {
    temperatures: {
        return: null,
        flow: null,
        air: null,
        delta: null
    },
    control: {
        mode: 'off',
        targetTemp: 40,
        maxTemp: 75,
        power: 0
    },
    system: {
        relay: false,
        safety: true,
        water: true,
        uptime: '00:00:00',
        ip: '---'
    },
    chart: null,
    chartData: {
        labels: [],
        returnData: [],
        flowData: []
    }
};

// DOM элементы
const elements = {
    connectionStatus: document.getElementById('connectionStatus'),
    uptime: document.getElementById('uptime'),
    ipText: document.getElementById('ipText'),
    waterAlert: document.getElementById('waterAlert'),
    tempReturn: document.getElementById('tempReturn'),
    tempFlow: document.getElementById('tempFlow'),
    tempAir: document.getElementById('tempAir'),
    tempDelta: document.getElementById('tempDelta'),
    modeBadge: document.getElementById('modeBadge'),
    targetTemp: document.getElementById('targetTemp'),
    powerValue: document.getElementById('powerValue'),
    powerSlider: document.getElementById('powerSlider'),
    maxTemp: document.getElementById('maxTemp'),
    relayIcon: document.getElementById('relayIcon'),
    relayState: document.getElementById('relayState'),
    safetyIcon: document.getElementById('safetyIcon'),
    safetyState: document.getElementById('safetyState'),
    lastUpdate: document.getElementById('lastUpdate'),
    wifiInfo: document.getElementById('wifiInfo'),
    consumption: document.getElementById('consumption'),
    btnAuto: document.getElementById('btnAuto'),
    btnOff: document.getElementById('btnOff'),
    btnManual: document.getElementById('btnManual'),
    toggleChart: document.getElementById('toggleChart'),
    btnReset: document.getElementById('btnReset'),
    btnRefresh: document.getElementById('btnRefresh')
};

// Инициализация
document.addEventListener('DOMContentLoaded', function() {
    initChart();
    updateConnectionStatus();
    setupEventListeners();
    refreshData();
    startAutoUpdate();
});

// Инициализация графика
function initChart() {
    // Проверяем доступность Chart.js
    if (typeof Chart === 'undefined' || !window.chartJSAvailable) {
        console.warn('Chart.js недоступен — инициализация графика пропущена');
        const chartContainer = document.querySelector('.chart-container');
        if (chartContainer) {
            chartContainer.innerHTML = '<p style="text-align:center;padding:40px;color:#666;">📈 Графики недоступны (нет CDN). Подключите ESP32 к интернету для загрузки Chart.js.</p>';
        }
        return;
    }

    const ctx = document.getElementById('tempChart').getContext('2d');
    appState.chart = new Chart(ctx, {
        type: 'line',
        data: {
            labels: appState.chartData.labels,
            datasets: [
                {
                    label: 'Обратка',
                    data: appState.chartData.returnData,
                    borderColor: 'rgb(255, 99, 132)',
                    backgroundColor: 'rgba(255, 99, 132, 0.1)',
                    tension: 0.4,
                    fill: true,
                    borderWidth: 2
                },
                {
                    label: 'Подача',
                    data: appState.chartData.flowData,
                    borderColor: 'rgb(54, 162, 235)',
                    backgroundColor: 'rgba(54, 162, 235, 0.1)',
                    tension: 0.4,
                    fill: true,
                    borderWidth: 2
                }
            ]
        },
        options: {
            responsive: true,
            maintainAspectRatio: false,
            plugins: {
                legend: {
                    position: 'top',
                    labels: {
                        font: {
                            size: 14,
                            weight: 'bold'
                        }
                    }
                },
                tooltip: {
                    mode: 'index',
                    intersect: false,
                    titleFont: {
                        size: 14
                    },
                    bodyFont: {
                        size: 14
                    }
                }
            },
            scales: {
                x: {
                    display: true,
                    title: {
                        display: true,
                        text: 'Время',
                        font: {
                            size: 14,
                            weight: 'bold'
                        }
                    },
                    ticks: {
                        font: {
                            size: 12
                        }
                    }
                },
                y: {
                    display: true,
                    title: {
                        display: true,
                        text: 'Температура (°C)',
                        font: {
                            size: 14,
                            weight: 'bold'
                        }
                    },
                    ticks: {
                        font: {
                            size: 12
                        }
                    },
                    suggestedMin: 0,
                    suggestedMax: 100
                }
            }
        }
    });
}

// Настройка обработчиков событий
function setupEventListeners() {
    // Крупные кнопки режимов
    elements.btnAuto.addEventListener('click', () => {
        sendCommand('on');
        addButtonFeedback(elements.btnAuto);
    });
    
    elements.btnOff.addEventListener('click', () => {
        sendCommand('off');
        addButtonFeedback(elements.btnOff);
    });
    
    elements.btnManual.addEventListener('click', () => {
        sendCommand('manual');
        addButtonFeedback(elements.btnManual);
    });
    
    // Кнопки температуры цели
    document.querySelectorAll('.temp').forEach(button => {
        button.addEventListener('click', function() {
            let temp = this.textContent.replace('°C', '').trim();
            if (temp && !isNaN(temp)) {
                sendCommand(`temp${temp}`);
                addButtonFeedback(this);
            }
        });
    });

    // Кнопки мощности
    document.querySelectorAll('.power').forEach(button => {
        button.addEventListener('click', function() {
            let power = this.textContent.replace('%', '').trim();
            if (power && !isNaN(power)) {
                setPower(parseInt(power));
                addButtonFeedback(this);
            }
        });
    });

    // Кнопки максимальной температуры
    const controlButtons = document.querySelectorAll('.control-buttons .btn-sm');
    controlButtons.forEach(button => {
        if (!button.classList.contains('temp') && !button.classList.contains('power')) {
            button.addEventListener('click', function() {
                const maxTemp = this.textContent.trim();
                if (maxTemp && !isNaN(maxTemp)) {
                    sendCommand(`max${maxTemp}`);
                    addButtonFeedback(this);
                }
            });
        }
    });

    // Ползунок мощности
    elements.powerSlider.addEventListener('input', function() {
        elements.powerValue.textContent = `${this.value}%`;
    });

    elements.powerSlider.addEventListener('change', function() {
        setPower(this.value);
    });

    // Другие кнопки
    elements.toggleChart.addEventListener('click', toggleChart);
    elements.btnReset.addEventListener('click', () => {
        sendCommand('reset');
        addButtonFeedback(elements.btnReset);
    });
    elements.btnRefresh.addEventListener('click', () => {
        refreshData();
        addButtonFeedback(elements.btnRefresh);
    });
}

// Визуальная обратная связь для кнопок
function addButtonFeedback(button) {
    button.style.transform = 'scale(0.95)';
    button.style.opacity = '0.8';
    setTimeout(() => {
        button.style.transform = '';
        button.style.opacity = '';
    }, 150);
}

// Обновление статуса подключения
// При запуске
function updateConnectionStatus() {
    fetch('/api/status')
        .then(response => response.json())
        .then(data => {
            if (data.connected && data.ip !== '0.0.0.0') {
                // Устройство в сети
                if (elements.ipText) elements.ipText.textContent = data.ip;
                const dot = document.querySelector('.status-dot');
                if (dot) dot.className = 'status-dot online';
                
                // Опционально: обновляем иконку
                const icon = document.querySelector('#ipItem i');
                icon.className = data.rssi > -60 ? 'fas fa-wifi' : 
                                data.rssi > -70 ? 'fas fa-wifi' : 
                                'fas fa-wifi';
            } else {
                // Нет подключения
                document.getElementById('ipText').textContent = 'Нет сети';
                document.querySelector('.status-dot').className = 'status-dot offline';
            }
        })
        .catch(() => {
            // Ошибка запроса
            if (elements.ipText) elements.ipText.textContent = 'Ошибка связи';
            const dot = document.querySelector('.status-dot');
            if (dot) dot.className = 'status-dot error';
        });
}

// Обновляем статус каждые 10 секунд
setInterval(updateConnectionStatus, 10000);
updateConnectionStatus(); // Первый запуск

// Получение данных с сервера
async function fetchData() {
    try {
        const response = await fetch('/api/status');
        if (!response.ok) throw new Error('Ошибка сети');
        
        const data = await response.json();
        updateUI(data);
        
        // Обновление истории для графика
        if (appState.temperatures.return !== null) {
            updateChartData(data);
        }
        
        if (elements.lastUpdate) {
            elements.lastUpdate.textContent = new Date().toLocaleTimeString();
        }
        return true;
    } catch (error) {
        console.error('Ошибка получения данных:', error);
        if (elements.connectionStatus) {
            const dot = elements.connectionStatus.querySelector('.status-dot');
            const text = elements.connectionStatus.querySelector('span');
            if (dot) dot.className = 'status-dot offline';
            if (text) text.textContent = 'Ошибка подключения';
        }
        return false;
    }
}

// Обновление интерфейса
function updateUI(data) {
    try {
        // Температуры
        appState.temperatures.return = data.return_temp;
        appState.temperatures.flow = data.flow_temp;
        appState.temperatures.air = data.air_temp;
        appState.temperatures.delta = (data.flow_temp - data.return_temp).toFixed(1);
        
        if (elements.tempReturn) elements.tempReturn.textContent = `${data.return_temp.toFixed(1)}°C`;
        if (elements.tempFlow) elements.tempFlow.textContent = `${data.flow_temp.toFixed(1)}°C`;
        if (elements.tempAir) elements.tempAir.textContent = `${data.air_temp.toFixed(1)}°C`;
        if (elements.tempDelta) elements.tempDelta.textContent = `${appState.temperatures.delta}°C`;
        
        // Управление
        appState.control.mode = data.mode_code;
        appState.control.targetTemp = data.target_temp;
        appState.control.maxTemp = data.max_temp;
        appState.control.power = data.power;
        
        if (elements.targetTemp) elements.targetTemp.textContent = `${data.target_temp}°C`;
        if (elements.maxTemp) elements.maxTemp.textContent = `${data.max_temp}°C`;
        if (elements.powerValue) elements.powerValue.textContent = `${data.power}%`;
        if (elements.powerSlider) elements.powerSlider.value = data.power;
        
        // Режим работы
        const modeColors = {
            0: {class: 'mode-off', text: 'Выключен', color: '#e74c3c'},
            1: {class: 'mode-maintain', text: 'Авто', color: '#27ae60'},
            2: {class: 'mode-manual', text: 'Ручной', color: '#3498db'}
        };
        
        const mode = modeColors[data.mode_code] || modeColors[0];
        if (elements.modeBadge) {
            elements.modeBadge.className = 'mode-badge';
            elements.modeBadge.textContent = mode.text;
            elements.modeBadge.style.backgroundColor = mode.color;
        }
        
        // Система
        appState.system.relay = data.relay;
        appState.system.safety = !data.safety;
        appState.system.water = data.water;
        appState.system.uptime = data.uptime;
        appState.system.ip = data.ip;

        if (elements.uptime) elements.uptime.textContent = data.uptime;
        if (elements.ipText) elements.ipText.textContent = data.ip;
        
        // РЕЛЕ - показываем рабочее состояние (без мигания)
        const isRelayActive = (data.power > 0) && !data.safety && data.water;
        
        if (elements.relayIcon) {
            elements.relayIcon.innerHTML = isRelayActive 
                ? '<i class="fas fa-power-off" style="color: #27ae60"></i>'
                : '<i class="fas fa-power-off" style="color: #95a5a6"></i>';
        }
        
        if (elements.relayState) {
            elements.relayState.textContent = isRelayActive ? 'ВКЛ' : 'ВЫКЛ';
            elements.relayState.style.color = isRelayActive ? '#27ae60' : '#e74c3c';
        }
        
        // Безопасность
        if (elements.safetyIcon && elements.safetyState) {
            if (data.safety) {
                elements.safetyIcon.innerHTML = '<i class="fas fa-exclamation-triangle" style="color: #e74c3c"></i>';
                elements.safetyState.textContent = data.safety_reason || 'Ошибка';
                elements.safetyState.style.color = '#e74c3c';
            } else {
                elements.safetyIcon.innerHTML = '<i class="fas fa-check-circle" style="color: #27ae60"></i>';
                elements.safetyState.textContent = 'OK';
                elements.safetyState.style.color = '#27ae60';
            }
        }
        
        // Вода - с анимацией аварии
        if (elements.waterAlert) {
            if (!data.water) {
                elements.waterAlert.style.display = 'flex';
                // Добавляем аварийный режим
                document.body.classList.add('emergency-water');
                
                // Аварийная кнопка сброса
                if (elements.btnReset) {
                    elements.btnReset.classList.add('btn-emergency');
                    elements.btnReset.innerHTML = '<i class="fas fa-exclamation-triangle"></i> АВАРИЯ! Сброс';
                }
            } else {
                elements.waterAlert.style.display = 'none';
                document.body.classList.remove('emergency-water');
                
                if (elements.btnReset) {
                    elements.btnReset.classList.remove('btn-emergency');
                    elements.btnReset.innerHTML = '<i class="fas fa-redo"></i> Сброс ошибок';
                }
            }
        }
        
        // Перегрев - отдельное уведомление
        if (data.safety && data.safety_reason && data.safety_reason.includes("OVERHEAT")) {
            showOverheatAlert(data.safety_reason, data.flow_temp);
        } else {
            removeOverheatAlert();
        }
        
        // WiFi информация
        if (elements.wifiInfo) {
            const wifiIcon = elements.wifiInfo.querySelector('i');
            const wifiText = elements.wifiInfo.querySelector('span');

            if (wifiIcon && wifiText) {
                wifiIcon.className = 'fas fa-wifi';
                wifiText.textContent = 'WiFi подключен';
                elements.wifiInfo.style.background = 'rgba(52, 152, 219, 0.1)';
            }
        }
        
        // ПОТРЕБЛЕНИЕ - средняя мощность (без мигания)
        if (elements.consumption) {
            if (isRelayActive) {
                // Расчёт для 4кВт котла
                const avgConsumption = Math.round(4000 * data.power / 100);
                
                // Форматирование
                if (avgConsumption >= 1000) {
                    elements.consumption.textContent = `${(avgConsumption / 1000).toFixed(1)} кВт`;
                } else {
                    elements.consumption.textContent = `${avgConsumption} Вт`;
                }
                
                // Цвет в зависимости от мощности
                if (data.power > 75) {
                    elements.consumption.style.color = '#e74c3c';
                } else if (data.power > 50) {
                    elements.consumption.style.color = '#f39c12';
                } else if (data.power > 25) {
                    elements.consumption.style.color = '#27ae60';
                } else {
                    elements.consumption.style.color = '#3498db';
                }
                
            } else {
                elements.consumption.textContent = '-- Вт';
                elements.consumption.style.color = '#95a5a6';
            }
        }
        
    } catch (error) {
        console.error('Ошибка обновления UI:', error);
    }
}

// Показать уведомление о перегреве
function showOverheatAlert(reason, temperature) {
    let alert = document.getElementById('overheatAlert');
    
    if (!alert) {
        alert = document.createElement('div');
        alert.id = 'overheatAlert';
        alert.className = 'overheat-alert';
        alert.innerHTML = `
            <div class="alert-content">
                <i class="fas fa-fire"></i>
                <div class="alert-text">
                    <div class="alert-title">ПЕРЕГРЕВ!</div>
                    <div class="alert-subtitle">${reason} (${temperature.toFixed(1)}°C)</div>
                </div>
                <button class="alert-btn" onclick="sendCommand('reset')">
                    <i class="fas fa-redo"></i> Сброс
                </button>
            </div>
        `;
        
        // Вставляем после заголовка
        const header = document.querySelector('.header');
        if (header && header.parentNode) {
            header.parentNode.insertBefore(alert, header.nextSibling);
        }
    }
}

// Убрать уведомление о перегреве
function removeOverheatAlert() {
    const alert = document.getElementById('overheatAlert');
    if (alert) {
        alert.remove();
    }
}

// Обновление данных графика
function updateChartData(data) {
    const now = new Date();
    const timeLabel = now.getHours().toString().padStart(2, '0') + ':' + 
                     now.getMinutes().toString().padStart(2, '0');
    
    appState.chartData.labels.push(timeLabel);
    appState.chartData.returnData.push(data.return_temp);
    appState.chartData.flowData.push(data.flow_temp);
    
    // Ограничение количества точек
    if (appState.chartData.labels.length > config.chartHistoryPoints) {
        appState.chartData.labels.shift();
        appState.chartData.returnData.shift();
        appState.chartData.flowData.shift();
    }
    
    if (appState.chart) {
        appState.chart.update();
    }
}

// Отправка команды на сервер
async function sendCommand(cmd) {
    try {
        console.log('Отправка команды:', cmd);
        const response = await fetch('/api/control', {
            method: 'POST',
            headers: {
                'Content-Type': 'application/x-www-form-urlencoded',
            },
            body: `cmd=${encodeURIComponent(cmd)}`
        });
        
        if (response.ok) {
            console.log('Команда успешно отправлена:', cmd);
            // Немедленное обновление данных
            setTimeout(refreshData, 500);
            return true;
        } else {
            console.error('Ошибка сервера:', response.status);
            showNotification(`Ошибка: ${response.status}`, 'error');
            return false;
        }
    } catch (error) {
        console.error('Ошибка отправки команды:', error);
        showNotification('Ошибка подключения к серверу', 'error');
        return false;
    }
}

// Показать уведомление
function showNotification(message, type = 'info') {
    const notification = document.createElement('div');
    notification.className = `notification notification-${type}`;
    notification.innerHTML = `
        <div class="notification-content">
            <i class="fas fa-${type === 'error' ? 'exclamation-circle' : 'check-circle'}"></i>
            <span>${message}</span>
            <button class="notification-close">&times;</button>
        </div>
    `;
    
    // Стили для уведомления
    notification.style.cssText = `
        position: fixed;
        top: 20px;
        right: 20px;
        background: ${type === 'error' ? '#e74c3c' : '#27ae60'};
        color: white;
        padding: 15px 20px;
        border-radius: 8px;
        box-shadow: 0 5px 15px rgba(0,0,0,0.2);
        z-index: 1000;
        animation: slideIn 0.3s ease;
    `;
    
    notification.querySelector('.notification-close').onclick = () => {
        notification.style.animation = 'slideOut 0.3s ease';
        setTimeout(() => notification.remove(), 300);
    };
    
    document.body.appendChild(notification);
    
    // Автоматическое скрытие через 3 секунды
    setTimeout(() => {
        if (notification.parentNode) {
            notification.style.animation = 'slideOut 0.3s ease';
            setTimeout(() => notification.remove(), 300);
        }
    }, 3000);
}

// Установка мощности
function setPower(power) {
    if (power < 0 || power > 100) {
        console.error('Некорректное значение мощности:', power);
        showNotification('Мощность должна быть от 0 до 100%', 'error');
        return;
    }
    
    if (elements.powerValue) elements.powerValue.textContent = `${power}%`;
    if (elements.powerSlider) elements.powerSlider.value = power;
    
    sendCommand(`power${power}`)
        .then(success => {
            if (success) {
                console.log(`Мощность установлена на ${power}%`);
                showNotification(`Мощность: ${power}%`, 'info');
            }
        });
}

// Обновление всех данных
function refreshData() {
    fetchData();
}

// Переключение отображения графика
function toggleChart() {
    const chartContainer = document.querySelector('.chart-container');
    const icon = elements.toggleChart.querySelector('i');

    if (!chartContainer || !icon) return;

    const isExpanded = chartContainer.style.height === '500px';

    if (isExpanded) {
        chartContainer.style.height = '350px';
        icon.className = 'fas fa-expand-alt';
    } else {
        chartContainer.style.height = '500px';
        icon.className = 'fas fa-compress-alt';
    }
    
    setTimeout(() => {
        if (appState.chart) {
            appState.chart.resize();
        }
    }, 300);
    
    addButtonFeedback(elements.toggleChart);
}

// Автоматическое обновление
function startAutoUpdate() {
    setInterval(refreshData, config.updateInterval);
}

// События сети
window.addEventListener('online', updateConnectionStatus);
window.addEventListener('offline', updateConnectionStatus);

// Горячие клавиши
document.addEventListener('keydown', function(event) {
    if (event.target.tagName === 'INPUT' || event.target.tagName === 'TEXTAREA') {
        return;
    }
    
    switch(event.key.toLowerCase()) {
        case '1': 
            if (elements.btnAuto) {
                sendCommand('on');
                addButtonFeedback(elements.btnAuto);
            }
            break;
        case '2': 
            if (elements.btnOff) {
                sendCommand('off');
                addButtonFeedback(elements.btnOff);
            }
            break;
        case '3': 
            if (elements.btnManual) {
                sendCommand('manual');
                addButtonFeedback(elements.btnManual);
            }
            break;
        case 'r': 
            refreshData();
            if (elements.btnRefresh) addButtonFeedback(elements.btnRefresh);
            break;
        case '+': 
            if (elements.powerSlider) {
                const currentPower = parseInt(elements.powerSlider.value);
                if (currentPower < 100) setPower(currentPower + 10);
            }
            break;
        case '-': 
            if (elements.powerSlider) {
                const currentPower = parseInt(elements.powerSlider.value);
                if (currentPower > 0) setPower(currentPower - 10);
            }
            break;
        case 'f': 
            toggleChart();
            break;
    }
});

// Добавляем CSS анимации
const style = document.createElement('style');
style.textContent = `
    @keyframes slideIn {
        from {
            transform: translateX(100%);
            opacity: 0;
        }
        to {
            transform: translateX(0);
            opacity: 1;
        }
    }
    
    @keyframes slideOut {
        from {
            transform: translateX(0);
            opacity: 1;
        }
        to {
            transform: translateX(100%);
            opacity: 0;
        }
    }
    
    .notification-content {
        display: flex;
        align-items: center;
        gap: 10px;
    }
    
    .notification-close {
        background: none;
        border: none;
        color: white;
        font-size: 20px;
        cursor: pointer;
        margin-left: 10px;
    }
`;
document.head.appendChild(style);