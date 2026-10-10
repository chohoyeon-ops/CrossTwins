#pragma once

#include <ReleaseJsonParser.h>

#include <cstring>
#include <string_view>

// Formats only canonical ct releases; callers retain their existing label on failure.
inline bool formatSettingsFirmwareVersionLabel(const char* version, const std::string_view brand, char* output,
                                               const size_t capacity) {
  if (!output || capacity == 0) return false;
  output[0] = '\0';
  if (!version || brand.empty() || compareFirmwareVersions(version, version) != FirmwareVersionOrder::Equal)
    return false;

  std::string_view value(version);
  if (value.starts_with('v')) value.remove_prefix(1);
  value = value.substr(0, value.find('+'));
  const size_t separator = value.find("-ct.");
  if (separator == std::string_view::npos || separator != value.find('-')) return false;
  const auto base = value.substr(0, separator);
  const auto counter = value.substr(separator + 4);
  if (counter.empty() || counter.front() == '0') return false;
  for (const char digit : counter) {
    if (digit < '0' || digit > '9') return false;
  }

  // Bound every part before summing; no allocation or truncated version label.
  if (brand.size() >= capacity || base.size() >= capacity || counter.size() >= capacity) return false;
  const size_t length = brand.size() + 1 + base.size() + 4 + counter.size();
  if (length >= capacity) return false;
  size_t position = 0;
  const auto append = [&](const std::string_view part) {
    memcpy(output + position, part.data(), part.size());
    position += part.size();
  };
  append(brand);
  append("-");
  append(base);
  append("-CT.");
  append(counter);
  output[position] = '\0';
  return true;
}
