#ifndef ASYNC_ROUTER_H_
#define ASYNC_ROUTER_H_

#include <Arduino.h>
#include <ESPAsyncWebServer.h>
#include <string>

class AsyncMiddleware;

typedef std::function<void(AsyncWebServerRequest *request)> ArRequestHandlerFunction;
typedef std::function<void(AsyncWebServerRequest *request, const String &filename, size_t index, uint8_t *data, size_t len, bool final)>
  ArUploadHandlerFunction;
typedef std::function<void(AsyncWebServerRequest *request, uint8_t *data, size_t len, size_t index, size_t total)> ArBodyHandlerFunction;
typedef uint8_t WebRequestMethodComposite;

class AsyncRouter {
    public:
        AsyncRouter(AsyncWebServer* server, const char *path);
        ~AsyncRouter();
        AsyncCallbackWebHandler& on(
            const char* uri,
            WebRequestMethodComposite method,
            ArRequestHandlerFunction onRequest,
            ArUploadHandlerFunction onUpload = nullptr,
            ArBodyHandlerFunction onBody = nullptr
        );
        AsyncCallbackWebHandler& on(const char* uri, ArRequestHandlerFunction onRequest);
        AsyncCallbackWebHandler& on(const char* uri, WebRequestMethod method, ArRequestHandlerFunction onRequest);

        void addMidleware(AsyncMiddleware* middleware);
        std::string path();
    private:
        std::string _path;
        AsyncWebServer* _server;
        std::list<AsyncCallbackWebHandler*> _handlers;
};

#endif 