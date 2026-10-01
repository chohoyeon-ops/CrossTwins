#pragma once
#include <cstdint>
#include <array>
#include <BoardConfig.h>

class HalDisplay {
 public:
  enum class Controller { Host };
  Controller getController() const;

  enum class GrayscaleMode { Overlay, Absolute };
  enum class GrayscaleBase { Combined, Separate };
  struct GrayscaleCapabilities { bool asyncBase = false; bool stripUploads = false; GrayscaleBase base; };

  enum class GrayscaleEncoding { Host };

  GrayscaleCapabilities grayscaleCapabilities(GrayscaleMode mode = GrayscaleMode::Overlay) const;

  HalDisplay();

  ~HalDisplay();

  enum RefreshMode {
    FULL_REFRESH,  // Full refresh with complete waveform
    HALF_REFRESH,  // Half refresh (1720ms) - balanced quality and speed
    FAST_REFRESH   // Fast refresh using custom LUT
  };

  void begin(bool seamless = false);

  static constexpr uint16_t DISPLAY_WIDTH = 800;
  static constexpr uint16_t DISPLAY_HEIGHT = 480;
  static constexpr uint16_t DISPLAY_WIDTH_BYTES = DISPLAY_WIDTH / 8;
  static constexpr uint32_t BUFFER_SIZE = DISPLAY_WIDTH_BYTES * DISPLAY_HEIGHT;

  void clearScreen(uint8_t color = 0xFF) const;
  void drawImage(const uint8_t* imageData, uint16_t x, uint16_t y, uint16_t w, uint16_t h,
                 bool fromProgmem = false) const;
  void drawImageTransparent(const uint8_t* imageData, uint16_t x, uint16_t y, uint16_t w, uint16_t h,
                            bool fromProgmem = false) const;

  void displayBuffer(RefreshMode mode = RefreshMode::FAST_REFRESH, bool turnOffScreen = false);
  void displayBufferAsync(RefreshMode mode = RefreshMode::FAST_REFRESH);
  void waitRefreshComplete();
  bool supportsAsyncRefresh() const;
  bool supportsAsyncGrayscaleBase() const;
  void refreshDisplay(RefreshMode mode = RefreshMode::FAST_REFRESH, bool turnOffScreen = false);

  void setInverted(bool inverted);
  bool toggleInverted();
  bool isInverted() const;

  void deepSleep();

  uint8_t* getFrameBuffer() const;

  uint8_t* lendFrameBufferStorage(uint32_t* sizeOut);
  void returnFrameBufferStorage();

  void preconditionGrayscale();
  void preconditionGrayscale(uint16_t x, uint16_t y, uint16_t w, uint16_t h);

  void displayGrayscaleBase(RefreshMode fallback = HALF_REFRESH, bool turnOffScreen = false);
  bool displayGrayscaleBase(GrayscaleMode mode, RefreshMode fallback = HALF_REFRESH, bool turnOffScreen = false);

  void copyGrayscaleBuffers(const uint8_t* lsbBuffer, const uint8_t* msbBuffer);
  void copyGrayscaleLsbBuffers(const uint8_t* lsbBuffer);
  void copyGrayscaleMsbBuffers(const uint8_t* msbBuffer);
  void cleanupGrayscaleBuffers(const uint8_t* bwBuffer);

  void displayGrayBuffer(bool turnOffScreen = false);

  void writeGrayscalePlaneStrip(bool lsbPlane, const uint8_t* rows, uint16_t yStart, uint16_t numRows);
  bool supportsStripGrayscale() const;

  bool combinesGrayscaleBase() const;

  uint16_t getDisplayWidth() const;
  uint16_t getDisplayHeight() const;
  uint16_t getDisplayWidthBytes() const;
  uint32_t getBufferSize() const;

 private:
 mutable std::array<uint8_t, BUFFER_SIZE> framebuffer{};
};
inline HalDisplay::HalDisplay() = default;
inline HalDisplay::~HalDisplay() = default;
inline uint8_t* HalDisplay::getFrameBuffer() const { return framebuffer.data(); }
inline uint16_t HalDisplay::getDisplayWidth() const { return DISPLAY_WIDTH; }
inline uint16_t HalDisplay::getDisplayHeight() const { return DISPLAY_HEIGHT; }
inline uint16_t HalDisplay::getDisplayWidthBytes() const { return DISPLAY_WIDTH_BYTES; }
inline uint32_t HalDisplay::getBufferSize() const { return BUFFER_SIZE; }

inline uint8_t* HalDisplay::lendFrameBufferStorage(uint32_t* size) { *size=BUFFER_SIZE; return framebuffer.data(); }
inline void HalDisplay::returnFrameBufferStorage() {}
inline void HalDisplay::drawImage(const uint8_t*,uint16_t,uint16_t,uint16_t,uint16_t,bool) const {}
inline bool HalDisplay::isInverted() const { return false; }
inline void HalDisplay::clearScreen(uint8_t color) const { framebuffer.fill(color); }
inline void HalDisplay::displayBuffer(RefreshMode,bool) {}
inline void HalDisplay::displayBufferAsync(RefreshMode) {}
inline void HalDisplay::waitRefreshComplete() {}
inline bool HalDisplay::supportsAsyncRefresh() const { return false; }
inline HalDisplay::GrayscaleCapabilities HalDisplay::grayscaleCapabilities(GrayscaleMode) const { return {}; }
inline void HalDisplay::displayGrayscaleBase(RefreshMode,bool) {}
inline bool HalDisplay::displayGrayscaleBase(GrayscaleMode,RefreshMode,bool) { return true; }
inline void HalDisplay::preconditionGrayscale() {}
inline void HalDisplay::preconditionGrayscale(uint16_t,uint16_t,uint16_t,uint16_t) {}
inline void HalDisplay::copyGrayscaleLsbBuffers(const uint8_t*) {}
inline void HalDisplay::copyGrayscaleMsbBuffers(const uint8_t*) {}
inline void HalDisplay::displayGrayBuffer(bool) {}
inline void HalDisplay::cleanupGrayscaleBuffers(const uint8_t*) {}
inline void HalDisplay::writeGrayscalePlaneStrip(bool,const uint8_t*,uint16_t,uint16_t) {}
