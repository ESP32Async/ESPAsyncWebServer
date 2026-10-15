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

## Multiple URIs per Handler

A handler can serve **multiple URIs** by holding a list of matchers instead of a single one:

| Method | Effect |
|---|---|
| `handler.setUri(matcher)` | Replace the list with one matcher (backward-compatible) |
| `handler.addUri(matcher)` | Append an additional URI matcher |
| `handler.clearUris()` | Remove all URI matchers |

One HTTP method mask (`setMethod()`) applies to **all** URIs of a handler. When different URIs need different methods (e.g. `GET` vs `POST` with body/upload callbacks), use separate handler objects.

### One Handler, Multiple URIs

```cpp
static AsyncWebServer server(80);

// One handler object — lives in static RAM
static AsyncCallbackWebHandler greetHandler;

void setup() {
  greetHandler.onRequest([](AsyncWebServerRequest *request) {
    request->send(200, "text/plain", "Hello from " + request->url());
  });
  greetHandler.setMethod(HTTP_GET);

  // Same handler object serving three different URIs:
  greetHandler.setUri(AsyncURIMatcher::exact("/hello"));
  greetHandler.addUri(AsyncURIMatcher::exact("/hi"));
  greetHandler.addUri(AsyncURIMatcher::prefix("/greet/"));

  // Non-owning registration (by reference) — handler lives in static RAM:
  server.addHandler(greetHandler);

  server.begin();
}
```

### Different Methods Need Different Handlers

The method mask applies to all URIs of a handler, so a `POST` endpoint gets its own handler:

```cpp
static AsyncCallbackWebHandler getHandler;
getHandler.setMethod(HTTP_GET);
getHandler.setUri(AsyncURIMatcher::exact("/data"));
getHandler.onRequest([](AsyncWebServerRequest *request) {
  request->send(200, "text/plain", "GET response");
});
server.addHandler(getHandler);

static AsyncCallbackWebHandler postHandler;
postHandler.setMethod(HTTP_POST);
postHandler.setUri(AsyncURIMatcher::exact("/data"));
postHandler.onRequest([](AsyncWebServerRequest *request) {
  request->send(200, "text/plain", "POST received");
});
server.addHandler(postHandler);
```

### PSRAM Handler on Multiple URIs (ESP32)

```cpp
#include <esp_heap_caps.h>

void setup() {
  void *mem = heap_caps_malloc(sizeof(AsyncCallbackWebHandler), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  auto *handler = new (mem) AsyncCallbackWebHandler();
  handler->onRequest([](AsyncWebServerRequest *request) {
    request->send(200, "application/json", "{\"ok\":true}");
  });
  handler->setMethod(HTTP_GET);

  // Same PSRAM handler serving two URIs — non-owning (borrow by reference):
  handler->setUri(AsyncURIMatcher::prefix("/api/"));
  handler->addUri(AsyncURIMatcher::exact("/status"));
  server.addHandler(*handler);

  server.begin();
}
```

### Removing a Handler

```cpp
// Removes the handler entirely — it no longer matches any URI:
server.removeHandler(&greetHandler);
```

> [!NOTE]
> This is fully backward compatible. Existing code using `on()`, `addHandler()`, `serveStatic()`, and single-URI handlers (`setUri()`) works identically — no migration required.

See the [MultiRoute example](https://github.com/ESP32Async/ESPAsyncWebServer/blob/master/examples/arduino/MultiRoute/MultiRoute.ino) for a complete sketch.

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
| `server.addHandler(myHandler)` | Non-owning (borrow) | Static or global object (see warning below about stack) |
| `server.addHandler(*psramHandler)` | Non-owning (borrow) | PSRAM via placement new |

With non-owning registration, **you** are responsible for keeping the handler alive while it is registered, and for destroying it after unregistration.

> [!TIP]
> For objects that live for the entire lifetime of the application (like handlers), static or global allocation is preferable over heap allocation. It results in simpler code, avoids heap fragmentation, and requires no lifetime tracking overhead.

> [!WARNING]
> Borrowing a **stack-allocated** object is risky unless you have architected your program to keep it in scope for as long as the server uses it. The usual Arduino-style `setup()` / `loop()` pattern does **not** guarantee this — a handler declared as a local variable inside `setup()` would be destroyed when `setup()` returns, leaving the server with a dangling pointer. Use `static` or global storage instead.

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

There are two ways to place a handler in PSRAM:

**1. Placement new with `heap_caps_malloc`:**

```cpp
#include <esp_heap_caps.h>

void setup() {
  auto *handler = new (heap_caps_malloc(sizeof(AsyncCallbackWebHandler), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT)) AsyncCallbackWebHandler();
  handler->setUri(AsyncURIMatcher::exact("/status"));
  handler->onRequest([](AsyncWebServerRequest *request) {
    request->send(200, "application/json", "{\"ok\":true}");
  });
  server.addHandler(*handler);  // non-owning borrow — manage PSRAM lifetime yourself
  server.begin();
}
```

**2. Static object in PSRAM via `EXT_RAM_BSS_ATTR`** (requires `CONFIG_SPIRAM_ALLOW_BSS_SEG_EXTERNAL_MEMORY=y`, typically only available when using Arduino as an ESP-IDF component):

```cpp
EXT_RAM_BSS_ATTR static AsyncCallbackWebHandler statusHandler;

void setup() {
  statusHandler.setUri(AsyncURIMatcher::exact("/status"));
  statusHandler.onRequest([](AsyncWebServerRequest *request) {
    request->send(200, "application/json", "{\"ok\":true}");
  });
  server.addHandler(statusHandler);  // non-owning — handler lives in PSRAM .bss
  server.begin();
}
```

> [!NOTE]
> On ESP32, `delete` works on objects allocated with `heap_caps_malloc()` because `delete` maps to `free()` which maps to `heap_caps_free()`. This is specific to Espressif's implementation (GNU libstdc++ + ESP-IDF libc) and is not guaranteed on all platforms.

### Works with All Handler Types

The same registration pattern applies to any `AsyncWebHandler` subclass: `AsyncCallbackWebHandler`, `AsyncStaticWebHandler`, `AsyncWebSocket`, `AsyncEventSource`, `AsyncCallbackJsonWebHandler`, or your own custom subclass.

```cpp
static AsyncWebSocket ws("/ws");
ws.onEvent([](AsyncWebSocket *s, AsyncWebSocketClient *c, AwsEventType t, void *a, uint8_t *d, size_t l) {
  if (t == WS_EVT_DATA) s->text(c->id(), "echo");
});
server.addHandler(ws);  // non-owning — ws lives in static RAM
```

### Custom Catch-All Handler

A catch-all (not-found) handler is **always installed**. It can be customized in two ways:

**At construction time** — pass your handler as a second argument to the `AsyncWebServer` constructor:

| Constructor | Ownership |
|---|---|
| `AsyncWebServer server(80)` | Server creates & owns a default catch-all |
| `AsyncWebServer server(80, new AsyncCallbackWebHandler())` | Server owns |
| `AsyncWebServer server(80, myHandler)` | Non-owning (borrow by reference) |
| `AsyncWebServer server(80, *psramHandler)` | Non-owning (borrow from PSRAM) |

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
| `server.setCatchAllHandler(new AsyncCallbackWebHandler())` | Server owns |
| `server.setCatchAllHandler(myHandler)` | Non-owning (borrow by reference) |
| `server.setCatchAllHandler(*psramHandler)` | Non-owning (borrow from PSRAM) |

```cpp
server.setCatchAllHandler(catchAll);  // non-owning — catchAll lives in static RAM
```

> [!NOTE]
> A catch-all handler is always required — passing `nullptr` to the constructor or `setCatchAllHandler()` is a programming error (asserts in debug builds). `catchAllHandler()` safely returns a reference to the installed catch-all.

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
