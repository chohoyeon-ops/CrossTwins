#include "OtaUpdater.h"

// clang-format off
// HttpDownloader.h pulls Arduino/SdFat, whose macros collide with lwip's
// ip4_addr.h unless seen first. Pin this order; clang-format would otherwise sort
// the local header last and break the build.
#include "HttpDownloader.h"
#include <Logging.h>
#include <Memory.h>
#include <ReleaseJsonParser.h>
#include <esp_ota_ops.h>
#include <esp_wifi.h>
// clang-format on

#include <algorithm>
#include <cstring>
#include <string>

#include "FirmwareBoardTag.h"
#include "FirmwareFlasher.h"

namespace {
constexpr size_t RELEASES_PER_PAGE = 100;
constexpr unsigned MAX_RELEASE_PAGES = 10;
constexpr size_t MAX_RELEASE_PAGE_BYTES = 2 * 1024 * 1024;
}  // namespace

OtaUpdater::OtaUpdaterError OtaUpdater::checkForUpdate() {
  updateAvailable = false;
  latestVersion.clear();
  otaUrl.clear();
  otaSize = totalSize = 0;
  LOG_DBG("OTA", "Checking CrossTwins releases (current: %s)", CROSSPOINT_VERSION);

  // This OTA path selects the combined X3/X4 application; other boards are excluded.
  if (board_tag::boardNameLen() != 2 || memcmp(board_tag::boardName(), "x4", 2) != 0) return NO_UPDATE;

  // Fixed-size state (~3KB) is kept off the activity task stack. Stream large
  // release bodies; never retain the whole API response alongside TLS buffers.
  auto releases = makeUniqueNoThrow<ReleaseListParser>();
  if (!releases) {
    LOG_ERR("OTA", "OOM allocating release parser");
    return OOM_ERROR;
  }
  bool allPagesRead = false;
  for (unsigned page = 1; page <= MAX_RELEASE_PAGES; ++page) {
    char url[160]{};
    snprintf(url, sizeof(url), "https://api.github.com/repos/%s/releases?per_page=%zu&page=%u",
             cross_twins::firmwareRepository, RELEASES_PER_PAGE, page);
    releases->beginPage();
    size_t received = 0;
    bool invalidResponse = false;
    const bool ok = HttpDownloader::fetchUrl(url, [&](const uint8_t* data, size_t len) {
      if (len > MAX_RELEASE_PAGE_BYTES - received) {
        invalidResponse = true;
        return false;
      }
      received += len;
      if (!releases->feed(reinterpret_cast<const char*>(data), len)) {
        invalidResponse = true;
        return false;
      }
      return true;
    });
    if (invalidResponse || (ok && !releases->finishPage())) {
      LOG_ERR("OTA", "Invalid or oversized release response on page %u", page);
      return JSON_PARSE_ERROR;
    }
    if (!ok) {
      LOG_ERR("OTA", "Release check fetch failed on page %u", page);
      return HTTP_ERROR;
    }
    if (releases->pageCount() < RELEASES_PER_PAGE) {
      allPagesRead = true;
      break;
    }
  }
  // Never claim to have selected the latest release from a truncated history.
  if (!allPagesRead) {
    LOG_ERR("OTA", "Release history exceeds bounded pagination");
    return JSON_PARSE_ERROR;
  }
  if (!releases->foundRelease()) return NO_UPDATE;
  if (releases->hasRejectedFirmwareUrl()) {
    LOG_ERR("OTA", "Rejected firmware asset URL in release: %s", releases->getTagName());
    return ASSET_URL_ERROR;
  }
  const auto order = compareFirmwareVersions(releases->getTagName(), CROSSPOINT_VERSION);
  if (order == FirmwareVersionOrder::Invalid) {
    LOG_ERR("OTA", "Cannot compare firmware versions: %s / %s", releases->getTagName(), CROSSPOINT_VERSION);
    return JSON_PARSE_ERROR;
  }
  if (!releases->foundFirmware()) {
    LOG_INF("OTA", "No unambiguous X3/X4 asset in latest published release: %s", releases->getTagName());
    return NO_UPDATE;
  }
  latestVersion = releases->getTagName();
  otaUrl = releases->getFirmwareUrl();
  otaSize = releases->getFirmwareSize();
  totalSize = otaSize;
  updateAvailable = true;
  LOG_DBG("OTA", "Found CrossTwins release: tag=%s size=%zu", latestVersion.c_str(), otaSize);
  return OK;
}

bool OtaUpdater::isUpdateNewer() const {
  return updateAvailable &&
         compareFirmwareVersions(latestVersion.c_str(), CROSSPOINT_VERSION) == FirmwareVersionOrder::Newer;
}

const std::string& OtaUpdater::getLatestVersion() const { return latestVersion; }

OtaUpdater::OtaUpdaterError OtaUpdater::installUpdate(ProgressCallback onProgress, void* ctx) {
  if (!isUpdateNewer()) {
    return UPDATE_OLDER_ERROR;
  }

  // esp_https_ota is hardwired to esp-tls/mbedTLS, whose precompiled build on this
  // package can't negotiate TLS 1.3 (see SecureClient.h). Drive the OTA partition
  // ourselves and stream the firmware through HttpDownloader, which runs over
  // wolfSSL when FREEINK_NET_WOLFSSL is set, reusing its redirect handling for the
  // GitHub -> CDN hop.
  const esp_partition_t* updatePartition = esp_ota_get_next_update_partition(nullptr);
  if (!updatePartition) {
    LOG_ERR("OTA", "No OTA partition available");
    return INTERNAL_UPDATE_ERROR;
  }

  esp_ota_handle_t otaHandle = 0;
  esp_err_t esp_err = esp_ota_begin(updatePartition, OTA_SIZE_UNKNOWN, &otaHandle);
  if (esp_err != ESP_OK) {
    LOG_ERR("OTA", "esp_ota_begin failed: %s", esp_err_to_name(esp_err));
    return INTERNAL_UPDATE_ERROR;
  }

  /* For better timing and connectivity, we disable power saving for WiFi */
  esp_wifi_set_ps(WIFI_PS_NONE);

  processedSize = 0;
  int lastReportedPct = -1;
  bool flashOk = true;
  // The image streams in chunks; only the first bytes carry the header. Buffer
  // the first 14 bytes so we can read chip_id (esp_image_header_t offset 12)
  // and reject a wrong-MCU image before it overwrites the OTA partition.
  uint8_t hdr[14];
  size_t hdrLen = 0;
  bool wrongChip = false;
  // All S3 boards share a chip_id, so also scan the stream for the embedded
  // board tag (FirmwareBoardTag.h). An untagged image passes; a tag naming a
  // different board aborts the download. The wrong image may partially land in
  // the inactive OTA slot, but esp_ota_abort() below means it never becomes
  // the boot target.
  board_tag::Scanner tagScanner;
  const bool fetchOk = HttpDownloader::fetchUrl(otaUrl, [&](const uint8_t* data, size_t len) {
    if (hdrLen < sizeof(hdr)) {
      const size_t take = std::min(len, sizeof(hdr) - hdrLen);
      std::memcpy(hdr + hdrLen, data, take);
      hdrLen += take;
      if (hdrLen == sizeof(hdr)) {
        uint16_t imageChip;
        std::memcpy(&imageChip, hdr + 12, sizeof(imageChip));
        const uint16_t deviceChip = firmware_flash::runningPartitionChipId();
        if (deviceChip != 0xFFFF && imageChip != deviceChip) {
          LOG_ERR("OTA", "wrong chip: image=0x%04X device=0x%04X", imageChip, deviceChip);
          wrongChip = true;
          return false;  // abort the transfer
        }
      }
    }
    tagScanner.feed(data, len);
    if (tagScanner.mismatch()) {
      LOG_ERR("OTA", "wrong board: image=%s device=%.*s", tagScanner.foundName(),
              static_cast<int>(board_tag::boardNameLen()), board_tag::boardName());
      return false;  // abort the transfer
    }
    if (esp_ota_write(otaHandle, data, len) != ESP_OK) {
      flashOk = false;
      return false;  // abort the transfer
    }
    processedSize += len;
    // Fire the callback only on whole-percent change. Per-chunk updates wake the
    // render task, whose framebuffer work contends with TLS on the internal arena,
    // and e-ink can't repaint faster than a percent tick anyway.
    if (onProgress && totalSize > 0) {
      const int pct = static_cast<int>(static_cast<uint64_t>(processedSize) * 100 / totalSize);
      if (pct != lastReportedPct) {
        lastReportedPct = pct;
        onProgress(ctx);
      }
    }
    return true;
  });

  /* Return back to default power saving for WiFi in case of failing */
  esp_wifi_set_ps(WIFI_PS_MIN_MODEM);

  if (wrongChip || tagScanner.mismatch()) {
    LOG_ERR("OTA", "Firmware install aborted: wrong device");
    esp_ota_abort(otaHandle);
    return WRONG_DEVICE_ERROR;
  }

  if (!fetchOk || !flashOk) {
    LOG_ERR("OTA", "Firmware install failed (%s)", flashOk ? "download" : "flash write");
    esp_ota_abort(otaHandle);
    return flashOk ? HTTP_ERROR : INTERNAL_UPDATE_ERROR;
  }

  esp_err = esp_ota_end(otaHandle);  // verifies the written image
  if (esp_err != ESP_OK) {
    LOG_ERR("OTA", "esp_ota_end failed: %s", esp_err_to_name(esp_err));
    return INTERNAL_UPDATE_ERROR;
  }

  esp_err = esp_ota_set_boot_partition(updatePartition);
  if (esp_err != ESP_OK) {
    LOG_ERR("OTA", "esp_ota_set_boot_partition failed: %s", esp_err_to_name(esp_err));
    return INTERNAL_UPDATE_ERROR;
  }

  LOG_INF("OTA", "Update completed");
  return OK;
}
