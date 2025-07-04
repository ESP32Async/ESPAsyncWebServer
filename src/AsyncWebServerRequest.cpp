#include <ESPAsyncWebServer.h>

/**
 * @brief Sends a file from the filesystem to the client, with optional gzip compression and ETag-based caching.
 *
 * This method serves files over HTTP from the provided filesystem. If a compressed version of the file
 * (with a `.gz` extension) exists and the `download` flag is not set, it serves the compressed file.
 * It also handles ETag caching using the CRC32 value from the gzip trailer, responding with `304 Not Modified`
 * if the client's `If-None-Match` header matches the generated ETag.
 *
 * @param fs Reference to the filesystem (SPIFFS, LittleFS, etc.).
 * @param path Path to the file to be served.
 * @param contentType Optional MIME type of the file to be sent.
 *                    If contentType is "" it will be obtained from the file extension
 * @param download If true, forces the file to be sent as a download (disables gzip compression).
 * @param callback Optional template processor for dynamic content generation.
 *                 Templates will not be processed in compressed files.
 *
 * @note If neither the file nor its compressed version exists, responds with `404 Not Found`.
 */
void AsyncWebServerRequest::send(FS &fs, const String &path, const char *contentType, bool download, AwsTemplateProcessor callback) {
  const String gzPath = path + asyncsrv::T__gz;
  const bool useCompressedVersion = !download && fs.exists(gzPath);

  // If-None-Match header
  if (useCompressedVersion && this->hasHeader(asyncsrv::T_INM)) {
    // CRC32-based ETag of the trailer, bytes 4-7 from the end
    File file = fs.open(gzPath, fs::FileOpenMode::read);
    if (file && file.size() >= 18) {  // 18 is the minimum size of valid gzip file
      file.seek(file.size() - 8);

      uint8_t crcFromGzipTrailer[4];
      if (file.read(crcFromGzipTrailer, sizeof(crcFromGzipTrailer)) == sizeof(crcFromGzipTrailer)) {
        char serverETag[9];
        _getEtag(crcFromGzipTrailer, serverETag);

        // Compare with client's If-None-Match header
        const AsyncWebHeader *inmHeader = this->getHeader(asyncsrv::T_INM);
        if (inmHeader && inmHeader->value().equals(serverETag)) {
          file.close();
          this->send(304);  // Not Modified
          return;
        }
      }
      file.close();
    }
  }

  // If we get here, create and send the normal response
  if (fs.exists(path) || useCompressedVersion) {
    send(beginResponse(fs, path, contentType, download, callback));
  } else {
    send(404);
  }
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
