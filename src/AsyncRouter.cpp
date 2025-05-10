#include "AsyncRouter.h"

AsyncRouter::AsyncRouter(AsyncWebServer* server, const char* path) {
    _server = server;
    _path = path;
}

AsyncRouter::~AsyncRouter() {
    for (AsyncCallbackWebHandler* handler : _handlers) {
        _server->removeHandler(handler);
        delete handler;
    }
}

AsyncCallbackWebHandler& AsyncRouter::on(const char* uri, ArRequestHandlerFunction onRequest) {
    return _server->on((_path + uri).c_str(), HTTP_ANY, onRequest);
}

AsyncCallbackWebHandler& AsyncRouter::on(const char* uri, WebRequestMethodComposite method, ArRequestHandlerFunction onRequest) {
    return _server->on((_path + uri).c_str(), method, onRequest);
}

AsyncCallbackWebHandler& AsyncRouter::on(
    const char* uri,
    WebRequestMethodComposite method,
    ArRequestHandlerFunction onRequest,
    ArUploadHandlerFunction onUpload,
    ArBodyHandlerFunction onBody) {
    AsyncCallbackWebHandler& handler = _server->on(
        (_path + uri).c_str(),
        method,
        onRequest,
        onUpload,
        onBody);
    _handlers.push_back(&handler);
    return handler;
};

void AsyncRouter::addMidleware(AsyncMiddleware* middleware) {
    for (AsyncCallbackWebHandler* handler : _handlers) {
        handler->addMiddleware(middleware);
    }
}

std::string AsyncRouter::path() {
    return _path;
}