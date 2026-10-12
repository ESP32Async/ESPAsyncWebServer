# Routing

## URL Matching and Routing

The `AsyncURIMatcher` class provides flexible and powerful URL routing mechanisms.

**Important**: When using plain strings (not `AsyncURIMatcher` objects), the library uses auto-detection (`URIMatchAuto`) which analyzes the URI pattern and applies appropriate matching rules. This is **not** simple exact matching - it combines exact and folder matching by default!

### Auto-Detection Behavior

When you pass a plain string or `const char*` to `server.on()`, the `URIMatchAuto` flag is used, which:

1. **Empty URI**: Matches everything
2. **Ends with `*`**: Becomes prefix match (`URIMatchPrefix`)
3. **Contains `/*.ext`**: Becomes extension match (`URIMatchExtension`)
4. **Starts with `^` and ends with `$`**: Becomes regex match (if regex enabled)
5. **Everything else**: Becomes **both** exact and folder match (`URIMatchPrefixFolder | URIMatchExact`)

This means traditional string-based routes like `server.on("/path", handler)` will match:

- `/path` (exact match)
- `/path/` (folder with trailing slash)
- `/path/anything` (folder match)

But will **NOT** match `/path-suffix` (prefix without folder separator).

### Usage Patterns

#### Traditional String-based Routing (Auto-Detection)

```cpp
// Auto-detection with exact + folder matching
server.on("/api", handler);          // Matches /api AND /api/anything
server.on("/login", handler);        // Matches /login AND /login/sub

// Auto-detection with prefix matching
server.on("/prefix*", handler);      // Matches /prefix, /prefix-test, /prefix/sub

// Auto-detection with extension matching
server.on("/images/*.jpg", handler); // Matches /images/pic.jpg, /images/sub/pic.jpg
```

#### Factory Functions

Using factory functions is clearer and gives you explicit control over matching behavior.

```cpp
// Exact matching only (matches "/login" but NOT "/login/sub")
server.on(AsyncURIMatcher::exact("/login"), handler);

// Prefix matching (matches "/api", "/api/v1", "/api-test")
server.on(AsyncURIMatcher::prefix("/api"), handler);

// Directory matching (matches "/admin/users" but NOT "/admin")
// Requires trailing slash in URL conceptually
server.on(AsyncURIMatcher::dir("/admin"), handler);

// Extension matching
server.on(AsyncURIMatcher::ext("/images/*.jpg"), handler);

// Case insensitive matching
server.on(AsyncURIMatcher::exact("/case", AsyncURIMatcher::CaseInsensitive), handler);
// matches "/case", "/CASE", "/CaSe"

// Regular Expression (requires -D ASYNCWEBSERVER_REGEX)
#ifdef ASYNCWEBSERVER_REGEX
server.on(AsyncURIMatcher::regex("^/user/([0-9]+)$"), handler);
#endif
```

## Param Rewrite With Matching

It is possible to rewrite the request url with parameter match. Here is an example with one parameter:
Rewrite for example "/radio/{frequence}" -> "/radio?f={frequence}"

```cpp
class OneParamRewrite : public AsyncWebRewrite
{
  protected:
    String _urlPrefix;
    int _paramIndex;
    String _paramsBackup;

  public:
  OneParamRewrite(const char* from, const char* to)
    : AsyncWebRewrite(from, to) {

      _paramIndex = _from.indexOf('{');

      if( _paramIndex >=0 && _from.endsWith("}")) {
        _urlPrefix = _from.substring(0, _paramIndex);
        int index = _params.indexOf('{');
        if(index >= 0) {
          _params = _params.substring(0, index);
        }
      } else {
        _urlPrefix = _from;
      }
      _paramsBackup = _params;
  }

  bool match(AsyncWebServerRequest *request) override {
    if(request->url().startsWith(_urlPrefix)) {
      if(_paramIndex >= 0) {
        _params = _paramsBackup + request->url().substring(_paramIndex);
      } else {
        _params = _paramsBackup;
      }
    return true;

    } else {
      return false;
    }
  }
};
```

Usage:

```cpp
  server.addRewrite( new OneParamRewrite("/radio/{frequence}", "/radio?f={frequence}") );
```

See the [Rewrite example here](https://github.com/ESP32Async/ESPAsyncWebServer/blob/master/examples/arduino/Rewrite/Rewrite.ino).

## Handler Ownership and Memory Placement

By default, handlers created by `server.on()`, `server.serveStatic()`, or passed to `server.addHandler(new MyHandler())` are **owned by the server** — it deletes them on `removeHandler()`, `reset()`, or destruction.

When you need to control **where** a handler object lives in memory (e.g. to use PSRAM on ESP32, or to avoid heap allocation entirely), register it as **non-owning**:

| Registration | Ownership | Use case |
|---|---|---|
| `server.addHandler(new MyHandler())` | Server owns | Default, backward compatible |
| `server.addHandler(ptr, false)` | Non-owning (pointer) | Heap or PSRAM via placement new |
| `server.addHandler(myHandler)` | Non-owning (reference) | Stack, static, or global object |

With non-owning registration, **you** are responsible for keeping the handler alive while it is registered, and for destroying it after unregistration.

### Static / Global RAM (no heap allocation)

```cpp
static AsyncWebServer server(80);

// Lives in .bss — zero heap usage
static AsyncCallbackWebHandler statusHandler;

void setup() {
  statusHandler.setUri(AsyncURIMatcher::exact("/status"));
  statusHandler.onRequest([](AsyncWebServerRequest *request) {
    request->send(200, "application/json", "{\"ok\":true}");
  });
  server.addHandler(statusHandler);  // non-owning reference overload
  server.begin();
}
```

### PSRAM (ESP32)

```cpp
#include <esp_heap_caps.h>

void setup() {
  void *mem = heap_caps_malloc(sizeof(AsyncCallbackWebHandler), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  auto *handler = new (mem) AsyncCallbackWebHandler();
  handler->setUri(AsyncURIMatcher::exact("/status"));
  handler->onRequest([](AsyncWebServerRequest *request) {
    request->send(200, "application/json", "{\"ok\":true}");
  });
  server.addHandler(handler, false);  // non-owning — manage PSRAM lifetime yourself
  server.begin();
}
```

### WebSocket / EventSource / Static File Handlers

The same pattern works for any `AsyncWebHandler` subclass — `AsyncWebSocket`, `AsyncEventSource`, `AsyncStaticWebHandler`, `AsyncCallbackJsonWebHandler`:

```cpp
static AsyncWebSocket ws("/ws");
ws.onEvent([](AsyncWebSocket *s, AsyncWebSocketClient *c, AwsEventType t, void *a, uint8_t *d, size_t l) {
  if (t == WS_EVT_DATA) s->text(c->id(), "echo");
});
server.addHandler(ws);  // non-owning — ws lives in static RAM

static AsyncStaticWebHandler staticHandler("/static", LittleFS, "/www");
server.addHandler(staticHandler);  // non-owning
```

### Custom Catch-All Handler

The catch-all (not-found) handler can be customized in two ways:

**At construction time** — pass your handler as a second argument to the `AsyncWebServer` constructor:

| Constructor | Ownership |
|---|---|
| `AsyncWebServer server(80)` | Server creates & owns a default catch-all |
| `AsyncWebServer server(80, new AsyncCallbackWebHandler())` | Server owns |
| `AsyncWebServer server(80, ptr, false)` | Non-owning (pointer) |
| `AsyncWebServer server(80, myHandler)` | Non-owning (reference) |
| `AsyncWebServer server(80, nullptr)` | **No catch-all** — unmatched requests get a bare 501 |

```cpp
static AsyncCallbackWebHandler catchAll;

void setup() {
  catchAll.onRequest([](AsyncWebServerRequest *request) {
    request->send(404, "text/plain", "Not found: " + request->url());
  });
  // Pass custom catch-all at construction — non-owning, lives in static RAM
  AsyncWebServer server(80, catchAll);
  server.begin();
}
```

**After construction** — replace it with `setCatchAllHandler()`:

| Registration | Ownership |
|---|---|
| `server.setCatchAllHandler(new AsyncCallbackWebHandler())` | Server owns (default) |
| `server.setCatchAllHandler(ptr, false)` | Non-owning (pointer) |
| `server.setCatchAllHandler(myHandler)` | Non-owning (reference) |
| `server.setCatchAllHandler(nullptr)` | **Remove catch-all** — unmatched requests get a bare 501 |

```cpp
server.setCatchAllHandler(catchAll);  // non-owning — catchAll lives in static RAM
```

> [!NOTE]
> When no catch-all handler is installed (`nullptr`), `onNotFound()`, `onFileUpload()`, `onRequestBody()`, and `reset()` are no-ops. Use `catchAllHandlerPtr()` to safely check for the catch-all handler — it returns `nullptr` instead of crashing. Unmatched requests receive a bare `501 Not Implemented` response with the message "Handler did not handle the request".
>
> The older `catchAllHandler()` method is deprecated — it crashes if no catch-all is installed. Replace it with:
> ```cpp
> if (auto *catchAll = server.catchAllHandlerPtr()) {
>   catchAll->addMiddleware(myMiddleware);
> }
> ```

See the [CustomAllocation example](https://github.com/ESP32Async/ESPAsyncWebServer/blob/master/examples/arduino/CustomAllocation/CustomAllocation.ino) for a complete sketch.

## Remove handlers and rewrites

Server goes through handlers in same order as they were added. You can't simple add handler with same path to override them.
To remove handler:

```arduino
// save callback for particular URL path
auto& handler = server.on("/some/path", [](AsyncWebServerRequest *request){
  //do something useful
});
// when you don't need handler anymore remove it
server.removeHandler(&handler);

// same with rewrites
server.removeRewrite(&someRewrite);

server.onNotFound([](AsyncWebServerRequest *request){
  request->send(404);
});

// remove server.onNotFound handler
server.onNotFound(NULL);

// remove all rewrites, handlers and onNotFound/onFileUpload/onRequestBody callbacks
server.reset();
```
