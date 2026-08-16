#pragma once

#include <Arduino.h>
#include <FS.h>
#include <SD.h>
#include <SPI.h>

#include "esphome/components/logger/logger.h"
#include "esphome/core/application.h"

#include <algorithm>
#include <vector>

namespace eco_local_log {

static constexpr uint8_t SD_CS = 5;
static constexpr uint8_t SD_SCK = 18;
static constexpr uint8_t SD_MISO = 19;
static constexpr uint8_t SD_MOSI = 23;
static constexpr uint64_t MIN_FREE_BYTES = 100ULL * 1024ULL * 1024ULL;

static SPIClass sd_spi(VSPI);
static bool mounted = false;
static uint64_t next_sequence = 1;
static uint64_t acknowledged_sequence = 0;

inline uint64_t peek_next_sequence() { return next_sequence; }

inline uint64_t read_number(const char *path, uint64_t fallback) {
  File file = SD.open(path, FILE_READ);
  if (!file) return fallback;
  String value = file.readStringUntil('\n');
  file.close();
  return strtoull(value.c_str(), nullptr, 10);
}

inline bool write_number(const char *path, uint64_t value) {
  String temporary = "/state/temp.seq";
  SD.remove(temporary.c_str());
  File file = SD.open(temporary.c_str(), FILE_WRITE);
  if (!file) return false;
  file.printf("%llu\n", static_cast<unsigned long long>(value));
  file.flush();
  file.close();
  SD.remove(path);
  return SD.rename(temporary.c_str(), path);
}

inline bool begin() {
  sd_spi.begin(SD_SCK, SD_MISO, SD_MOSI, SD_CS);
  mounted = SD.begin(SD_CS, sd_spi, 4000000U);
  if (!mounted) return false;
  SD.mkdir("/logs");
  SD.mkdir("/state");
  next_sequence = read_number("/state/next.seq", 1);
  acknowledged_sequence = read_number("/state/ack.seq", 0);
  if (next_sequence <= acknowledged_sequence)
    next_sequence = acknowledged_sequence + 1;
  return true;
}

inline uint64_t sequence_from_json(const String &line) {
  const char *key = "\"sequence_id\":";
  int start = line.indexOf(key);
  if (start < 0) return 0;
  start += strlen(key);
  return strtoull(line.c_str() + start, nullptr, 10);
}

inline bool append(const String &day, const String &json, uint64_t &sequence) {
  if (!mounted) return false;
  sequence = next_sequence;
  String file_day = day.length() ? day : String("undated");
  file_day.replace("-", "");
  String path = "/logs/" + file_day + ".jsl";
  File file = SD.open(path.c_str(), FILE_APPEND);
  if (!file) return false;
  const size_t written = file.println(json);
  file.flush();
  file.close();
  if (written == 0) return false;
  next_sequence++;
  return write_number("/state/next.seq", next_sequence);
}

inline std::vector<String> log_files() {
  std::vector<String> names;
  File directory = SD.open("/logs");
  if (!directory || !directory.isDirectory()) return names;
  for (File file = directory.openNextFile(); file; file = directory.openNextFile()) {
    if (!file.isDirectory()) {
      String name = file.name();
      if (!name.startsWith("/")) name = "/logs/" + name;
      String lower_name = name;
      lower_name.toLowerCase();
      if (lower_name.endsWith(".jsl")) names.push_back(name);
    }
    file.close();
  }
  directory.close();
  std::sort(names.begin(), names.end(), [](const String &a, const String &b) {
    return strcmp(a.c_str(), b.c_str()) < 0;
  });
  return names;
}

inline bool next_pending(String &json, uint64_t &sequence) {
  if (!mounted) return false;
  for (const String &path : log_files()) {
    File file = SD.open(path.c_str(), FILE_READ);
    if (!file) continue;
    while (file.available()) {
      String line = file.readStringUntil('\n');
      line.trim();
      if (!line.length()) continue;
      const uint64_t candidate = sequence_from_json(line);
      if (candidate > acknowledged_sequence) {
        json = line;
        sequence = candidate;
        file.close();
        return true;
      }
    }
    file.close();
  }
  return false;
}

inline size_t next_pending_batch(String &json, uint64_t &highest_sequence,
                                 size_t maximum_records) {
  json = "{\"records\":[";
  highest_sequence = 0;
  if (!mounted || maximum_records == 0) {
    json = "";
    return 0;
  }

  size_t count = 0;
  for (const String &path : log_files()) {
    File file = SD.open(path.c_str(), FILE_READ);
    if (!file) continue;
    while (file.available() && count < maximum_records) {
      String line = file.readStringUntil('\n');
      line.trim();
      if (!line.length()) continue;
      const uint64_t candidate = sequence_from_json(line);
      if (candidate <= acknowledged_sequence) continue;
      line.replace("\"synced\":false", "\"synced\":true");
      if (count > 0) json += ',';
      json += line;
      highest_sequence = candidate;
      count++;
    }
    file.close();
    if (count >= maximum_records) break;
  }

  if (count == 0) {
    json = "";
    return 0;
  }
  json += "]}";
  return count;
}

inline bool acknowledge(uint64_t sequence) {
  if (!mounted || sequence <= acknowledged_sequence) return false;
  acknowledged_sequence = sequence;
  return write_number("/state/ack.seq", acknowledged_sequence);
}

inline uint64_t last_sequence_in_file(const String &path) {
  File file = SD.open(path.c_str(), FILE_READ);
  if (!file) return 0;
  uint64_t last = 0;
  while (file.available()) {
    String line = file.readStringUntil('\n');
    const uint64_t value = sequence_from_json(line);
    if (value > last) last = value;
  }
  file.close();
  return last;
}

inline void prune_synced(const String &current_day) {
  if (!mounted || SD.totalBytes() - SD.usedBytes() >= MIN_FREE_BYTES) return;
  String file_day = current_day;
  file_day.replace("-", "");
  const String current_path = "/logs/" + file_day + ".jsl";
  for (const String &path : log_files()) {
    if (path == current_path || path.endsWith("/undated.jsl")) continue;
    const uint64_t last = last_sequence_in_file(path);
    if (last > 0 && last <= acknowledged_sequence) {
      SD.remove(path.c_str());
      break;
    }
  }
}

inline uint64_t pending_count_estimate() {
  return next_sequence > acknowledged_sequence + 1
             ? next_sequence - acknowledged_sequence - 1
             : 0;
}

inline uint32_t crc32_update(uint32_t crc, const uint8_t *data, size_t length) {
  while (length--) {
    crc ^= *data++;
    for (uint8_t bit = 0; bit < 8; bit++)
      crc = (crc >> 1) ^ (0xEDB88320UL & (0UL - (crc & 1UL)));
  }
  return crc;
}

inline bool usb_export_file(const String &path) {
  File file = SD.open(path.c_str(), FILE_READ);
  if (!file || file.isDirectory()) return false;

  Serial.printf("@@CAMPER_SD_FILE|%s|%llu\n", path.c_str(),
                static_cast<unsigned long long>(file.size()));
  uint8_t bytes[48];
  char hex[sizeof(bytes) * 2 + 1];
  static constexpr char HEX_CHARS[] = "0123456789ABCDEF";
  uint64_t transferred = 0;
  uint32_t crc = 0xFFFFFFFFUL;
  while (file.available()) {
    const size_t count = file.read(bytes, sizeof(bytes));
    if (!count) break;
    crc = crc32_update(crc, bytes, count);
    for (size_t i = 0; i < count; i++) {
      hex[i * 2] = HEX_CHARS[bytes[i] >> 4];
      hex[i * 2 + 1] = HEX_CHARS[bytes[i] & 0x0F];
    }
    hex[count * 2] = '\0';
    Serial.printf("@@CAMPER_SD_DATA|%s\n", hex);
    transferred += count;
    App.feed_wdt();
    yield();
  }
  file.close();
  Serial.printf("@@CAMPER_SD_END_FILE|%s|%llu|%08lX\n", path.c_str(),
                static_cast<unsigned long long>(transferred),
                static_cast<unsigned long>(crc ^ 0xFFFFFFFFUL));
  return true;
}

inline void usb_export_all() {
  if (!mounted) {
    Serial.println("@@CAMPER_SD_ERROR|SD_NOT_MOUNTED");
    return;
  }
  const uint8_t old_level = logger::global_logger != nullptr
                                ? logger::global_logger->get_log_level()
                                : ESPHOME_LOG_LEVEL_DEBUG;
  if (logger::global_logger != nullptr)
    logger::global_logger->set_log_level(ESPHOME_LOG_LEVEL_NONE);

  Serial.println("@@CAMPER_SD_BEGIN|1");
  size_t files = 0;
  for (const String &path : log_files())
    if (usb_export_file(path)) files++;
  if (usb_export_file("/state/next.seq")) files++;
  if (usb_export_file("/state/ack.seq")) files++;
  Serial.printf("@@CAMPER_SD_DONE|%u\n", static_cast<unsigned>(files));
  Serial.flush();

  if (logger::global_logger != nullptr)
    logger::global_logger->set_log_level(old_level);
}

inline void poll_usb_command() {
  static String command;
  while (Serial.available()) {
    const char value = static_cast<char>(Serial.read());
    if (value == '\r') continue;
    if (value == '\n') {
      command.trim();
      if (command == "SD_EXPORT") usb_export_all();
      command = "";
    } else if (command.length() < 32) {
      command += value;
    } else {
      command = "";
    }
  }
}

}  // namespace eco_local_log
