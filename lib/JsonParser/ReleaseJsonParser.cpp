#include "ReleaseJsonParser.h"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string_view>

namespace {

void safeCopy(char* dst, size_t dstSize, const char* src, size_t srcLen) {
  size_t n = srcLen < dstSize - 1 ? srcLen : dstSize - 1;
  memcpy(dst, src, n);
  dst[n] = '\0';
}

}  // namespace

ReleaseJsonParser::ReleaseJsonParser()
    : parser(JsonCallbacks{this, sOnKey, sOnString, sOnNumber, sOnBool, sOnNull, sOnObjectStart, sOnObjectEnd,
                           sOnArrayStart, sOnArrayEnd}) {
  safeCopy(firmwareAssetName, sizeof(firmwareAssetName), "firmware.bin", sizeof("firmware.bin") - 1);
  reset();
}

void ReleaseJsonParser::setFirmwareAssetName(const char* name) {
  safeCopy(firmwareAssetName, sizeof(firmwareAssetName), name, strlen(name));
}

void ReleaseJsonParser::reset() {
  parser.reset();
  position = Position::TOP_LEVEL;
  lastKey = LastKey::NONE;
  depth = 0;
  assetDepth = 0;
  tagName[0] = '\0';
  firmwareUrl[0] = '\0';
  firmwareSize = 0;
  tagFound = false;
  firmwareFound = false;
  currentAssetName[0] = '\0';
  currentAssetUrl[0] = '\0';
  currentAssetSize = 0;
}

void ReleaseJsonParser::feed(const char* data, size_t len) { parser.feed(data, len); }

bool ReleaseJsonParser::foundTag() const { return tagFound; }
bool ReleaseJsonParser::foundFirmware() const { return firmwareFound; }
const char* ReleaseJsonParser::getTagName() const { return tagName; }
const char* ReleaseJsonParser::getFirmwareUrl() const { return firmwareUrl; }
size_t ReleaseJsonParser::getFirmwareSize() const { return firmwareSize; }

void ReleaseJsonParser::commitAsset() {
  if (strcmp(currentAssetName, firmwareAssetName) == 0) {
    memcpy(firmwareUrl, currentAssetUrl, sizeof(firmwareUrl));
    firmwareSize = currentAssetSize;
    firmwareFound = true;
  }
  currentAssetName[0] = '\0';
  currentAssetUrl[0] = '\0';
  currentAssetSize = 0;
}

// -- SAX callbacks (static trampolines) -------------------------------------

void ReleaseJsonParser::sOnKey(void* ctx, const char* key, size_t len) {
  auto* self = static_cast<ReleaseJsonParser*>(ctx);

  switch (self->position) {
    case Position::TOP_LEVEL:
      if (self->depth == 1) {
        if (len == 8 && memcmp(key, "tag_name", 8) == 0)
          self->lastKey = LastKey::TAG_NAME;
        else if (len == 6 && memcmp(key, "assets", 6) == 0)
          self->lastKey = LastKey::ASSETS;
        else
          self->lastKey = LastKey::NONE;
      }
      break;
    case Position::IN_ASSET_OBJECT:
      if (self->assetDepth == 1) {
        if (len == 4 && memcmp(key, "name", 4) == 0)
          self->lastKey = LastKey::ASSET_NAME;
        else if (len == 20 && memcmp(key, "browser_download_url", 20) == 0)
          self->lastKey = LastKey::ASSET_URL;
        else if (len == 4 && memcmp(key, "size", 4) == 0)
          self->lastKey = LastKey::ASSET_SIZE;
        else
          self->lastKey = LastKey::NONE;
      }
      break;
    default:
      break;
  }
}

void ReleaseJsonParser::sOnString(void* ctx, const char* value, size_t len) {
  auto* self = static_cast<ReleaseJsonParser*>(ctx);

  switch (self->lastKey) {
    case LastKey::TAG_NAME:
      if (self->position == Position::TOP_LEVEL && self->depth == 1) {
        safeCopy(self->tagName, sizeof(self->tagName), value, len);
        self->tagFound = true;
      }
      break;
    case LastKey::ASSET_NAME:
      if (self->position == Position::IN_ASSET_OBJECT && self->assetDepth == 1)
        safeCopy(self->currentAssetName, sizeof(self->currentAssetName), value, len);
      break;
    case LastKey::ASSET_URL:
      if (self->position == Position::IN_ASSET_OBJECT && self->assetDepth == 1)
        safeCopy(self->currentAssetUrl, sizeof(self->currentAssetUrl), value, len);
      break;
    default:
      break;
  }
  self->lastKey = LastKey::NONE;
}

void ReleaseJsonParser::sOnNumber(void* ctx, const char* value, size_t /*len*/) {
  auto* self = static_cast<ReleaseJsonParser*>(ctx);

  if (self->lastKey == LastKey::ASSET_SIZE && self->position == Position::IN_ASSET_OBJECT && self->assetDepth == 1) {
    self->currentAssetSize = static_cast<size_t>(strtoul(value, nullptr, 10));
  }
  self->lastKey = LastKey::NONE;
}

void ReleaseJsonParser::sOnBool(void* ctx, bool /*value*/) {
  static_cast<ReleaseJsonParser*>(ctx)->lastKey = LastKey::NONE;
}

void ReleaseJsonParser::sOnNull(void* ctx) { static_cast<ReleaseJsonParser*>(ctx)->lastKey = LastKey::NONE; }

void ReleaseJsonParser::sOnObjectStart(void* ctx) {
  auto* self = static_cast<ReleaseJsonParser*>(ctx);

  switch (self->position) {
    case Position::TOP_LEVEL:
      self->depth++;
      self->lastKey = LastKey::NONE;
      break;
    case Position::IN_ASSETS_ARRAY:
      self->position = Position::IN_ASSET_OBJECT;
      self->assetDepth = 1;
      self->currentAssetName[0] = '\0';
      self->currentAssetUrl[0] = '\0';
      self->currentAssetSize = 0;
      self->lastKey = LastKey::NONE;
      break;
    case Position::IN_ASSET_OBJECT:
      self->assetDepth++;
      self->lastKey = LastKey::NONE;
      break;
  }
}

void ReleaseJsonParser::sOnObjectEnd(void* ctx) {
  auto* self = static_cast<ReleaseJsonParser*>(ctx);

  switch (self->position) {
    case Position::TOP_LEVEL:
      if (self->depth > 0) self->depth--;
      break;
    case Position::IN_ASSET_OBJECT:
      self->assetDepth--;
      if (self->assetDepth == 0) {
        self->commitAsset();
        self->position = Position::IN_ASSETS_ARRAY;
      }
      self->lastKey = LastKey::NONE;
      break;
    default:
      break;
  }
}

void ReleaseJsonParser::sOnArrayStart(void* ctx) {
  auto* self = static_cast<ReleaseJsonParser*>(ctx);

  switch (self->position) {
    case Position::TOP_LEVEL:
      if (self->lastKey == LastKey::ASSETS && self->depth == 1) {
        self->position = Position::IN_ASSETS_ARRAY;
      } else {
        self->depth++;
      }
      self->lastKey = LastKey::NONE;
      break;
    case Position::IN_ASSET_OBJECT:
      self->assetDepth++;
      self->lastKey = LastKey::NONE;
      break;
    default:
      break;
  }
}

void ReleaseJsonParser::sOnArrayEnd(void* ctx) {
  auto* self = static_cast<ReleaseJsonParser*>(ctx);

  switch (self->position) {
    case Position::TOP_LEVEL:
      if (self->depth > 0) self->depth--;
      break;
    case Position::IN_ASSETS_ARRAY:
      self->position = Position::TOP_LEVEL;
      break;
    case Position::IN_ASSET_OBJECT:
      self->assetDepth--;
      self->lastKey = LastKey::NONE;
      break;
  }
}

namespace {
bool digit(char c) { return c >= '0' && c <= '9'; }
bool whitespace(char c) { return c == ' ' || c == '\t' || c == '\r' || c == '\n'; }
bool validNumber(const char* s, size_t n) {
  size_t i = 0;
  if (i < n && s[i] == '-') ++i;
  if (i == n || !digit(s[i])) return false;
  if (s[i] == '0')
    ++i;
  else
    while (i < n && digit(s[i])) ++i;
  if (i < n && s[i] == '.') {
    ++i;
    const size_t start = i;
    while (i < n && digit(s[i])) ++i;
    if (i == start) return false;
  }
  if (i < n && (s[i] == 'e' || s[i] == 'E')) {
    ++i;
    if (i < n && (s[i] == '+' || s[i] == '-')) ++i;
    const size_t start = i;
    while (i < n && digit(s[i])) ++i;
    if (i == start) return false;
  }
  return i == n;
}
bool copyExact(char* dst, size_t capacity, const char* src, size_t len) {
  if (len == 0 || len >= capacity || memchr(src, '\0', len)) return false;
  memcpy(dst, src, len);
  dst[len] = '\0';
  return true;
}
bool unsignedSize(const char* s, size_t n, size_t& output) {
  if (!n || (n > 1 && s[0] == '0')) return false;
  size_t value = 0;
  for (size_t i = 0; i < n; ++i) {
    if (!digit(s[i]) || value > (SIZE_MAX - (s[i] - '0')) / 10) return false;
    value = value * 10 + (s[i] - '0');
  }
  output = value;
  return true;
}
bool validPublished(const char* s) {
  if (strlen(s) != 20) return false;
  for (size_t i = 0; i < 20; ++i) {
    const char expected = i == 4 || i == 7 ? '-' : i == 10 ? 'T' : i == 13 || i == 16 ? ':' : i == 19 ? 'Z' : 0;
    if (expected ? s[i] != expected : !digit(s[i])) return false;
  }
  const auto pair = [s](size_t i) { return (s[i] - '0') * 10 + s[i + 1] - '0'; };
  const int year = pair(0) * 100 + pair(2);
  const int month = pair(5), day = pair(8);
  constexpr int days[] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
  if (year == 0 || month < 1 || month > 12) return false;
  const bool leap = year % 4 == 0 && (year % 100 != 0 || year % 400 == 0);
  return day >= 1 && day <= days[month - 1] + (month == 2 && leap ? 1 : 0) && pair(11) < 24 && pair(14) < 60 &&
         pair(17) < 60;
}
struct Version {
  std::string_view core[3];
  std::string_view pre;
};
bool numeric(std::string_view s) {
  if (s.empty()) return false;
  return std::all_of(s.begin(), s.end(), digit);
}
bool identifiers(std::string_view s, bool prerelease) {
  if (s.empty()) return false;
  while (!s.empty()) {
    const size_t dot = s.find('.');
    const auto part = s.substr(0, dot);
    if (part.empty() || (prerelease && numeric(part) && part.size() > 1 && part[0] == '0')) return false;
    if (!std::all_of(part.begin(), part.end(),
                     [](char c) { return digit(c) || (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || c == '-'; }))
      return false;
    if (dot == std::string_view::npos) return true;
    s.remove_prefix(dot + 1);
  }
  return false;
}
bool parseVersion(const char* text, Version& out) {
  if (!text) return false;
  std::string_view s(text);
  if (!s.empty() && s[0] == 'v') s.remove_prefix(1);
  const size_t plus = s.find('+');
  if (plus != std::string_view::npos) {
    if (!identifiers(s.substr(plus + 1), false)) return false;
    s = s.substr(0, plus);
  }
  const size_t dash = s.find('-');
  if (dash != std::string_view::npos) {
    out.pre = s.substr(dash + 1);
    if (!identifiers(out.pre, true)) return false;
    s = s.substr(0, dash);
  }
  for (size_t i = 0; i < 3; ++i) {
    const size_t dot = s.find('.');
    out.core[i] = s.substr(0, dot);
    if (!numeric(out.core[i]) || (out.core[i].size() > 1 && out.core[i][0] == '0')) return false;
    if (i == 2) return dot == std::string_view::npos;
    if (dot == std::string_view::npos) return false;
    s.remove_prefix(dot + 1);
  }
  return false;
}
int compareNumeric(std::string_view a, std::string_view b) {
  if (a.size() != b.size()) return a.size() > b.size() ? 1 : -1;
  return a.compare(b);
}
}  // namespace

bool ReleaseJsonSyntax::expectsValue() const {
  return state == State::Scan && depth > 0 &&
         (frames[depth - 1].phase == Phase::Value || frames[depth - 1].phase == Phase::ValueOrEnd);
}
bool ReleaseJsonSyntax::beginValue() {
  if (depth == 0) {
    if (done) return false;
    return true;
  }
  auto& f = frames[depth - 1];
  if (f.phase != Phase::Value && f.phase != Phase::ValueOrEnd) return false;
  f.phase = Phase::CommaOrEnd;
  return true;
}
void ReleaseJsonSyntax::endScalar() {
  state = State::Scan;
  if (depth == 0) done = true;
}
void ReleaseJsonSyntax::scan(char c) {
  if (whitespace(c)) return;
  if (c == '}' || c == ']') {
    if (!depth) {
      error = true;
      return;
    }
    const auto& f = frames[depth - 1];
    const bool empty = f.kind == '{' ? f.phase == Phase::KeyOrEnd : f.phase == Phase::ValueOrEnd;
    if (c != (f.kind == '{' ? '}' : ']') || (!empty && f.phase != Phase::CommaOrEnd)) {
      error = true;
      return;
    }
    --depth;
    if (!depth) done = true;
    return;
  }
  if (c == ':' || c == ',') {
    if (!depth) {
      error = true;
      return;
    }
    auto& f = frames[depth - 1];
    if (c == ':' && f.phase == Phase::Colon)
      f.phase = Phase::Value;
    else if (c == ',' && f.phase == Phase::CommaOrEnd)
      f.phase = f.kind == '{' ? Phase::Key : Phase::Value;
    else
      error = true;
    return;
  }
  stringKey = depth && frames[depth - 1].kind == '{' &&
              (frames[depth - 1].phase == Phase::KeyOrEnd || frames[depth - 1].phase == Phase::Key);
  if (stringKey) {
    if (c != '"') {
      error = true;
      return;
    }
    frames[depth - 1].phase = Phase::Colon;
  } else if ((depth == 1 && frames[0].kind == '[' && c != '{') || !beginValue()) {
    error = true;
    return;
  }
  if (c == '"')
    state = State::String;
  else if (c == '{' || c == '[') {
    if (depth == 32) {
      error = true;
      return;
    }
    frames[depth++] = Frame{c, c == '{' ? Phase::KeyOrEnd : Phase::ValueOrEnd};
  } else if (c == '-' || digit(c)) {
    state = State::Number;
    numberLen = 1;
    number[0] = c;
  } else if (c == 't' || c == 'f' || c == 'n') {
    literal = c == 't' ? "true" : c == 'f' ? "false" : "null";
    literalPos = 1;
    state = State::Literal;
  } else
    error = true;
}
void ReleaseJsonSyntax::feed(char c) {
  if (error) return;
  switch (state) {
    case State::Scan:
      scan(c);
      break;
    case State::String:
      if (static_cast<unsigned char>(c) < 0x20)
        error = true;
      else if (c == '\\')
        state = State::Escape;
      else if (c == '"') {
        if (stringKey)
          state = State::Scan;
        else
          endScalar();
      }
      break;
    case State::Escape:
      if (c == 'u') {
        unicodeLeft = 4;
        state = State::Unicode;
      } else if (c == '"' || c == '\\' || c == '/' || c == 'b' || c == 'f' || c == 'n' || c == 'r' || c == 't')
        state = State::String;
      else
        error = true;
      break;
    case State::Unicode:
      if (!digit(c) && !(c >= 'a' && c <= 'f') && !(c >= 'A' && c <= 'F'))
        error = true;
      else if (--unicodeLeft == 0)
        state = State::String;
      break;
    case State::Literal:
      if (c != literal[literalPos])
        error = true;
      else if (literal[++literalPos] == '\0')
        endScalar();
      break;
    case State::Number:
      if (digit(c) || c == '-' || c == '+' || c == '.' || c == 'e' || c == 'E') {
        if (numberLen == sizeof(number))
          error = true;
        else
          number[numberLen++] = c;
      } else {
        if (!validNumber(number, numberLen)) {
          error = true;
          return;
        }
        endScalar();
        scan(c);
      }
      break;
  }
}

FirmwareVersionOrder compareFirmwareVersions(const char* candidate, const char* current) {
  Version a{}, b{};
  if (!parseVersion(candidate, a) || !parseVersion(current, b)) return FirmwareVersionOrder::Invalid;
  int order = 0;
  for (size_t i = 0; i < 3 && order == 0; ++i) order = compareNumeric(a.core[i], b.core[i]);
  if (order == 0 && (a.pre.empty() || b.pre.empty())) order = a.pre.empty() ? (b.pre.empty() ? 0 : 1) : -1;
  while (order == 0 && !a.pre.empty() && !b.pre.empty()) {
    const size_t ad = a.pre.find('.'), bd = b.pre.find('.');
    const auto ap = a.pre.substr(0, ad), bp = b.pre.substr(0, bd);
    const bool an = numeric(ap), bn = numeric(bp);
    order = an && bn ? compareNumeric(ap, bp) : an != bn ? (an ? -1 : 1) : ap.compare(bp);
    a.pre = ad == std::string_view::npos ? std::string_view{} : a.pre.substr(ad + 1);
    b.pre = bd == std::string_view::npos ? std::string_view{} : b.pre.substr(bd + 1);
    if (order == 0 && (a.pre.empty() != b.pre.empty())) order = a.pre.empty() ? -1 : 1;
  }
  return order > 0   ? FirmwareVersionOrder::Newer
         : order < 0 ? FirmwareVersionOrder::Older
                     : FirmwareVersionOrder::Equal;
}
bool makeX3X4AssetName(const char* rawTag, char* output, size_t capacity) {
  Version version{};
  if (!parseVersion(rawTag, version)) return false;
  std::string_view tag(rawTag);
  if (tag[0] == 'v') tag.remove_prefix(1);
  tag = tag.substr(0, tag.find('+'));
  const int len = snprintf(output, capacity, "crosspoint-%.*s-x3-x4.bin", static_cast<int>(tag.size()), tag.data());
  return len > 0 && static_cast<size_t>(len) < capacity;
}

ReleaseListParser::ReleaseListParser()
    : parser(JsonCallbacks{this, onKey, onString, onNumber, onBool, onNull, onObjectStart, onObjectEnd, onArrayStart,
                           onArrayEnd}) {}
void ReleaseListParser::beginPage() {
  parser.reset();
  syntax = ReleaseJsonSyntax{};
  current = Release{};
  asset = Asset{};
  count = 0;
  depth = 0;
  assetsDepth = 0;
  key = Key::None;
  error = false;
  arrayRoot = false;
}
bool ReleaseListParser::feed(const char* data, size_t len) {
  for (size_t i = 0; i < len && !error; ++i) {
    // Reject non-object asset entries before the SAX token buffer can overflow.
    if (assetsDepth && depth == assetsDepth && syntax.expectsValue() && !whitespace(data[i]) && data[i] != '{' &&
        data[i] != ']') {
      error = true;
      break;
    }
    syntax.feed(data[i]);
    if (syntax.failed()) {
      error = true;
      break;
    }
    parser.feed(data + i, 1);
    if (parser.hasError()) error = true;
  }
  return !error;
}
bool ReleaseListParser::finishPage() const { return !error && arrayRoot && syntax.complete() && depth == 0; }
void ReleaseListParser::wrongType() {
  if (key != Key::None) error = true;
  key = Key::None;
}
void ReleaseListParser::onKey(void* ctx, const char* value, size_t) {
  auto& p = *static_cast<ReleaseListParser*>(ctx);
  p.wrongType();
  uint8_t bit = 0;
  uint8_t* fields = nullptr;
  if (p.depth == 2) {
    fields = &p.current.fields;
    if (!strcmp(value, "tag_name")) {
      p.key = Key::Tag;
      bit = 1;
    } else if (!strcmp(value, "published_at")) {
      p.key = Key::Published;
      bit = 2;
    } else if (!strcmp(value, "draft")) {
      p.key = Key::Draft;
      bit = 4;
    } else if (!strcmp(value, "prerelease")) {
      p.key = Key::Prerelease;
      bit = 8;
    } else if (!strcmp(value, "assets")) {
      p.key = Key::Assets;
      bit = 16;
    }
  } else if (p.assetsDepth && p.depth == 4) {
    fields = &p.asset.fields;
    if (!strcmp(value, "name")) {
      p.key = Key::Name;
      bit = 1;
    } else if (!strcmp(value, "browser_download_url")) {
      p.key = Key::Url;
      bit = 2;
    } else if (!strcmp(value, "size")) {
      p.key = Key::Size;
      bit = 4;
    }
  }
  if (fields && bit) {
    if (*fields & bit) p.error = true;
    *fields |= bit;
  }
}
void ReleaseListParser::onString(void* ctx, const char* value, size_t len) {
  auto& p = *static_cast<ReleaseListParser*>(ctx);
  bool ok = true;
  switch (p.key) {
    case Key::Tag:
      ok = copyExact(p.current.tag, sizeof(p.current.tag), value, len);
      break;
    case Key::Published:
      ok = copyExact(p.current.published, sizeof(p.current.published), value, len);
      break;
    case Key::Name:
      ok = copyExact(p.asset.name, sizeof(p.asset.name), value, len);
      break;
    case Key::Url:
      ok = copyExact(p.asset.url, sizeof(p.asset.url), value, len);
      break;
    case Key::None:
      break;
    default:
      ok = false;
      break;
  }
  if (!ok) p.error = true;
  p.key = Key::None;
}
void ReleaseListParser::onNumber(void* ctx, const char* value, size_t len) {
  auto& p = *static_cast<ReleaseListParser*>(ctx);
  if (p.key == Key::Size) {
    if (!unsignedSize(value, len, p.asset.size)) p.error = true;
    p.key = Key::None;
  } else
    p.wrongType();
}
void ReleaseListParser::onBool(void* ctx, bool value) {
  auto& p = *static_cast<ReleaseListParser*>(ctx);
  if (p.key == Key::Draft)
    p.current.draft = value;
  else if (p.key != Key::Prerelease && p.key != Key::None)
    p.error = true;
  p.key = Key::None;
}
void ReleaseListParser::onNull(void* ctx) {
  auto& p = *static_cast<ReleaseListParser*>(ctx);
  if (p.key == Key::Published)
    p.key = Key::None;  // Draft releases have null published_at.
  else
    p.wrongType();
}
void ReleaseListParser::onObjectStart(void* ctx) {
  auto& p = *static_cast<ReleaseListParser*>(ctx);
  p.wrongType();
  ++p.depth;
  if (p.depth == 1) p.error = true;
  if (p.depth == 2) {
    p.current = Release{};
    ++p.count;
  }
  if (p.assetsDepth && p.depth == 4) p.asset = Asset{};
}
void ReleaseListParser::onObjectEnd(void* ctx) {
  auto& p = *static_cast<ReleaseListParser*>(ctx);
  // An overflowing recognized string never gets a SAX callback: reject it.
  p.wrongType();
  if (p.assetsDepth && p.depth == 4) {
    constexpr std::string_view suffix = "-x3-x4.bin";
    const std::string_view name(p.asset.name);
    if (name.size() >= suffix.size() && name.substr(name.size() - suffix.size()) == suffix) {
      if (p.current.asset.name[0]) p.current.multipleAssets = true;
      p.current.asset = p.asset;
      p.current.validAsset = p.asset.fields == 7 && p.asset.url[0] && p.asset.size > 0;
    }
  }
  if (p.depth == 2) p.commitRelease();
  if (p.depth) --p.depth;
}
void ReleaseListParser::onArrayStart(void* ctx) {
  auto& p = *static_cast<ReleaseListParser*>(ctx);
  if (p.depth == 0)
    p.arrayRoot = true;
  else if (p.depth == 2 && p.key == Key::Assets)
    p.assetsDepth = 3;
  else {
    p.wrongType();
    if (p.depth == 1 || (p.assetsDepth && p.depth == 3)) p.error = true;
  }
  p.key = Key::None;
  ++p.depth;
}
void ReleaseListParser::onArrayEnd(void* ctx) {
  auto& p = *static_cast<ReleaseListParser*>(ctx);
  p.wrongType();
  if (p.depth == p.assetsDepth) p.assetsDepth = 0;
  if (p.depth) --p.depth;
}
void ReleaseListParser::commitRelease() {
  if (!(current.fields & 4)) {
    error = true;
    return;
  }
  if (current.draft) return;
  if (current.fields != 31 || !current.tag[0] || !validPublished(current.published)) {
    error = true;
    return;
  }
  char expected[96]{};
  if (!makeX3X4AssetName(current.tag, expected, sizeof(expected)) || strcmp(expected, current.asset.name) ||
      current.multipleAssets)
    current.validAsset = false;
  if (current.validAsset) {
    char expectedUrl[512]{};
    const int len =
        snprintf(expectedUrl, sizeof(expectedUrl),
                 "https://github.com/chohoyeon-ops/CrossTwins/releases/download/%s/%s", current.tag, expected);
    if (len < 0 || static_cast<size_t>(len) >= sizeof(expectedUrl) || strcmp(expectedUrl, current.asset.url))
      current.validAsset = false;
  }
  const int timeOrder = strcmp(current.published, selected.published);
  if (!selected.tag[0] || timeOrder > 0 || (timeOrder == 0 && strcmp(current.tag, selected.tag) > 0))
    selected = current;
}