#pragma once

#include <cstddef>
#include <cstdint>

#include "StreamingJsonParser.h"

class ReleaseJsonParser {
 public:
  ReleaseJsonParser();

  ReleaseJsonParser(const ReleaseJsonParser&) = delete;
  ReleaseJsonParser& operator=(const ReleaseJsonParser&) = delete;

  void reset();
  void feed(const char* data, size_t len);

  // Release-asset filename to match (default "firmware.bin").
  void setFirmwareAssetName(const char* name);

  bool foundTag() const;
  bool foundFirmware() const;
  const char* getTagName() const;
  const char* getFirmwareUrl() const;
  size_t getFirmwareSize() const;

 private:
  enum class Position : uint8_t {
    TOP_LEVEL,
    IN_ASSETS_ARRAY,
    IN_ASSET_OBJECT,
  };

  enum class LastKey : uint8_t {
    NONE,
    TAG_NAME,
    ASSETS,
    ASSET_NAME,
    ASSET_URL,
    ASSET_SIZE,
  };

  static void sOnKey(void* ctx, const char* key, size_t len);
  static void sOnString(void* ctx, const char* value, size_t len);
  static void sOnNumber(void* ctx, const char* value, size_t len);
  static void sOnBool(void* ctx, bool value);
  static void sOnNull(void* ctx);
  static void sOnObjectStart(void* ctx);
  static void sOnObjectEnd(void* ctx);
  static void sOnArrayStart(void* ctx);
  static void sOnArrayEnd(void* ctx);

  void commitAsset();

  StreamingJsonParser parser;

  Position position;
  LastKey lastKey;
  uint8_t depth;
  uint8_t assetDepth;

  char tagName[32];
  char firmwareUrl[512];
  size_t firmwareSize;
  bool tagFound;
  bool firmwareFound;

  char currentAssetName[48];
  char currentAssetUrl[512];
  size_t currentAssetSize;

  char firmwareAssetName[48];
};

// Strict, bounded syntax checking for OTA responses. The legacy SAX consumers
// keep their existing behavior; OTA requires a complete, well-formed document.
class ReleaseJsonSyntax {
 public:
  void feed(char c);
  bool complete() const { return !error && done && depth == 0 && state == State::Scan; }
  bool failed() const { return error; }
  bool expectsValue() const;

 private:
  enum class State : uint8_t { Scan, String, Escape, Unicode, Number, Literal };
  enum class Phase : uint8_t { KeyOrEnd, Key, Colon, ValueOrEnd, Value, CommaOrEnd };
  struct Frame {
    char kind;
    Phase phase;
  };
  bool beginValue();
  void scan(char c);
  void endScalar();
  Frame frames[32]{};
  size_t depth = 0;
  State state = State::Scan;
  bool error = false;
  bool done = false;
  bool stringKey = false;
  unsigned unicodeLeft = 0;
  char number[64]{};
  size_t numberLen = 0;
  const char* literal = nullptr;
  size_t literalPos = 0;
};

// SemVer ordering, with an optional leading 'v' and ignored build metadata.
// Invalid inputs are explicitly incomparable (never an update).
enum class FirmwareVersionOrder { Invalid, Older, Equal, Newer };
FirmwareVersionOrder compareFirmwareVersions(const char* candidate, const char* current);
bool makeX3X4AssetName(const char* rawTag, char* output, size_t capacity);

// One public /releases page at a time. Selection survives beginPage(), while
// all per-page parsing state is reset. No response-sized allocations.
class ReleaseListParser {
 public:
  ReleaseListParser();
  ReleaseListParser(const ReleaseListParser&) = delete;
  ReleaseListParser& operator=(const ReleaseListParser&) = delete;
  void beginPage();
  bool feed(const char* data, size_t len);
  bool finishPage() const;
  size_t pageCount() const { return count; }
  bool foundRelease() const { return selected.tag[0] != '\0'; }
  bool foundFirmware() const { return selected.validAsset; }
  const char* getTagName() const { return selected.tag; }
  const char* getFirmwareUrl() const { return selected.asset.url; }
  size_t getFirmwareSize() const { return selected.asset.size; }

 private:
  enum class Key : uint8_t { None, Tag, Published, Draft, Prerelease, Assets, Name, Url, Size };
  struct Asset {
    char name[96]{};
    char url[512]{};
    size_t size = 0;
    uint8_t fields = 0;
  };
  struct Release {
    char tag[64]{};
    char published[21]{};
    bool draft = false;
    uint8_t fields = 0;
    Asset asset{};
    bool validAsset = false;
    bool multipleAssets = false;
  };
  static void onKey(void*, const char*, size_t);
  static void onString(void*, const char*, size_t);
  static void onNumber(void*, const char*, size_t);
  static void onBool(void*, bool);
  static void onNull(void*);
  static void onObjectStart(void*);
  static void onObjectEnd(void*);
  static void onArrayStart(void*);
  static void onArrayEnd(void*);
  void commitRelease();
  void wrongType();
  StreamingJsonParser parser;
  ReleaseJsonSyntax syntax{};
  Release current{};
  Release selected{};
  Asset asset{};
  size_t count = 0;
  unsigned depth = 0;
  unsigned assetsDepth = 0;
  Key key = Key::None;
  bool error = false;
  bool arrayRoot = false;
};