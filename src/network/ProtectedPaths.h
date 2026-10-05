#pragma once

#include <cstdint>
#include <string>
#include <string_view>

// Credential isolation from upstream #3824 (5b1f0605f3c841f8035bd5160f9459bcc60ea2de).
// Only network file access uses these guards; internal credential stores keep their fixed paths.
namespace protectedpaths {
inline constexpr std::string_view SENSITIVE_FILES[] = {"/.crosspoint/wifi.json", "/.crosspoint/opds.json",
                                                       "/.crosspoint/koreader.json"};

// Reject byte spellings which a C-string API or FAT decoder could interpret differently.
// No Unicode normalization: valid Korean and other Unicode names are preserved.
inline bool validPathBytes(std::string_view path) {
  for (size_t i = 0; i < path.size();) {
    const uint8_t c = static_cast<uint8_t>(path[i++]);
    if (c < 0x80) {
      if (c < 0x20 || c == 0x7F || std::string_view("\\:\"*?<>|").find(c) != std::string_view::npos) return false;
      continue;
    }
    uint32_t cp;
    unsigned remaining;
    uint32_t minimum;
    if (c >= 0xC2 && c <= 0xDF) {
      cp = c & 0x1F;
      remaining = 1;
      minimum = 0x80;
    } else if (c >= 0xE0 && c <= 0xEF) {
      cp = c & 0x0F;
      remaining = 2;
      minimum = 0x800;
    } else if (c >= 0xF0 && c <= 0xF4) {
      cp = c & 7;
      remaining = 3;
      minimum = 0x10000;
    } else
      return false;
    if (remaining > path.size() - i) return false;
    while (remaining--) {
      const uint8_t next = static_cast<uint8_t>(path[i++]);
      if ((next & 0xC0) != 0x80) return false;
      cp = (cp << 6) | (next & 0x3F);
    }
    if (cp < minimum || cp > 0x10FFFF || (cp >= 0xD800 && cp <= 0xDFFF)) return false;
  }
  return true;
}

// Match the spelling FAT opens. A single reserved string avoids per-component heap allocations.
// Full paths remain rooted on SD; parent references clamp at root, as the existing web API did.
inline bool normalizePath(std::string_view path, std::string& out) {
  out.clear();
  if (!validPathBytes(path)) return false;
  out.reserve(path.size() + 1);
  size_t pos = 0;
  while (pos < path.size()) {
    const size_t end = path.find('/', pos);
    auto part = path.substr(pos, end == std::string_view::npos ? path.size() - pos : end - pos);
    pos = end == std::string_view::npos ? path.size() : end + 1;
    if (part.empty() || part == ".") continue;
    if (part == "..") {
      const auto slash = out.rfind('/');
      out.erase(slash == std::string::npos ? 0 : slash);
      continue;
    }
    while (!part.empty() && part.front() == ' ') part.remove_prefix(1);
    while (!part.empty() && (part.back() == '.' || part.back() == ' ')) part.remove_suffix(1);
    if (part.empty()) {
      out.clear();
      return false;
    }
    out += '/';
    out.append(part.data(), part.size());
  }
  if (out.empty()) out = "/";
  return true;
}

inline bool isShortAlias(std::string_view part) {
  const auto tilde = part.find('~');
  if (tilde == std::string_view::npos || tilde + 1 == part.size() || part[tilde + 1] < '0' || part[tilde + 1] > '9')
    return false;
  const auto dot = part.find('.');
  const size_t base = dot == std::string_view::npos ? part.size() : dot;
  const size_t ext = dot == std::string_view::npos ? 0 : part.size() - dot - 1;
  return base <= 8 && ext <= 3;
}
inline char lowerAscii(char c) { return c >= 'A' && c <= 'Z' ? static_cast<char>(c + ('a' - 'A')) : c; }

// Fail closed on invalid paths and shallow 8.3 aliases: without querying FAT, an alias cannot
// reliably be mapped to its long filename. Prefix matching also covers temp/backup siblings.
inline bool isSensitivePath(std::string_view path) {
  if (path.empty()) return true;
  std::string canon;
  if (!normalizePath(path, canon)) return true;
  size_t start = 1;
  for (int depth = 0; depth < 2 && start < canon.size(); ++depth) {
    size_t end = canon.find('/', start);
    if (end == std::string::npos) end = canon.size();
    if (isShortAlias(std::string_view(canon).substr(start, end - start))) return true;
    start = end + 1;
  }
  for (auto file : SENSITIVE_FILES) {
    if (canon.size() < file.size()) continue;
    bool match = true;
    for (size_t i = 0; i < file.size(); ++i) {
      if (lowerAscii(canon[i]) != file[i]) {
        match = false;
        break;
      }
    }
    if (match) return true;
  }
  return false;
}
}  // namespace protectedpaths
