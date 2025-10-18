// this page provides minimalistic WebSocket testing dashboard
// disclaimer: this page and code was generated with the help of DeepSeek AI tool

static const char *htmlPage = R"(
<!DOCTYPE html>
<html lang="en">
<head>
    <meta charset="UTF-8">
    <meta name="viewport" content="width=device-width, initial-scale=1.0">
    <title>WebSocket Dashboard</title>
    <!-- disclaimer: this page and code was generated with the help of DeepSeek AI tool -->
    <style>
* {
    margin: 0;
    padding: 0;
    box-sizing: border-box;
}

body {
    font-family: 'Segoe UI', Tahoma, Geneva, Verdana, sans-serif;
    background: linear-gradient(135deg, #667eea 0%, #764ba2 100%);
    min-height: 100vh;
    padding: 20px;
}

.container {
    max-width: 1000px;
    margin: 0 auto;
    background: white;
    border-radius: 15px;
    box-shadow: 0 10px 30px rgba(0, 0, 0, 0.3);
    overflow: hidden;
}

h1 {
    background: linear-gradient(135deg, #2c3e50, #3498db);
    color: white;
    padding: 20px;
    text-align: center;
    font-size: 1.5em;
}

.section {
    border-bottom: 1px solid #e9ecef;
}

.section:last-child {
    border-bottom: none;
}

.section-header {
    padding: 15px 20px;
    background: #f8f9fa;
    display: flex;
    justify-content: space-between;
    align-items: center;
    flex-wrap: wrap;
    gap: 15px;
}

.section-header h2 {
    color: #2c3e50;
    font-size: 1.2em;
}

.controls {
    display: flex;
    align-items: center;
    gap: 15px;
    flex-wrap: wrap;
}

button {
    padding: 8px 16px;
    border: none;
    border-radius: 5px;
    cursor: pointer;
    font-weight: bold;
    transition: all 0.3s ease;
    font-size: 0.9em;
}

.btn-connect {
    background: #28a745;
    color: white;
}

.btn-connect:hover:not(:disabled) {
    background: #218838;
    transform: translateY(-1px);
}

.btn-disconnect {
    background: #dc3545;
    color: white;
}

.btn-disconnect:hover:not(:disabled) {
    background: #c82333;
    transform: translateY(-1px);
}

button:disabled {
    background: #6c757d;
    cursor: not-allowed;
    transform: none !important;
}

.status {
    font-weight: bold;
    font-size: 0.9em;
}

.status-disconnected {
    background: #f8d7da;
    color: #721c24;
    padding: 4px 8px;
    border-radius: 12px;
}

.status-connecting {
    background: #fff3cd;
    color: #856404;
    padding: 4px 8px;
    border-radius: 12px;
}

.status-connected {
    background: #d4edda;
    color: #155724;
    padding: 4px 8px;
    border-radius: 12px;
}

/* Log Messages */
.log-container {
    padding: 15px;
}

.messages-box {
    height: 200px;
    border: 1px solid #ddd;
    border-radius: 8px;
    padding: 10px;
    overflow-y: auto;
    background: #fafafa;
    font-family: 'Courier New', monospace;
    font-size: 0.85em;
    line-height: 1.4;
}

.log-message {
    margin-bottom: 4px;
    padding: 2px 4px;
    border-radius: 3px;
}

.log-message.info {
    background: #e3f2fd;
    border-left: 3px solid #2196f3;
}

.log-message.error {
    background: #ffebee;
    border-left: 3px solid #f44336;
    color: #c62828;
}

.log-message.warning {
    background: #fff8e1;
    border-left: 3px solid #ff9800;
    color: #ef6c00;
}

/* Chat Section */
.chat-container {
    padding: 15px;
}

.input-container {
    display: flex;
    gap: 10px;
    margin-top: 10px;
}

#messageInput {
    flex: 1;
    padding: 10px;
    border: 1px solid #ddd;
    border-radius: 5px;
    font-size: 0.9em;
}

#messageInput:focus {
    outline: none;
    border-color: #3498db;
}

#sendBtn {
    background: #3498db;
    color: white;
    padding: 10px 20px;
}

#sendBtn:hover:not(:disabled) {
    background: #2980b9;
}

.chat-message {
    margin-bottom: 8px;
    padding: 8px;
    border-radius: 5px;
}

.chat-message.sent {
    background: #e3f2fd;
    border-left: 3px solid #2196f3;
    text-align: right;
}

.chat-message.received {
    background: #f5f5f5;
    border-left: 3px solid #4caf50;
}

/* Stats Section */
.stats-container {
    padding: 20px;
}

.stats-grid {
    display: grid;
    grid-template-columns: repeat(auto-fit, minmax(200px, 1fr));
    gap: 15px;
}

.stat-item {
    background: #f8f9fa;
    padding: 15px;
    border-radius: 8px;
    border-left: 4px solid #3498db;
}

.stat-item label {
    display: block;
    font-weight: bold;
    color: #2c3e50;
    margin-bottom: 5px;
    font-size: 0.9em;
}

.stat-item span {
    font-family: 'Courier New', monospace;
    font-size: 1.1em;
    color: #e74c3c;
    font-weight: bold;
}

/* Scrollbar styling */
.messages-box::-webkit-scrollbar {
    width: 6px;
}

.messages-box::-webkit-scrollbar-track {
    background: #f1f1f1;
    border-radius: 3px;
}

.messages-box::-webkit-scrollbar-thumb {
    background: #c1c1c1;
    border-radius: 3px;
}

.messages-box::-webkit-scrollbar-thumb:hover {
    background: #a8a8a8;
}

@media (max-width: 768px) {
    .container {
        margin: 10px;
    }
    
    .section-header {
        flex-direction: column;
        align-items: flex-start;
    }
    
    .controls {
        width: 100%;
        justify-content: space-between;
    }
    
    .stats-grid {
        grid-template-columns: 1fr;
    }
    
    .input-container {
        flex-direction: column;
    }
    
    #sendBtn {
        width: 100%;
    }
}

.header {
    background: linear-gradient(135deg, #2c3e50, #3498db);
    color: white;
    padding: 20px;
    text-align: center;
}

.header h1 {
    margin: 0;
    padding: 0;
    background: none;
}

.server-info {
    margin-top: 10px;
    font-size: 0.9em;
    opacity: 0.9;
}

.server-info span {
    font-family: 'Courier New', monospace;
    background: rgba(255, 255, 255, 0.2);
    padding: 2px 6px;
    border-radius: 3px;
}

.endpoint {
    font-size: 0.7em;
    background: #3498db;
    color: white;
    padding: 2px 8px;
    border-radius: 10px;
    margin-left: 8px;
    font-weight: normal;
    font-family: 'Courier New', monospace;
}
    </style>
</head>
<body>
    <div class="container">
        <div class="header">
            <h1>WebSocket Dashboard</h1>
            <div class="server-info">
                Server: <span id="serverAddress">ws://localhost:8080</span>
            </div>
        </div>
        
        <!-- Log Messages Section -->
        <div class="section">
            <div class="section-header">
                <h2>Log Messages <span class="endpoint">/ws</span></h2>
                <div class="status">
                    Status: <span id="logStatus" class="status-disconnected">Disconnected</span>
                </div>
            </div>
            <div class="log-container">
                <div id="logMessages" class="messages-box" title="WebSocket endpoint: /ws"></div>
            </div>
        </div>

        <!-- Echo Chat Section -->
        <div class="section">
            <div class="section-header">
                <h2>Echo Chat <span class="endpoint">/wsecho</span></h2>
                <div class="controls">
                    <button id="echoConnectBtn" class="btn-connect">Connect</button>
                    <button id="echoDisconnectBtn" class="btn-disconnect" disabled>Disconnect</button>
                    <div class="status">
                        Status: <span id="echoStatus" class="status-disconnected">Disconnected</span>
                    </div>
                </div>
            </div>
            <div class="chat-container">
                <div id="echoMessages" class="messages-box" title="WebSocket endpoint: /wsecho"></div>
                <div class="input-container">
                    <input type="text" id="messageInput" placeholder="Type your message..." disabled>
                    <button id="sendBtn" disabled>Send</button>
                </div>
            </div>
        </div>

        <!-- Speed Test Section -->
        <div class="section">
            <div class="section-header">
                <h2>Speed Test <span class="endpoint">/wsspeed</span></h2>
                <div class="controls">
                    <button id="speedConnectBtn" class="btn-connect">Connect</button>
                    <button id="speedDisconnectBtn" class="btn-disconnect" disabled>Disconnect</button>
                    <div class="status">
                        Status: <span id="speedStatus" class="status-disconnected">Disconnected</span>
                    </div>
                </div>
            </div>
            <div class="stats-container">
                <div class="stats-grid">
                    <div class="stat-item">
                        <label>FPS:</label>
                        <span id="fpsCounter">0</span>
                    </div>
                    <div class="stat-item">
                        <label>Frame Count:</label>
                        <span id="frameCounter">0</span>
                    </div>
                    <div class="stat-item">
                        <label>Incoming Data Rate:</label>
                        <span id="dataRate">0 B/s</span>
                    </div>
                    <div class="stat-item">
                        <label>Total Data:</label>
                        <span id="totalData">0 B</span>
                    </div>
                    <div class="stat-item">
                        <label>Avg Frame Size:</label>
                        <span id="avgFrameSize">0 B</span>
                    </div>
                    <div class="stat-item">
                        <label>Connection Time:</label>
                        <span id="connectionTime">0s</span>
                    </div>
                </div>
            </div>
        </div>
    </div>

    <script>
class WebSocketDashboard {
    constructor() {
        this.baseUrl = 'ws://' + location.host; // Change to your server URL
        
        // WebSocket connections
        this.logWs = null;
        this.echoWs = null;
        this.speedWs = null;
        
        // Message storage
        this.logMessages = [];
        this.echoMessages = [];
        this.maxLogMessages = 100;
        
        // Speed test statistics
        this.speedStats = {
            frameCount: 0,
            totalBytes: 0,
            startTime: 0,
            lastFpsUpdate: 0,
            framesThisSecond: 0,
            currentFps: 0,
            dataRate: 0,
            connectionTimer: 0,
            isConnected: false
        };
        
        this.initializeElements();
        this.initializeEventListeners();
        this.connectLogWebSocket();
        this.updateStatsDisplay();
    }

    initializeElements() {
        // Server info
        this.serverAddressElement = document.getElementById('serverAddress');
        this.serverAddressElement.textContent = this.baseUrl;
        
        // Status elements
        this.logStatusElement = document.getElementById('logStatus');
        this.echoStatusElement = document.getElementById('echoStatus');
        this.speedStatusElement = document.getElementById('speedStatus');
        
        // Message containers
        this.logMessagesElement = document.getElementById('logMessages');
        this.echoMessagesElement = document.getElementById('echoMessages');
        
        // Buttons
        this.echoConnectBtn = document.getElementById('echoConnectBtn');
        this.echoDisconnectBtn = document.getElementById('echoDisconnectBtn');
        this.speedConnectBtn = document.getElementById('speedConnectBtn');
        this.speedDisconnectBtn = document.getElementById('speedDisconnectBtn');
        this.sendBtn = document.getElementById('sendBtn');
        this.messageInput = document.getElementById('messageInput');
        
        // Stats elements
        this.fpsCounter = document.getElementById('fpsCounter');
        this.frameCounter = document.getElementById('frameCounter');
        this.dataRate = document.getElementById('dataRate');
        this.totalData = document.getElementById('totalData');
        this.avgFrameSize = document.getElementById('avgFrameSize');
        this.connectionTime = document.getElementById('connectionTime');
    }

    initializeEventListeners() {
        // Echo chat buttons
        this.echoConnectBtn.addEventListener('click', () => this.connectEchoWebSocket());
        this.echoDisconnectBtn.addEventListener('click', () => this.disconnectEchoWebSocket());
        
        // Speed test buttons
        this.speedConnectBtn.addEventListener('click', () => this.connectSpeedWebSocket());
        this.speedDisconnectBtn.addEventListener('click', () => this.disconnectSpeedWebSocket());
        
        // Chat input
        this.sendBtn.addEventListener('click', () => this.sendMessage());
        this.messageInput.addEventListener('keypress', (e) => {
            if (e.key === 'Enter') {
                this.sendMessage();
            }
        });
    }

    // Log WebSocket (/ws)
    connectLogWebSocket() {
        try {
            this.logWs = new WebSocket(`${this.baseUrl}/ws`);
            
            this.logWs.onopen = () => {
                this.updateStatus(this.logStatusElement, 'connected', 'Connected');
                this.addLogMessage('info', 'Log WebSocket connected');
            };
            
            this.logWs.onmessage = (event) => {
                this.addLogMessage('info', event.data);
            };
            
            this.logWs.onclose = (event) => {
                this.updateStatus(this.logStatusElement, 'disconnected', 'Disconnected');
                this.addLogMessage('warning', `Log WebSocket disconnected: ${event.code} ${event.reason || ''}`);
                
                // Auto-reconnect after 3 seconds
                setTimeout(() => {
                    if (!this.logWs || this.logWs.readyState === WebSocket.CLOSED) {
                        this.connectLogWebSocket();
                    }
                }, 3000);
            };
            
            this.logWs.onerror = (error) => {
                this.addLogMessage('error', 'Log WebSocket error');
            };
            
        } catch (error) {
            this.addLogMessage('error', `Failed to connect to log WebSocket: ${error}`);
        }
    }

    // Echo Chat WebSocket (/wsecho)
    connectEchoWebSocket() {
        try {
            this.echoWs = new WebSocket(`${this.baseUrl}/wsecho`);
            
            this.echoWs.onopen = () => {
                this.updateStatus(this.echoStatusElement, 'connected', 'Connected');
                this.toggleEchoButtons(true);
                this.addEchoMessage('system', 'Connected to echo server');
            };
            
            this.echoWs.onmessage = (event) => {
                this.addEchoMessage('received', event.data);
            };
            
            this.echoWs.onclose = (event) => {
                this.updateStatus(this.echoStatusElement, 'disconnected', 'Disconnected');
                this.toggleEchoButtons(false);
                this.addEchoMessage('system', 'Disconnected from echo server');
            };
            
            this.echoWs.onerror = (error) => {
                this.addEchoMessage('system', 'Connection error');
            };
            
        } catch (error) {
            this.addEchoMessage('system', `Failed to connect: ${error}`);
        }
    }

    disconnectEchoWebSocket() {
        if (this.echoWs) {
            this.echoWs.close(1000, 'User disconnected');
            this.echoWs = null;
        }
    }

    // Speed Test WebSocket (/wsspeed)
    connectSpeedWebSocket() {
        try {
            this.speedWs = new WebSocket(`${this.baseUrl}/wsspeed`);
            this.speedWs.binaryType = 'arraybuffer';
            
            this.resetSpeedStats();
            
            this.speedWs.onopen = () => {
                this.updateStatus(this.speedStatusElement, 'connected', 'Connected');
                this.toggleSpeedButtons(true);
                this.speedStats.startTime = Date.now();
                this.speedStats.isConnected = true;
                this.startConnectionTimer();
            };
            
            this.speedWs.onmessage = (event) => {
                if (event.data instanceof ArrayBuffer) {
                    this.processSpeedFrame(event.data);
                }
            };
            
            this.speedWs.onclose = (event) => {
                this.updateStatus(this.speedStatusElement, 'disconnected', 'Disconnected');
                this.toggleSpeedButtons(false);
                this.speedStats.isConnected = false;
                this.stopConnectionTimer();
            };
            
            this.speedWs.onerror = (error) => {
                console.error('Speed WebSocket error:', error);
                this.speedStats.isConnected = false;
                this.stopConnectionTimer();
            };
            
        } catch (error) {
            console.error('Failed to connect to speed WebSocket:', error);
        }
    }

    disconnectSpeedWebSocket() {
        if (this.speedWs) {
            this.speedWs.close(1000, 'User disconnected');
            this.speedWs = null;
            this.speedStats.isConnected = false;
            this.stopConnectionTimer();
        }
    }

    startConnectionTimer() {
        if (this.speedStats.connectionTimer) {
            clearInterval(this.speedStats.connectionTimer);
        }
        
        this.speedStats.connectionTimer = setInterval(() => {
            if (this.speedStats.isConnected) {
                const connectionTime = Math.floor((Date.now() - this.speedStats.startTime) / 1000);
                this.connectionTime.textContent = `${connectionTime}s`;
            }
        }, 1000);
    }

    stopConnectionTimer() {
        if (this.speedStats.connectionTimer) {
            clearInterval(this.speedStats.connectionTimer);
            this.speedStats.connectionTimer = 0;
        }
    }

    processSpeedFrame(arrayBuffer) {
        const frameSize = arrayBuffer.byteLength;
        
        // Update statistics
        this.speedStats.frameCount++;
        this.speedStats.totalBytes += frameSize;
        
        // Calculate FPS
        const now = performance.now();
        if (now >= this.speedStats.lastFpsUpdate + 1000) {
            this.speedStats.currentFps = this.speedStats.framesThisSecond;
            
            // Calculate data rate (bytes per second)
            const avgFrameSize = this.speedStats.frameCount > 0 ? 
                this.speedStats.totalBytes / this.speedStats.frameCount : 0;
            this.speedStats.dataRate = this.speedStats.framesThisSecond * avgFrameSize;
            
            this.speedStats.framesThisSecond = 0;
            this.speedStats.lastFpsUpdate = now;
        }
        this.speedStats.framesThisSecond++;
    }

    resetSpeedStats() {
        this.speedStats = {
            frameCount: 0,
            totalBytes: 0,
            startTime: 0,
            lastFpsUpdate: 0,
            framesThisSecond: 0,
            currentFps: 0,
            dataRate: 0,
            connectionTimer: 0,
            isConnected: false
        };
        
        // Reset display values
        this.fpsCounter.textContent = '0';
        this.frameCounter.textContent = '0';
        this.dataRate.textContent = '0 B/s';
        this.totalData.textContent = '0 B';
        this.avgFrameSize.textContent = '0 B';
        this.connectionTime.textContent = '0s';
    }

    // Message handling
    addLogMessage(type, message) {
        const timestamp = new Date().toLocaleTimeString();
        const logEntry = {
            type,
            message: `[${timestamp}] ${message}`,
            timestamp: Date.now()
        };
        
        // Add to end (bottom) for normal scrolling
        this.logMessages.push(logEntry);
        
        // Keep only last 100 messages
        if (this.logMessages.length > this.maxLogMessages) {
            this.logMessages = this.logMessages.slice(-this.maxLogMessages);
        }
        
        this.updateLogDisplay();
    }

    updateLogDisplay() {
        this.logMessagesElement.innerHTML = '';
        
        this.logMessages.forEach(entry => {
            const messageElement = document.createElement('div');
            messageElement.className = `log-message ${entry.type}`;
            messageElement.textContent = entry.message;
            this.logMessagesElement.appendChild(messageElement);
        });
        
        // Auto-scroll to bottom to show latest messages
        this.logMessagesElement.scrollTop = this.logMessagesElement.scrollHeight;
    }

    addEchoMessage(type, message) {
        const messageElement = document.createElement('div');
        messageElement.className = `chat-message ${type}`;
        
        const timestamp = new Date().toLocaleTimeString();
        
        if (type === 'sent') {
            messageElement.textContent = `You: ${message}`;
        } else if (type === 'received') {
            messageElement.textContent = `Echo: ${message}`;
        } else {
            messageElement.textContent = `[${timestamp}] ${message}`;
            messageElement.style.fontStyle = 'italic';
            messageElement.style.color = '#666';
        }
        
        this.echoMessagesElement.appendChild(messageElement);
        this.echoMessagesElement.scrollTop = this.echoMessagesElement.scrollHeight;
        
        // Keep reasonable history
        if (this.echoMessagesElement.children.length > 50) {
            this.echoMessagesElement.removeChild(this.echoMessagesElement.firstChild);
        }
    }

    sendMessage() {
        const message = this.messageInput.value.trim();
        if (message && this.echoWs && this.echoWs.readyState === WebSocket.OPEN) {
            this.echoWs.send(message);
            this.addEchoMessage('sent', message);
            this.messageInput.value = '';
        }
    }

    // UI Updates
    updateStatus(element, statusClass, text) {
        element.className = `status-${statusClass}`;
        element.textContent = text;
    }

    toggleEchoButtons(connected) {
        this.echoConnectBtn.disabled = connected;
        this.echoDisconnectBtn.disabled = !connected;
        this.messageInput.disabled = !connected;
        this.sendBtn.disabled = !connected;
    }

    toggleSpeedButtons(connected) {
        this.speedConnectBtn.disabled = connected;
        this.speedDisconnectBtn.disabled = !connected;
    }

    updateStatsDisplay() {
        // Update FPS
        this.fpsCounter.textContent = this.speedStats.currentFps;
        
        // Update frame count
        this.frameCounter.textContent = this.speedStats.frameCount;
        
        // Update data rate (already labeled as "Incoming Data Rate" in HTML)
        this.dataRate.textContent = this.formatBytes(this.speedStats.dataRate) + '/s';
        
        // Update total data
        this.totalData.textContent = this.formatBytes(this.speedStats.totalBytes);
        
        // Update average frame size
        const avgSize = this.speedStats.frameCount > 0 ? 
            this.speedStats.totalBytes / this.speedStats.frameCount : 0;
        this.avgFrameSize.textContent = this.formatBytes(avgSize);
        
        // Connection time is now updated by the timer, so we don't need to update it here
        
        // Continue updating
        requestAnimationFrame(() => this.updateStatsDisplay());
    }

    formatBytes(bytes) {
        if (bytes === 0 || isNaN(bytes)) return '0 B';
        
        const k = 1024;
        const sizes = ['B', 'KB', 'MB', 'GB'];
        const i = Math.floor(Math.log(bytes) / Math.log(k));
        
        return parseFloat((bytes / Math.pow(k, i)).toFixed(2)) + ' ' + sizes[i];
    }
}

// Initialize the dashboard when the page loads
document.addEventListener('DOMContentLoaded', () => {
    window.dashboard = new WebSocketDashboard();
});

// Handle page beforeunload to properly close connections
window.addEventListener('beforeunload', () => {
    const dashboard = window.dashboard;
    if (dashboard) {
        if (dashboard.logWs) dashboard.logWs.close(1000, 'Page unloading');
        if (dashboard.echoWs) dashboard.echoWs.close(1000, 'Page unloading');
        if (dashboard.speedWs) dashboard.speedWs.close(1000, 'Page unloading');
    }
});    
    </script>
</body>
</html>
)";
