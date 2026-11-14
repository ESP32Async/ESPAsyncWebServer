# EventSource

## Async Event Source Plugin

The server includes EventSource (Server-Sent Events) plugin which can be used to send short text events to the browser.
Difference between EventSource and WebSockets is that EventSource is single direction, text-only protocol.

See the [ServerSentEvents example here](https://github.com/ESP32Async/ESPAsyncWebServer/blob/master/examples/arduino/ServerSentEvents/ServerSentEvents.ino).

### Setup Event Source on the server

```cpp
AsyncWebServer server(80);
AsyncEventSource events("/events");

void setup(){
  // setup ......
  events.onConnect([](AsyncEventSourceClient *client){
    if(client->lastId()){
      Serial.printf("Client reconnected! Last message ID that it got is: %u\n", client->lastId());
    }
    //send event with message "hello!", id current millis
    // and set reconnect delay to 1 second
    client->send("hello!",NULL,millis(),1000);
  });

  server.addHandler(&events);
  // setup ......
}

void loop(){
  if(eventTriggered){ // your logic here
    //send event "myevent"
    events.send("my event content","myevent",millis());
  }
}
```

**IMPORTANT**: Use `AsyncAuthenticationMiddleware` instead of the deprecated `setAuthentication()` method for authentication.

```cpp
AsyncAuthenticationMiddleware authMiddleware;
authMiddleware.setAuthType(AsyncAuthType::AUTH_DIGEST);
authMiddleware.setRealm("My app name");
authMiddleware.setUsername("admin");
authMiddleware.setPassword("admin");
authMiddleware.generateHash();

events.addMiddleware(&authMiddleware);
```

### Track and close a single client

Each SSE client is exposed as a shared handle (`AsyncEventSourceClientPtr`, a `std::shared_ptr`). A handle may be stored past the end of any callback: it keeps the client object alive until the last handle is dropped, and a client that disconnects simply reports `connected() == false` instead of becoming a dangling pointer.

The request-aware connect callback lets you associate the connection with request-scoped state (such as the authenticated user) while the request is still alive:

```cpp
std::map<String, std::weak_ptr<AsyncEventSourceClient>> sessions;  // app-side map

events.onConnectWithRequest([](AsyncWebServerRequest *request, const AsyncEventSourceClientPtr &client) {
  // read authentication/session state from request here
  String user = getUserFrom(request);
  // store a weak reference: an expired session must not keep a dead client object alive
  sessions[user] = client;
});

// later, from your normal service context (e.g. session expiry):
if (auto client = sessions["bob"].lock()) {
  client->close();  // closes ONLY this connection, other clients stay connected
}

// send a message to every connected client (safe snapshot):
for (const auto &client : events.getClients()) {
  if (client->connected()) {
    client->send("update", "state");
  }
}
```

**Note**: if you access such app-side containers from several tasks, guard them with a mutex of your own — the connect/disconnect callbacks run on the AsyncTCP task.

### Ownership-safe callbacks

Besides the classic `onConnect` / `onDisconnect` callbacks (raw pointer, valid only inside the callback), ownership-safe variants deliver the shared handle and are safe to store:

```cpp
events.onConnectSafe([](const AsyncEventSourceClientPtr &client) {
  // client->send(...) works as usual, and the handle may be retained
});

events.onDisconnectSafe([](const AsyncEventSourceClientPtr &client) {
  // safe to call methods: the object is kept alive by the handle
  Serial.printf("SSE client gone, lastId=%u\n", client->lastId());
});
```

Each connect/disconnect event has a single callback slot shared by the classic and the safe setter: registering one overwrites the other (the last registered wins). Pick one flavor per event.

`shared_from_this()` (inherited from `std::enable_shared_from_this`) provides the same handle from a client pointer you already hold, e.g. inside the classic `onConnect` callback — do not call it from the client constructor.

### Setup Event Source in the browser

```javascript
if (!!window.EventSource) {
  var source = new EventSource("/events");

  source.addEventListener(
    "open",
    function (e) {
      console.log("Events Connected");
    },
    false,
  );

  source.addEventListener(
    "error",
    function (e) {
      if (e.target.readyState != EventSource.OPEN) {
        console.log("Events Disconnected");
      }
    },
    false,
  );

  source.addEventListener(
    "message",
    function (e) {
      console.log("message", e.data);
    },
    false,
  );

  source.addEventListener(
    "myevent",
    function (e) {
      console.log("myevent", e.data);
    },
    false,
  );
}
```
