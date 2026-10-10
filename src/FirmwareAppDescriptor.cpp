#include <esp_app_desc.h>
#include <esp_idf_version.h>
#include <sdkconfig.h>

#define CT_STRINGIFY_IMPL(value) #value
#define CT_STRINGIFY(value) CT_STRINGIFY_IMPL(value)
#define CT_IDF_VERSION \
  CT_STRINGIFY(ESP_IDF_VERSION_MAJOR) "." CT_STRINGIFY(ESP_IDF_VERSION_MINOR) "." CT_STRINGIFY(ESP_IDF_VERSION_PATCH)

static_assert(sizeof(CROSSPOINT_VERSION) <= sizeof(esp_app_desc_t::version),
              "Firmware version exceeds descriptor field");

// Override IDF's weak descriptor at link time: prebuilt SDK archives carry
// the SDK build's project/version/date, rather than this application's metadata.
extern "C" {
extern const esp_app_desc_t esp_app_desc __attribute__((section(".rodata_desc"), used)) = {
    .magic_word = ESP_APP_DESC_MAGIC_WORD,
#ifdef CONFIG_BOOTLOADER_APP_SECURE_VERSION
    .secure_version = CONFIG_BOOTLOADER_APP_SECURE_VERSION,
#endif
    .version = CROSSPOINT_VERSION,
    .project_name = "CrossTwins",
#ifdef CONFIG_APP_COMPILE_TIME_DATE
    .time = __TIME__,
    .date = __DATE__,
#endif
    .idf_ver = CT_IDF_VERSION,
    .min_efuse_blk_rev_full = CONFIG_ESP_EFUSE_BLOCK_REV_MIN_FULL,
    .max_efuse_blk_rev_full = CONFIG_ESP_EFUSE_BLOCK_REV_MAX_FULL,
    .mmu_page_size = 31 - __builtin_clz(CONFIG_MMU_PAGE_SIZE),
};
}
