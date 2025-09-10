// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright 2016-2025 Hristo Gochkov, Mathieu Carbou, Emil Muratov

#pragma once

#ifdef ASYNCWEBSERVER_LOG_CUSTOM
// The user must provide the following macros in AsyncWebServerLoggingCustom.h:
//   async_ws_log_e, async_ws_log_w, async_ws_log_i, async_ws_log_d, async_ws_log_v
#include <AsyncWebServerLoggingCustom.h>

#elif defined(ASYNCWEBSERVER_LOG_DEBUG)
// Local Debug logging
#include <HardwareSerial.h>
#define async_ws_log_e(format, ...) Serial.printf("E async_ws %s() %d: " format, __FUNCTION__, __LINE__, ##__VA_ARGS__);
#define async_ws_log_w(format, ...) Serial.printf("W async_ws %s() %d: " format, __FUNCTION__, __LINE__, ##__VA_ARGS__);
#define async_ws_log_i(format, ...) Serial.printf("I async_ws %s() %d: " format, __FUNCTION__, __LINE__, ##__VA_ARGS__);
#define async_ws_log_d(format, ...) Serial.printf("D async_ws %s() %d: " format, __FUNCTION__, __LINE__, ##__VA_ARGS__);
#define async_ws_log_v(format, ...) Serial.printf("V async_ws %s() %d: " format, __FUNCTION__, __LINE__, ##__VA_ARGS__);

#else
// Framework-based logging

/**
 * LibreTiny specific configurations
 */
#if defined(LIBRETINY)
#include <Arduino.h>
#define async_ws_log_e(format, ...) log_e(format, ##__VA_ARGS__)
#define async_ws_log_w(format, ...) log_w(format, ##__VA_ARGS__)
#define async_ws_log_i(format, ...) log_i(format, ##__VA_ARGS__)
#define async_ws_log_d(format, ...) log_d(format, ##__VA_ARGS__)
#define async_ws_log_v(format, ...) log_v(format, ##__VA_ARGS__)

/**
 * Raspberry Pi Pico specific configurations
 */
#elif defined(TARGET_RP2040) || defined(TARGET_RP2350) || defined(PICO_RP2040) || defined(PICO_RP2350)
#include <HardwareSerial.h>
// define log levels
#define ASYNC_WS_LOG_NONE    0 /*!< No log output */
#define ASYNC_WS_LOG_ERROR   1 /*!< Critical errors, software module can not recover on its own */
#define ASYNC_WS_LOG_WARN    2 /*!< Error conditions from which recovery measures have been taken */
#define ASYNC_WS_LOG_INFO    3 /*!< Information messages which describe normal flow of events */
#define ASYNC_WS_LOG_DEBUG   4 /*!< Extra information which is not necessary for normal use (values, pointers, sizes, etc). */
#define ASYNC_WS_LOG_VERBOSE 5 /*!< Verbose information for debugging purposes */
#define ASYNC_WS_LOG_MAX     6 /*!< Number of levels supported */
// set default log level
#ifndef ASYNCWEBSERVER_LOG_LEVEL
#define ASYNCWEBSERVER_LOG_LEVEL ASYNC_WS_LOG_INFO
#endif
// error
#if ASYNCWEBSERVER_LOG_LEVEL >= ASYNC_WS_LOG_ERROR
#define async_ws_log_e(format, ...) Serial.printf("E async_ws %s() %d: " format, __FUNCTION__, __LINE__, ##__VA_ARGS__);
#else
#define async_ws_log_e(format, ...)
#endif
// warn
#if ASYNCWEBSERVER_LOG_LEVEL >= ASYNC_WS_LOG_WARN
#define async_ws_log_w(format, ...) Serial.printf("W async_ws %s() %d: " format, __FUNCTION__, __LINE__, ##__VA_ARGS__);
#else
#define async_ws_log_w(format, ...)
#endif
// info
#if ASYNCWEBSERVER_LOG_LEVEL >= ASYNC_WS_LOG_INFO
#define async_ws_log_i(format, ...) Serial.printf("I async_ws %s() %d: " format, __FUNCTION__, __LINE__, ##__VA_ARGS__);
#else
#define async_ws_log_i(format, ...)
#endif
// debug
#if ASYNCWEBSERVER_LOG_LEVEL >= ASYNC_WS_LOG_DEBUG
#define async_ws_log_d(format, ...) Serial.printf("D async_ws %s() %d: " format, __FUNCTION__, __LINE__, ##__VA_ARGS__);
#else
#define async_ws_log_d(format, ...)
#endif
// verbose
#if ASYNCWEBSERVER_LOG_LEVEL >= ASYNC_WS_LOG_VERBOSE
#define async_ws_log_v(format, ...) Serial.printf("V async_ws %s() %d: " format, __FUNCTION__, __LINE__, ##__VA_ARGS__);
#else
#define async_ws_log_v(format, ...)
#endif

/**
 * ESP8266 specific configurations
 */
#elif defined(ESP8266)
#include <ets_sys.h>
// define log levels
#define ASYNC_WS_LOG_NONE    0 /*!< No log output */
#define ASYNC_WS_LOG_ERROR   1 /*!< Critical errors, software module can not recover on its own */
#define ASYNC_WS_LOG_WARN    2 /*!< Error conditions from which recovery measures have been taken */
#define ASYNC_WS_LOG_INFO    3 /*!< Information messages which describe normal flow of events */
#define ASYNC_WS_LOG_DEBUG   4 /*!< Extra information which is not necessary for normal use (values, pointers, sizes, etc). */
#define ASYNC_WS_LOG_VERBOSE 5 /*!< Verbose information for debugging purposes */
#define ASYNC_WS_LOG_MAX     6 /*!< Number of levels supported */
// set default log level
#ifndef ASYNCWEBSERVER_LOG_LEVEL
#define ASYNCWEBSERVER_LOG_LEVEL ASYNC_WS_LOG_INFO
#endif
// error
#if ASYNCWEBSERVER_LOG_LEVEL >= ASYNC_WS_LOG_ERROR
#define async_ws_log_e(format, ...) ets_printf(String(F("E async_ws %s() %d: " format)).c_str(), __FUNCTION__, __LINE__, ##__VA_ARGS__);
#else
#define async_ws_log_e(format, ...)
#endif
// warn
#if ASYNCWEBSERVER_LOG_LEVEL >= ASYNC_WS_LOG_WARN
#define async_ws_log_w(format, ...) ets_printf(String(F("W async_ws %s() %d: " format)).c_str(), __FUNCTION__, __LINE__, ##__VA_ARGS__);
#else
#define async_ws_log_w(format, ...)
#endif
// info
#if ASYNCWEBSERVER_LOG_LEVEL >= ASYNC_WS_LOG_INFO
#define async_ws_log_i(format, ...) ets_printf(String(F("I async_ws %s() %d: " format)).c_str(), __FUNCTION__, __LINE__, ##__VA_ARGS__);
#else
#define async_ws_log_i(format, ...)
#endif
// debug
#if ASYNCWEBSERVER_LOG_LEVEL >= ASYNC_WS_LOG_DEBUG
#define async_ws_log_d(format, ...) ets_printf(String(F("D async_ws %s() %d: " format)).c_str(), __FUNCTION__, __LINE__, ##__VA_ARGS__);
#else
#define async_ws_log_d(format, ...)
#endif
// verbose
#if ASYNCWEBSERVER_LOG_LEVEL >= ASYNC_WS_LOG_VERBOSE
#define async_ws_log_v(format, ...) ets_printf(String(F("V async_ws %s() %d: " format)).c_str(), __FUNCTION__, __LINE__, ##__VA_ARGS__);
#else
#define async_ws_log_v(format, ...)
#endif

/**
 * Arduino specific configurations
 */
#elif defined(ARDUINO)
#if defined(USE_ESP_IDF_LOG)
#include <esp_log.h>
#define async_ws_log_e(format, ...) ESP_LOGE("async_ws", "%s() %d: " format, __FUNCTION__, __LINE__, ##__VA_ARGS__)
#define async_ws_log_w(format, ...) ESP_LOGW("async_ws", "%s() %d: " format, __FUNCTION__, __LINE__, ##__VA_ARGS__)
#define async_ws_log_i(format, ...) ESP_LOGI("async_ws", "%s() %d: " format, __FUNCTION__, __LINE__, ##__VA_ARGS__)
#define async_ws_log_d(format, ...) ESP_LOGD("async_ws", "%s() %d: " format, __FUNCTION__, __LINE__, ##__VA_ARGS__)
#define async_ws_log_v(format, ...) ESP_LOGV("async_ws", "%s() %d: " format, __FUNCTION__, __LINE__, ##__VA_ARGS__)

#else
#include <esp32-hal-log.h>
#define async_ws_log_e(format, ...) log_e(format, ##__VA_ARGS__)
#define async_ws_log_w(format, ...) log_w(format, ##__VA_ARGS__)
#define async_ws_log_i(format, ...) log_i(format, ##__VA_ARGS__)
#define async_ws_log_d(format, ...) log_d(format, ##__VA_ARGS__)
#define async_ws_log_v(format, ...) log_v(format, ##__VA_ARGS__)
#endif  // USE_ESP_IDF_LOG

/**
 * ESP-IDF specific configurations
 */
#else
#include <esp_log.h>
#define async_ws_log_e(format, ...) ESP_LOGE("async_ws", "%s() %d: " format, __FUNCTION__, __LINE__, ##__VA_ARGS__)
#define async_ws_log_w(format, ...) ESP_LOGW("async_ws", "%s() %d: " format, __FUNCTION__, __LINE__, ##__VA_ARGS__)
#define async_ws_log_i(format, ...) ESP_LOGI("async_ws", "%s() %d: " format, __FUNCTION__, __LINE__, ##__VA_ARGS__)
#define async_ws_log_d(format, ...) ESP_LOGD("async_ws", "%s() %d: " format, __FUNCTION__, __LINE__, ##__VA_ARGS__)
#define async_ws_log_v(format, ...) ESP_LOGV("async_ws", "%s() %d: " format, __FUNCTION__, __LINE__, ##__VA_ARGS__)
#endif  // !LIBRETINY && !ARDUINO

#endif  // ASYNCWEBSERVER_LOG_CUSTOM
