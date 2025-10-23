# AsyncURIMatcher Example

This example demonstrates the comprehensive URI matching capabilities of the ESPAsyncWebServer library using the `AsyncURIMatcher` class.

## Overview

The `AsyncURIMatcher` class provides flexible and powerful URL routing mechanisms that go beyond simple string matching. It supports various matching strategies that can be combined to create sophisticated routing rules.

**Important**: When using plain strings (not `AsyncURIMatcher` objects), the library uses auto-detection (`URIMatchAuto`) which analyzes the URI pattern and applies appropriate matching rules. This is **not** simple exact matching - it combines exact and folder matching by default!

## Auto-Detection Behavior

When you pass a plain string or `const char*` to `server.on()`, the `URIMatchAuto` flag is used, which:

1. **Empty URI**: Matches everything
2. **Ends with `*`**: Becomes prefix match (`URIMatchPrefix`)
3. **Contains `/*.ext`**: Becomes extension match (`URIMatchExtension`)
4. **Starts with `^` and ends with `$`**: Becomes regex match (if enabled)
5. **Everything else**: Becomes **both** exact and folder match (`URIMatchPrefixFolder | URIMatchExact`)

This means traditional string-based routes like `server.on("/path", handler)` will match:
- `/path` (exact match)
- `/path/anything` (folder match)

But will **NOT** match `/path-suffix` (prefix without folder separator).

## Features Demonstrated

### 1. **Auto-Detection (Traditional Behavior)**
- When using plain strings, automatically detects the appropriate matching strategy
- Default behavior: combines exact and folder matching
- Special patterns: `*` suffix → prefix, `/*.ext` → extension, regex patterns
- Use cases: Traditional ESP32 web server behavior with enhanced capabilities
- Examples: `/path` matches both `/path` and `/path/sub`

### 2. **Exact Matching**
- Matches only the exact URL specified
- Use cases: API endpoints, specific pages
- Examples: `/exact`, `/login`, `/dashboard`

### 3. **Prefix Matching**
- Matches URLs that start with the specified pattern
- Use cases: API versioning, module grouping
- Examples: `/api*` matches `/api/users`, `/api/posts`, etc.

### 4. **Folder/Directory Matching**
- Matches URLs under a specific directory path
- Use cases: Admin sections, organized content
- Examples: `/admin/` matches `/admin/users`, `/admin/settings`

### 5. **Extension Matching**
- Matches files with specific extensions under a path
- Use cases: Static file serving, file type handlers
- Examples: `/images/*.jpg` matches any `.jpg` file under `/images/`

### 6. **Case Insensitive Matching**
- Matches URLs regardless of character case
- Use cases: User-friendly URLs, legacy support
- Examples: `/CaSe` matches `/case`, `/CASE`, etc.

### 7. **Regular Expression Matching**
- Advanced pattern matching using regex (requires `ASYNCWEBSERVER_REGEX`)
- Use cases: Complex URL patterns, parameter extraction
- Examples: `/user/([0-9]+)` matches `/user/123` and captures the ID

### 8. **Combined Flags**
- Multiple matching strategies can be combined
- Use cases: Flexible routing requirements
- Examples: Case-insensitive prefix matching

## Usage Patterns

### Traditional String-based Routing (Auto-Detection)
```cpp
// Auto-detection with exact + folder matching
server.on("/api", handler);          // Matches /api AND /api/anything
server.on("/login", handler);        // Matches /login AND /login/sub

// Auto-detection with prefix matching  
server.on("/prefix*", handler);      // Matches /prefix, /prefix-test, /prefix/sub

// Auto-detection with extension matching
server.on("/images/*.jpg", handler); // Matches /images/pic.jpg, /images/sub/pic.jpg
```

### Explicit AsyncURIMatcher Syntax
### Explicit AsyncURIMatcher Syntax
```cpp
// Exact matching only
server.on(AsyncURIMatcher("/path", URIMatchExact), handler);

// Prefix matching only
server.on(AsyncURIMatcher("/api", URIMatchPrefix), handler);

// Combined flags
server.on(AsyncURIMatcher("/api", URIMatchPrefix | URIMatchCaseInsensitive), handler);
```

### Factory Functions
```cpp
// More readable and expressive
server.on(AsyncURIMatcher::exact("/login"), handler);
server.on(AsyncURIMatcher::prefix("/api"), handler);
server.on(AsyncURIMatcher::dir("/admin"), handler);
server.on(AsyncURIMatcher::ext("/images/*.jpg"), handler);
server.on(AsyncURIMatcher::iExact("/case"), handler);

#ifdef ASYNCWEBSERVER_REGEX
server.on(AsyncURIMatcher::regex("^/user/([0-9]+)$"), handler);
#endif
```

## Available Flags

| Flag | Description |
|------|-------------|
| `URIMatchAuto` | Auto-detect match type from pattern (default) |
| `URIMatchExact` | Exact URL match |
| `URIMatchPrefix` | Prefix match |
| `URIMatchPrefixFolder` | Folder prefix match (requires trailing /) |
| `URIMatchExtension` | File extension match pattern |
| `URIMatchCaseInsensitive` | Case insensitive matching |
| `URIMatchRegex` | Regular expression matching (requires ASYNCWEBSERVER_REGEX) |

## Testing the Example

1. **Upload the sketch** to your ESP32/ESP8266
2. **Connect to WiFi AP**: `esp-captive` (no password required)
3. **Navigate to**: `http://192.168.4.1/`
4. **Explore the examples** by clicking the organized test links
5. **Monitor Serial output**: Open Serial Monitor to see detailed debugging information for each matched route

### Test URLs Available (All Clickable from Homepage)

**Auto-Detection Examples:**
- `http://192.168.4.1/auto` (exact + folder match)
- `http://192.168.4.1/auto/sub` (folder match - same handler!)
- `http://192.168.4.1/wildcard-test` (auto-detected prefix)
- `http://192.168.4.1/auto-images/photo.png` (auto-detected extension)

**Factory Method Examples:**
- `http://192.168.4.1/exact` (AsyncURIMatcher::exact)
- `http://192.168.4.1/service/status` (AsyncURIMatcher::prefix)
- `http://192.168.4.1/admin/users` (AsyncURIMatcher::dir)
- `http://192.168.4.1/images/photo.jpg` (AsyncURIMatcher::ext)

**Case Insensitive Examples:**
- `http://192.168.4.1/case` (lowercase)
- `http://192.168.4.1/CASE` (uppercase)
- `http://192.168.4.1/CaSe` (mixed case)

**Regex Examples (if ASYNCWEBSERVER_REGEX enabled):**
- `http://192.168.4.1/user/123` (captures numeric ID)
- `http://192.168.4.1/user/456` (captures numeric ID)

**Combined Flags Examples:**
- `http://192.168.4.1/mixedcase-test` (prefix + case insensitive)
- `http://192.168.4.1/MIXEDCASE/sub` (prefix + case insensitive)

### Console Output

Each handler provides detailed debugging information via Serial output:
```
Auto-Detection Match (Traditional)
Matched URL: /auto
Uses auto-detection: exact + folder matching
```

```
Factory Exact Match  
Matched URL: /exact
Uses AsyncURIMatcher::exact() factory function
```

```
Regex Match - User ID
Matched URL: /user/123
Captured User ID: 123
This regex matches /user/{number} pattern
```

## Compilation Options

### Enable Regex Support
To enable regular expression matching, compile with:
```
-D ASYNCWEBSERVER_REGEX
```

In PlatformIO, add to `platformio.ini`:
```ini
build_flags = -D ASYNCWEBSERVER_REGEX
```

In Arduino IDE, add to your sketch:
```cpp
#define ASYNCWEBSERVER_REGEX
```

## Performance Considerations

1. **Exact matches** are fastest
2. **Prefix matches** are very efficient  
3. **Regex matches** are slower but most flexible
4. **Case insensitive** matching adds minimal overhead
5. **Auto-detection** adds slight parsing overhead at construction time

## Real-World Applications

### REST API Design
```cpp
// API versioning
server.on(AsyncURIMatcher::prefix("/api/v1"), handleAPIv1);
server.on(AsyncURIMatcher::prefix("/api/v2"), handleAPIv2);

// Resource endpoints with IDs
server.on(AsyncURIMatcher::regex("^/api/users/([0-9]+)$"), handleUserById);
server.on(AsyncURIMatcher::regex("^/api/posts/([0-9]+)/comments$"), handlePostComments);
```

### File Serving
```cpp
// Serve different file types
server.on(AsyncURIMatcher::ext("/assets/*.css"), serveCSSFiles);
server.on(AsyncURIMatcher::ext("/assets/*.js"), serveJSFiles);
server.on(AsyncURIMatcher::ext("/images/*.jpg"), serveImageFiles);
```

### Admin Interface
```cpp
// Admin section with authentication
server.on(AsyncURIMatcher::dir("/admin"), handleAdminPages);
server.on(AsyncURIMatcher::exact("/admin"), redirectToAdminDashboard);
```

## See Also

- [ESPAsyncWebServer Documentation](https://github.com/ESP32Async/ESPAsyncWebServer)
- [Regular Expression Reference](https://en.cppreference.com/w/cpp/regex)
- Other examples in the `examples/` directory
