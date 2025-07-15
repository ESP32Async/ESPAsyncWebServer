#include <ESPAsyncWebServer.h>

/**
 * @brief Sends a file from the filesystem to the client, with optional gzip compression and ETag-based caching.
 *
 * This method serves files over HTTP from the provided filesystem. If a compressed version of the file
 * (with a `.gz` extension) exists and uncompressed version does not exist, it serves the compressed file.
 * It also handles ETag caching using the CRC32 value from the gzip trailer, responding with `304 Not Modified`
 * if the client's `If-None-Match` header matches the generated ETag.
 *
 * @param fs Reference to the filesystem (SPIFFS, LittleFS, etc.).
 * @param path Path to the file to be served.
 * @param contentType Optional MIME type of the file to be sent.
 *                    If contentType is "" it will be obtained from the file extension
 * @param download If true, forces the file to be sent as a download.
 * @param callback Optional template processor for dynamic content generation.
 *                 Templates will not be processed in compressed files.
 *
 * @note If neither the file nor its compressed version exists, responds with `404 Not Found`.
 */
void AsyncWebServerRequest::send(FS &fs, const String &path, const char *contentType, bool download, AwsTemplateProcessor callback) {
  constexpr size_t MAX_PATH = 128;
  const size_t GZ_SUFFIX_LEN = 4; // ".gz\0"

  // Try to open the uncompressed version first
  File content = fs.open(path, fs::FileOpenMode::read);
  if (content.available()) {
    send(beginResponse(content, path, contentType, download, callback));
    return;
  }

  // Handle compressed version
  size_t pathLen = path.length();
  
  // Prevent buffer overflow
  if (pathLen > (MAX_PATH - GZ_SUFFIX_LEN)) {
    send(404);
    return;
  }
  
  char gzPath[MAX_PATH]; // Safe buffer size for ESP32 paths
  memcpy(gzPath, path.c_str(), pathLen);
  memcpy(gzPath + pathLen, asyncsrv::T__gz, GZ_SUFFIX_LEN);
  content = fs.open(gzPath, fs::FileOpenMode::read);

  // Compressed file not found or invalid
  if (!content.seek(content.size() - 8)) {
    content.close();
    send(404);
    return;
  }

  // ETag validation
  if (this->hasHeader(asyncsrv::T_INM)) {
    // Generate server ETag from CRC in gzip trailer
    uint8_t crcInTrailer[4];
    content.read(crcInTrailer, 4);
    char serverETag[9];
    _getEtag(crcInTrailer, serverETag);

    // Compare with client's ETag
    const AsyncWebHeader *inmHeader = this->getHeader(asyncsrv::T_INM);
    if (inmHeader && inmHeader->value() == serverETag) {
      content.close();
      this->send(304);  // Not Modified
      return;
    }
  }

  // Send compressed file response
  send(beginResponse(content, path, contentType, download, callback));
}

/**
 * @brief Generates an ETag string from a 4-byte trailer
 *
 * This function converts a 4-byte array into a hexadecimal ETag string enclosed in quotes.
 *
 * @param trailer[4] Input array of 4 bytes to convert to hexadecimal
 * @param serverETag Output buffer to store the ETag
 *                   Must be pre-allocated with minimum 9 bytes (8 hex + 1 null terminator)
 */
void AsyncWebServerRequest::_getEtag(uint8_t trailer[4], char *serverETag) {
  static constexpr char hexChars[] = "0123456789ABCDEF";

  uint32_t data;
  memcpy(&data, trailer, 4);

  serverETag[0] = hexChars[(data >> 4) & 0x0F];
  serverETag[1] = hexChars[data & 0x0F];
  serverETag[2] = hexChars[(data >> 12) & 0x0F];
  serverETag[3] = hexChars[(data >> 8) & 0x0F];
  serverETag[4] = hexChars[(data >> 20) & 0x0F];
  serverETag[5] = hexChars[(data >> 16) & 0x0F];
  serverETag[6] = hexChars[(data >> 28)];
  serverETag[7] = hexChars[(data >> 24) & 0x0F];
  serverETag[8] = '\0';
}
