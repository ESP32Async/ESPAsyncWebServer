// this page provides minimalistic WebSocket MJPEG CAM streaming app
// disclaimer: this page and code was generated with the help of DeepSeek AI tool

static const char *htmlPage = R"(
<!DOCTYPE html>
<html lang="en">
<head>
    <meta charset="UTF-8">
    <meta name="viewport" content="width=device-width, initial-scale=1.0">
    <title>MJPEG WebSocket Stream</title>
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
        max-width: 800px;
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
    
    .controls {
        padding: 20px;
        background: #f8f9fa;
        border-bottom: 1px solid #e9ecef;
        display: flex;
        justify-content: space-between;
        align-items: center;
        flex-wrap: wrap;
        gap: 15px;
    }
    
    button {
        padding: 10px 20px;
        border: none;
        border-radius: 5px;
        cursor: pointer;
        font-weight: bold;
        transition: all 0.3s ease;
    }
    
    #connectBtn {
        background: #28a745;
        color: white;
    }
    
    #connectBtn:hover:not(:disabled) {
        background: #218838;
        transform: translateY(-2px);
    }
    
    #disconnectBtn {
        background: #dc3545;
        color: white;
    }
    
    #disconnectBtn:hover:not(:disabled) {
        background: #c82333;
        transform: translateY(-2px);
    }
    
    button:disabled {
        background: #6c757d;
        cursor: not-allowed;
        transform: none !important;
    }
    
    .status {
        font-weight: bold;
    }
    
    #status {
        padding: 5px 10px;
        border-radius: 15px;
        font-size: 0.9em;
    }
    
    #status.connected {
        background: #d4edda;
        color: #155724;
    }
    
    #status.disconnected {
        background: #f8d7da;
        color: #721c24;
    }
    
    #status.connecting {
        background: #fff3cd;
        color: #856404;
    }
    
    .video-container {
        padding: 20px;
        text-align: center;
        background: #000;
    }
    
    #videoCanvas {
        border: 3px solid #34495e;
        border-radius: 10px;
        background: #000;
        max-width: 100%;
        height: auto;
        box-shadow: 0 5px 15px rgba(0, 0, 0, 0.5);
    }
    
    .info {
        padding: 15px;
        background: #f8f9fa;
        border-top: 1px solid #e9ecef;
        display: flex;
        justify-content: space-around;
        flex-wrap: wrap;
        gap: 20px;
    }
    
    .info-item {
        text-align: center;
    }
    
    .info-item p {
        margin: 5px 0;
        font-weight: bold;
        color: #495057;
        font-size: 0.9em;
    }
    
    .info-value {
        font-family: 'Courier New', monospace;
        background: #e9ecef;
        padding: 2px 6px;
        border-radius: 3px;
        margin-left: 5px;
    }
    
    @media (max-width: 600px) {
        .controls {
            flex-direction: column;
            text-align: center;
        }
        
        .container {
            margin: 10px;
        }
        
        #videoCanvas {
            width: 100%;
            height: auto;
        }
        
        .info {
            flex-direction: column;
            gap: 10px;
        }
        
        .info-item {
            text-align: left;
        }
    }
    </style>
</head>
<body>
    <div class="container">
        <h1>MJPEG WebSocket Video Stream</h1>
        
        <div class="controls">
            <button id="connectBtn">Connect</button>
            <button id="disconnectBtn" disabled>Disconnect</button>
            <div class="status">
                Status: <span id="status">Disconnected</span>
            </div>
        </div>

        <div class="video-container">
            <canvas id="videoCanvas" width="640" height="480"></canvas>
        </div>

        <div class="info">
            <div class="info-item">
                <p>FPS: <span id="fpsCounter">0</span></p>
                <p>Frame Count: <span id="frameCounter">0</span></p>
            </div>
            <div class="info-item">
                <p>Frame Size: <span id="frameSize">0</span> bytes</p>
                <p>Memory: <span id="memory">0</span> bytes free</p>
                <p>PSRAM: <span id="psram">0</span> bytes free</p>
            </div>
        </div>
    </div>

    <script>
class MJPEGStreamPlayer {
    constructor() {
        this.ws = null;
        this.canvas = document.getElementById('videoCanvas');
        this.ctx = this.canvas.getContext('2d');
        this.statusElement = document.getElementById('status');
        this.fpsCounter = document.getElementById('fpsCounter');
        this.frameCounterElement = document.getElementById('frameCounter');
        
        // Info display elements
        this.frameSizeElement = document.getElementById('frameSize');
        this.memoryElement = document.getElementById('memory');
        this.psramElement = document.getElementById('psram');
        
        this.frameCount = 0;
        this.lastFpsUpdate = 0;
        this.framesThisSecond = 0;
        this.currentFps = 0;
        
        // Frame info storage
        this.lastFrameInfo = {
            frameSize: 0,
            memory: 0,
            psram: 0,
            lastUpdate: 0
        };
        
        this.isPlaying = false;
        this.pendingFrames = 0;
        this.maxPendingFrames = 2; // Limit concurrent frame processing
        
        this.initializeEventListeners();
        this.updateFpsCounter();
    }

    initializeEventListeners() {
        document.getElementById('connectBtn').addEventListener('click', () => this.connect());
        document.getElementById('disconnectBtn').addEventListener('click', () => this.disconnect());
    }

    connect() {
        this.updateStatus('connecting', 'Connecting...');
        
        try {
            this.ws = new WebSocket('ws://' + location.host + '/wsstream');
            
            this.ws.binaryType = 'arraybuffer';
            
            this.ws.onopen = () => {
                this.updateStatus('connected', 'Connected');
                this.toggleButtons(true);
                this.isPlaying = true;
                this.pendingFrames = 0;
                console.log('WebSocket connected');
            };
            
            this.ws.onmessage = (event) => {
                if (event.data instanceof ArrayBuffer) {
                    // Binary message - MJPEG frame
                    this.processFrame(event.data);
                } else if (typeof event.data === 'string') {
                    // Text message - frame information
                    this.processInfoMessage(event.data);
                }
            };
            
            this.ws.onclose = (event) => {
                this.updateStatus('disconnected', 'Disconnected');
                this.toggleButtons(false);
                this.isPlaying = false;
                console.log('WebSocket disconnected:', event.code, event.reason);
/*
                // Attempt reconnect if not manually disconnected
                if (event.code !== 1000) { // 1000 = normal closure
                    console.log('Attempting to reconnect in 2 seconds...');
                    setTimeout(() => {
                        if (!this.ws || this.ws.readyState === WebSocket.CLOSED) {
                            this.connect();
                        }
                    }, 2000);
                }
*/
            };
            
            this.ws.onerror = (error) => {
                console.error('WebSocket error:', error);
                // Note: onclose will be called after onerror
            };
            
        } catch (error) {
            console.error('Failed to connect:', error);
            this.updateStatus('disconnected', 'Connection Failed');
        }
    }

    disconnect() {
        this.isPlaying = false;
        if (this.ws) {
            // Use normal closure code
            this.ws.close(1000, 'User disconnected');
            this.ws = null;
        }
        this.clearCanvas();
    }

    processFrame(arrayBuffer) {
        if (!this.isPlaying || this.pendingFrames >= this.maxPendingFrames) {
            // Skip frame if not playing or too many pending frames
            return;
        }

        this.pendingFrames++;
        
        try {
            // Create blob and process frame
            const blob = new Blob([arrayBuffer], { type: 'image/jpeg' });
            const url = URL.createObjectURL(blob);
            
            const img = new Image();
            
            img.onload = () => {
                this.ctx.clearRect(0, 0, this.canvas.width, this.canvas.height);
                this.ctx.drawImage(img, 0, 0, this.canvas.width, this.canvas.height);
                URL.revokeObjectURL(url);
                this.updateCounters();
                this.updateFrameInfoDisplay();
                this.pendingFrames--;
            };
            
            img.onerror = (error) => {
                console.error('Failed to load image frame:', error);
                URL.revokeObjectURL(url);
                this.pendingFrames--;
            };
            
            // Start loading the image
            img.src = url;
            
        } catch (error) {
            console.error('Error processing frame:', error);
            this.pendingFrames--;
        }
    }

    processInfoMessage(message) {
        console.log('Received info message:', message.trim());
        
        try {
            // Parse the info message format: "FrameSize:%u, Mem:%u, psram:%u\n"
            const frameSizeMatch = message.match(/FrameSize:\s*(\d+)/);
            const memoryMatch = message.match(/Mem:\s*(\d+)/);
            const psramMatch = message.match(/PSRAM:\s*(\d+)/);
            
            if (frameSizeMatch || memoryMatch || psramMatch) {
                this.lastFrameInfo = {
                    frameSize: frameSizeMatch ? parseInt(frameSizeMatch[1]) : this.lastFrameInfo.frameSize,
                    memory: memoryMatch ? parseInt(memoryMatch[1]) : this.lastFrameInfo.memory,
                    psram: psramMatch ? parseInt(psramMatch[1]) : this.lastFrameInfo.psram,
                    lastUpdate: Date.now()
                };
                
                this.updateFrameInfoDisplay();
            } else {
                console.warn('Unknown info message format:', message.trim());
            }
        } catch (error) {
            console.error('Error parsing info message:', error, message);
        }
    }

    updateFrameInfoDisplay() {
        try {
            // Format and display the frame information
            this.frameSizeElement.textContent = this.formatBytes(this.lastFrameInfo.frameSize);
            this.memoryElement.textContent = this.formatBytes(this.lastFrameInfo.memory);
            this.psramElement.textContent = this.formatBytes(this.lastFrameInfo.psram);
            
            // Add timestamp indicator if info is stale (older than 5 seconds)
            const now = Date.now();
            const isStale = (now - this.lastFrameInfo.lastUpdate) > 5000;
            
            const elements = [this.frameSizeElement, this.memoryElement, this.psramElement];
            elements.forEach(element => {
                if (isStale && this.lastFrameInfo.lastUpdate > 0) {
                    element.style.color = '#6c757d';
                    element.style.fontStyle = 'italic';
                    element.title = 'Info may be outdated (last update: ' + 
                        new Date(this.lastFrameInfo.lastUpdate).toLocaleTimeString() + ')';
                } else {
                    element.style.color = '';
                    element.style.fontStyle = '';
                    element.title = 'Last update: ' + 
                        new Date(this.lastFrameInfo.lastUpdate).toLocaleTimeString();
                }
            });
        } catch (error) {
            console.error('Error updating frame info display:', error);
        }
    }

    formatBytes(bytes) {
        if (bytes === 0 || isNaN(bytes)) return '0 B';
        
        const k = 1024;
        const sizes = ['B', 'KB', 'MB', 'GB'];
        const i = Math.floor(Math.log(bytes) / Math.log(k));
        
        return parseFloat((bytes / Math.pow(k, i)).toFixed(2)) + ' ' + sizes[i];
    }

    updateCounters() {
        this.frameCount++;
        this.frameCounterElement.textContent = this.frameCount;
        
        // Calculate FPS
        const now = performance.now();
        if (now >= this.lastFpsUpdate + 1000) {
            this.currentFps = this.framesThisSecond;
            this.framesThisSecond = 0;
            this.lastFpsUpdate = now;
        }
        this.framesThisSecond++;
    }

    updateFpsCounter() {
        this.fpsCounter.textContent = this.currentFps;
        requestAnimationFrame(() => this.updateFpsCounter());
    }

    clearCanvas() {
        try {
            this.ctx.fillStyle = 'black';
            this.ctx.fillRect(0, 0, this.canvas.width, this.canvas.height);
            this.ctx.fillStyle = 'white';
            this.ctx.font = '20px Arial';
            this.ctx.textAlign = 'center';
            this.ctx.fillText('Stream Disconnected', this.canvas.width / 2, this.canvas.height / 2);
            
            // Reset info display
            this.frameSizeElement.textContent = '0 B';
            this.memoryElement.textContent = '0 B';
            this.psramElement.textContent = '0 B';
            this.frameCount = 0;
            this.frameCounterElement.textContent = '0';
            this.currentFps = 0;
        } catch (error) {
            console.error('Error clearing canvas:', error);
        }
    }

    updateStatus(className, text) {
        this.statusElement.className = className;
        this.statusElement.textContent = text;
    }

    toggleButtons(connected) {
        document.getElementById('connectBtn').disabled = connected;
        document.getElementById('disconnectBtn').disabled = !connected;
    }
}

// Alternative method using createImageBitmap for better performance
class MJPEGStreamPlayerOptimized extends MJPEGStreamPlayer {
    processFrame(arrayBuffer) {
        if (!this.isPlaying || this.pendingFrames >= this.maxPendingFrames) {
            return;
        }

        this.pendingFrames++;
        
        try {
            createImageBitmap(new Blob([arrayBuffer], { type: 'image/jpeg' }))
                .then(bitmap => {
                    if (this.isPlaying) {
                        this.ctx.clearRect(0, 0, this.canvas.width, this.canvas.height);
                        this.ctx.drawImage(bitmap, 0, 0, this.canvas.width, this.canvas.height);
                        this.updateCounters();
                        this.updateFrameInfoDisplay();
                    }
                    this.pendingFrames--;
                })
                .catch(error => {
                    console.error('Failed to decode image frame:', error);
                    this.pendingFrames--;
                    
                    // Fallback to regular image loading
                    this.fallbackProcessFrame(arrayBuffer);
                });
        } catch (error) {
            console.error('Error in optimized frame processing:', error);
            this.pendingFrames--;
            this.fallbackProcessFrame(arrayBuffer);
        }
    }

    fallbackProcessFrame(arrayBuffer) {
        try {
            const blob = new Blob([arrayBuffer], { type: 'image/jpeg' });
            const url = URL.createObjectURL(blob);
            
            const img = new Image();
            img.onload = () => {
                this.ctx.clearRect(0, 0, this.canvas.width, this.canvas.height);
                this.ctx.drawImage(img, 0, 0, this.canvas.width, this.canvas.height);
                URL.revokeObjectURL(url);
                this.updateCounters();
                this.updateFrameInfoDisplay();
            };
            img.onerror = () => {
                URL.revokeObjectURL(url);
            };
            img.src = url;
        } catch (error) {
            console.error('Error in fallback frame processing:', error);
        }
    }
}

// Initialize the player when the page loads
document.addEventListener('DOMContentLoaded', () => {
    // Use optimized version if createImageBitmap is supported
    if (typeof createImageBitmap === 'function') {
        window.mjpegPlayer = new MJPEGStreamPlayerOptimized();
    } else {
        window.mjpegPlayer = new MJPEGStreamPlayer();
    }
});

// Handle page visibility changes to pause/resume rendering
document.addEventListener('visibilitychange', () => {
    const player = window.mjpegPlayer;
    if (player) {
        player.isPlaying = !document.hidden;
        if (document.hidden) {
            // Clear canvas when page is hidden to save resources
            player.ctx.clearRect(0, 0, player.canvas.width, player.canvas.height);
            player.ctx.fillStyle = 'black';
            player.ctx.fillRect(0, 0, player.canvas.width, player.canvas.height);
            player.ctx.fillStyle = 'gray';
            player.ctx.font = '16px Arial';
            player.ctx.textAlign = 'center';
            player.ctx.fillText('Page is hidden - video paused', player.canvas.width / 2, player.canvas.height / 2);
        }
    }
});

// Handle page beforeunload to properly close connection
window.addEventListener('beforeunload', () => {
    const player = window.mjpegPlayer;
    if (player && player.ws) {
        player.ws.close(1000, 'Page unloading');
    }
});
    </script>
</body>
</html>
)";
