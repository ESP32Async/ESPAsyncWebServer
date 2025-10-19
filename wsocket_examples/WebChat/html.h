// this page provides minimalistic WebChat room
// disclaimer: this page and code was generated with the help of AI tools

static const char *htmlChatPage = R"(
<!DOCTYPE html>
<html lang="en">
<head>
  <meta charset="UTF-8" />
  <meta name="viewport" content="width=device-width, initial-scale=1.0"/>
  <title>AsyncWebSocket Chat example</title>
  <!-- disclaimer: this page and code was generated with the help of AI tools -->
  <style>
    body {
      margin: 0;
      font-family: Arial, sans-serif;
      background-color: #121212;
      color: #ffffff;
      display: flex;
      flex-direction: column;
      height: 100vh;
    }

    #messages {
      flex: 1;
      overflow-y: auto;
      padding: 20px;
      box-sizing: border-box;
      border-bottom: 1px solid #333;
      display: flex;
      flex-direction: column;
    }

    .message {
      max-width: 70%;
      margin-bottom: 10px;
      padding: 10px 14px;
      border-radius: 18px;
      line-height: 1.4;
      word-wrap: break-word;
      white-space: pre-wrap;
    }

    .sender {
      align-self: flex-end;
      background-color: #3f51b5;
      color: white;
      border-bottom-right-radius: 0;
    }

    .receiver {
      align-self: flex-start;
      background-color: #2a2a2a;
      color: #ffffff;
      border-bottom-left-radius: 0;
    }

    #inputArea {
      display: flex;
      padding: 10px 20px;
      box-sizing: border-box;
      background-color: #1e1e1e;
    }

    #replyInput {
      flex: 1;
      padding: 10px;
      border: none;
      border-radius: 4px;
      font-size: 14px;
      background-color: #2a2a2a;
      color: #fff;
    }

    #sendButton {
      margin-left: 10px;
      padding: 10px 16px;
      background-color: #3f51b5;
      color: white;
      border: none;
      border-radius: 4px;
      cursor: pointer;
      font-size: 14px;
    }

    #sendButton:hover {
      background-color: #5c6bc0;
    }

    /* Modal Styles */
    #usernameModal {
      position: fixed;
      top: 0;
      left: 0;
      width: 100vw;
      height: 100vh;
      background-color: rgba(0,0,0,0.8);
      display: flex;
      align-items: center;
      justify-content: center;
      z-index: 9999;
    }

    #usernameModalContent {
      background-color: #1e1e1e;
      padding: 30px;
      border-radius: 8px;
      text-align: center;
      box-shadow: 0 0 10px rgba(0,0,0,0.5);
    }

    #usernameInput {
      padding: 10px;
      width: 100%;
      max-width: 300px;
      font-size: 16px;
      border: none;
      border-radius: 4px;
      margin-top: 10px;
      background-color: #2a2a2a;
      color: white;
    }

    #usernameSubmit {
      margin-top: 15px;
      padding: 10px 20px;
      font-size: 14px;
      background-color: #3f51b5;
      color: white;
      border: none;
      border-radius: 4px;
      cursor: pointer;
    }

    #usernameSubmit:hover {
      background-color: #5c6bc0;
    }
  </style>
</head>
<body>
  <!-- Username Modal -->
  <div id="usernameModal">
    <div id="usernameModalContent">
      <h2>Enter your username</h2>
      <input type="text" id="usernameInput" placeholder="Your name" />
      <br />
      <button id="usernameSubmit">Start Chat</button>
    </div>
  </div>

  <!-- Chat Messages -->
  <div id="messages"></div>

  <!-- Input Field -->
  <div id="inputArea">
    <input type="text" id="replyInput" placeholder="Type your message..." disabled />
    <button id="sendButton" disabled>Send</button>
  </div>

  <script>
    let username = '';
    const socket = new WebSocket('ws://' + location.host + '/chat'); // Replace with your WebSocket server

    const messagesDiv = document.getElementById('messages');
    const replyInput = document.getElementById('replyInput');
    const sendButton = document.getElementById('sendButton');

    const usernameModal = document.getElementById('usernameModal');
    const usernameInput = document.getElementById('usernameInput');
    const usernameSubmit = document.getElementById('usernameSubmit');

    // Modal submission
    usernameSubmit.addEventListener('click', submitUsername);
    usernameInput.addEventListener('keydown', (e) => {
      if (e.key === 'Enter') submitUsername();
    });

    function submitUsername() {
      const name = usernameInput.value.trim();
      if (name !== '') {
        username = name;
        usernameModal.style.display = 'none';
        replyInput.disabled = false;
        sendButton.disabled = false;
        replyInput.focus();
      }
    }

    socket.addEventListener('open', () => {
      console.log('WebSocket connection opened');
    });

    socket.addEventListener('message', (event) => {
        console.log('msg in:', event.data);
        // If the server echoes back the same message we sent, ignore it
        if (event.data.startsWith(`${username}:`)) {
            return;
        }
      const { user, message } = parseMessage(event.data);
      appendMessage(user, message, 'receiver');
    });

    sendButton.addEventListener('click', sendMessage);
    replyInput.addEventListener('keydown', function (e) {
      if (e.key === 'Enter') {
        sendMessage();
      }
    });

    function sendMessage() {
      const message = replyInput.value.trim();
      if (message !== '' && socket.readyState === WebSocket.OPEN) {
        const fullMessage = `${username}: ${message}`;
        console.log('msg out:', fullMessage);
        socket.send(fullMessage);
        appendMessage(username, message, 'sender');
        replyInput.value = '';
      }
    }

    function appendMessage(user, message, type) {
      const msg = document.createElement('div');
      msg.className = `message ${type}`;
      msg.innerHTML = `<strong>${escapeHtml(user)}:</strong> ${escapeHtml(message)}`;
      messagesDiv.appendChild(msg);
      messagesDiv.scrollTop = messagesDiv.scrollHeight;
    }

    // Parse message as "Username: Message text"
    function parseMessage(raw) {
      const colonIndex = raw.indexOf(':');
      if (colonIndex > 0) {
        const user = raw.slice(0, colonIndex).trim();
        const message = raw.slice(colonIndex + 1).trim();
        return { user, message };
      } else {
        return { user: 'Server', message: raw };
      }
    }

    // Prevent XSS in messages
    function escapeHtml(text) {
      const div = document.createElement("div");
      div.innerText = text;
      return div.innerHTML;
    }
  </script>
</body>
</html>
)";
