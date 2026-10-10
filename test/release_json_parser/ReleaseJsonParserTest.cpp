#include <gtest/gtest.h>

#include <cstring>
#include <string>

#include "lib/JsonParser/ReleaseJsonParser.h"
#include "src/components/SettingsFirmwareVersionLabel.h"

TEST(SettingsFirmwareVersionLabel, UsesBrandAndUppercaseCounterWithoutSha) {
  char output[48]{};
  ASSERT_TRUE(formatSettingsFirmwareVersionLabel("1.6.5-ct.3+3e3720e", "CrossTwins", output, sizeof(output)));
  EXPECT_STREQ(output, "CrossTwins-1.6.5-CT.3");
  EXPECT_EQ(std::string(output).find('+'), std::string::npos);
  EXPECT_EQ(std::string(output).find("ct."), std::string::npos);
}

TEST(SettingsFirmwareVersionLabel, BuildMetadataDoesNotChangeDisplay) {
  for (const char* version : {"1.6.5-ct.3", "1.6.5-ct.3+abcdef0", "1.6.5-ct.3+ota-test", "v1.6.5-ct.3+1234567"}) {
    char output[48]{};
    ASSERT_TRUE(formatSettingsFirmwareVersionLabel(version, "CrossTwins", output, sizeof(output)));
    EXPECT_STREQ(output, "CrossTwins-1.6.5-CT.3");
  }
}

TEST(SettingsFirmwareVersionLabel, DerivesBaseCounterAndBrandFromInputs) {
  char output[48]{};
  ASSERT_TRUE(formatSettingsFirmwareVersionLabel("2.7.9-ct.12+abcdef0", "CrossTwins", output, sizeof(output)));
  EXPECT_STREQ(output, "CrossTwins-2.7.9-CT.12");
  ASSERT_TRUE(formatSettingsFirmwareVersionLabel("1.6.5-ct.4", "Reader", output, sizeof(output)));
  EXPECT_STREQ(output, "Reader-1.6.5-CT.4");
}

TEST(SettingsFirmwareVersionLabel, NonCtBuildsRetainCallerFallback) {
  for (const char* version :
       {"1.6.5", "1.6.5-dev-master-abcdef0", "1.6.5-rc+abcdef0", "1.6.5-CT.3", "1.6.5-foo-ct.3"}) {
    char output[48] = "stale label";
    EXPECT_FALSE(formatSettingsFirmwareVersionLabel(version, "CrossTwins", output, sizeof(output)));
    EXPECT_EQ(output[0], '\0');
  }
}

TEST(SettingsFirmwareVersionLabel, RejectsMalformedOrNonCanonicalCounters) {
  for (const char* version : {"", "garbage", "01.6.5-ct.3", "1.6.5-ct.0", "1.6.5-ct.03", "1.6.5-ct.3.1",
                              "1.6.5-ct.3-extra", "1.6.5-ct.3+", "1.6.5-ct.3+bad/SHA"}) {
    char output[48]{};
    EXPECT_FALSE(formatSettingsFirmwareVersionLabel(version, "CrossTwins", output, sizeof(output))) << version;
    EXPECT_EQ(output[0], '\0');
  }
}

TEST(SettingsFirmwareVersionLabel, RequiresSpaceForTerminatorWithoutTruncating) {
  char output[24];
  memset(output, '#', sizeof(output));
  EXPECT_FALSE(formatSettingsFirmwareVersionLabel("1.6.5-ct.3", "CrossTwins", output, 21));
  EXPECT_EQ(output[0], '\0');
  EXPECT_EQ(output[21], '#');
  ASSERT_TRUE(formatSettingsFirmwareVersionLabel("1.6.5-ct.3", "CrossTwins", output, 22));
  EXPECT_STREQ(output, "CrossTwins-1.6.5-CT.3");
  EXPECT_EQ(output[22], '#');
}

TEST(SettingsFirmwareVersionLabel, RejectsMissingArgumentsAndOverlongLabel) {
  char output[48]{};
  EXPECT_FALSE(formatSettingsFirmwareVersionLabel(nullptr, "CrossTwins", output, sizeof(output)));
  EXPECT_FALSE(formatSettingsFirmwareVersionLabel("1.6.5-ct.3", "", output, sizeof(output)));
  EXPECT_FALSE(formatSettingsFirmwareVersionLabel("1.6.5-ct.3", "CrossTwins", nullptr, 0));
  output[0] = '#';
  EXPECT_FALSE(formatSettingsFirmwareVersionLabel("1.6.5-ct.3", "CrossTwins", output, 0));
  EXPECT_EQ(output[0], '#');
  EXPECT_FALSE(formatSettingsFirmwareVersionLabel("1.6.5-ct.1234567890123456789012345678901234567890", "CrossTwins",
                                                  output, sizeof(output)));
}

TEST(SettingsFirmwareVersionLabel, LeavesInternalVersionAndOtaContractIntact) {
  char version[] = "1.6.5-ct.3+abcdef0";
  char output[48]{};
  ASSERT_TRUE(formatSettingsFirmwareVersionLabel(version, "CrossTwins", output, sizeof(output)));
  EXPECT_STREQ(version, "1.6.5-ct.3+abcdef0");
  EXPECT_EQ(compareFirmwareVersions("v1.6.5-ct.3", version), FirmwareVersionOrder::Equal);
  EXPECT_EQ(compareFirmwareVersions("v1.6.5-ct.3", "1.6.5-ct.2+ota-test"), FirmwareVersionOrder::Newer);
  EXPECT_EQ(compareFirmwareVersions(output, version), FirmwareVersionOrder::Invalid);
}

namespace {

const char* kRealisticPretty = R"({
  "url": "https://api.github.com/repos/crosspoint-reader/crosspoint-reader/releases/12345",
  "assets_url": "https://api.github.com/repos/crosspoint-reader/crosspoint-reader/releases/12345/assets",
  "upload_url": "https://uploads.github.com/repos/crosspoint-reader/crosspoint-reader/releases/12345/assets{?name,label}",
  "html_url": "https://github.com/crosspoint-reader/crosspoint-reader/releases/tag/v2.4.1",
  "id": 12345,
  "author": {
    "login": "releasebot",
    "id": 99887766,
    "node_id": "MDQ6VXNlcjk5ODg3NzY2",
    "avatar_url": "https://avatars.githubusercontent.com/u/99887766?v=4",
    "url": "https://api.github.com/users/releasebot",
    "type": "User",
    "site_admin": false
  },
  "node_id": "RE_kwDOAbCdEf4AADBN",
  "tag_name": "v2.4.1",
  "target_commitish": "main",
  "name": "CrossPoint Reader v2.4.1",
  "draft": false,
  "prerelease": false,
  "created_at": "2026-04-28T10:00:00Z",
  "published_at": "2026-04-28T10:30:00Z",
  "assets": [
    {
      "url": "https://api.github.com/repos/crosspoint-reader/crosspoint-reader/releases/assets/100001",
      "id": 100001,
      "node_id": "RA_kwDOAbCdEf4AAGHR",
      "name": "crosspoint-reader-v2.4.1-source.zip",
      "label": null,
      "uploader": {
        "login": "releasebot",
        "id": 99887766,
        "node_id": "MDQ6VXNlcjk5ODg3NzY2",
        "type": "User"
      },
      "content_type": "application/zip",
      "state": "uploaded",
      "size": 2048576,
      "download_count": 42,
      "created_at": "2026-04-28T10:15:00Z",
      "updated_at": "2026-04-28T10:15:30Z",
      "browser_download_url": "https://github.com/crosspoint-reader/crosspoint-reader/releases/download/v2.4.1/crosspoint-reader-v2.4.1-source.zip"
    },
    {
      "url": "https://api.github.com/repos/crosspoint-reader/crosspoint-reader/releases/assets/100002",
      "id": 100002,
      "node_id": "RA_kwDOAbCdEf4AAGHS",
      "name": "firmware.bin",
      "label": "ESP32-C3 Firmware",
      "uploader": {
        "login": "releasebot",
        "id": 99887766,
        "node_id": "MDQ6VXNlcjk5ODg3NzY2",
        "type": "User"
      },
      "content_type": "application/octet-stream",
      "state": "uploaded",
      "size": 1572864,
      "download_count": 187,
      "created_at": "2026-04-28T10:16:00Z",
      "updated_at": "2026-04-28T10:16:45Z",
      "browser_download_url": "https://github.com/crosspoint-reader/crosspoint-reader/releases/download/v2.4.1/firmware.bin"
    },
    {
      "url": "https://api.github.com/repos/crosspoint-reader/crosspoint-reader/releases/assets/100003",
      "id": 100003,
      "node_id": "RA_kwDOAbCdEf4AAGHR",
      "name": "checksums.sha256",
      "label": null,
      "uploader": {
        "login": "releasebot",
        "id": 99887766,
        "node_id": "MDQ6VXNlcjk5ODg3NzY2",
        "type": "User"
      },
      "content_type": "text/plain",
      "state": "uploaded",
      "size": 192,
      "download_count": 15,
      "created_at": "2026-04-28T10:17:00Z",
      "updated_at": "2026-04-28T10:17:10Z",
      "browser_download_url": "https://github.com/crosspoint-reader/crosspoint-reader/releases/download/v2.4.1/checksums.sha256"
    }
  ],
  "tarball_url": "https://api.github.com/repos/crosspoint-reader/crosspoint-reader/tarball/v2.4.1",
  "zipball_url": "https://api.github.com/repos/crosspoint-reader/crosspoint-reader/zipball/v2.4.1",
  "body": "## What's Changed\n\n* Fixed orientation crash (#123)\n* Improved EPUB rendering performance\n* Added Serbian translation\n\n**Full Changelog**: https://github.com/crosspoint-reader/crosspoint-reader/compare/v2.4.0...v2.4.1",
  "reactions": {
    "url": "https://api.github.com/repos/crosspoint-reader/crosspoint-reader/releases/12345/reactions",
    "total_count": 5,
    "+1": 3,
    "-1": 0,
    "laugh": 1,
    "hooray": 1,
    "confused": 0,
    "heart": 0,
    "rocket": 0,
    "eyes": 0
  }
})";

const char* kRealisticMinified =
    R"({"url":"https://api.github.com/repos/crosspoint-reader/crosspoint-reader/releases/12345","assets_url":"https://api.github.com/repos/crosspoint-reader/crosspoint-reader/releases/12345/assets","id":12345,"author":{"login":"releasebot","id":99887766,"node_id":"MDQ6VXNlcjk5ODg3NzY2","type":"User","site_admin":false},"tag_name":"v2.4.1","target_commitish":"main","name":"CrossPoint Reader v2.4.1","draft":false,"prerelease":false,"assets":[{"url":"https://api.github.com/repos/crosspoint-reader/crosspoint-reader/releases/assets/100001","id":100001,"name":"crosspoint-reader-v2.4.1-source.zip","uploader":{"login":"releasebot","id":99887766},"content_type":"application/zip","state":"uploaded","size":2048576,"download_count":42,"browser_download_url":"https://github.com/crosspoint-reader/crosspoint-reader/releases/download/v2.4.1/crosspoint-reader-v2.4.1-source.zip"},{"url":"https://api.github.com/repos/crosspoint-reader/crosspoint-reader/releases/assets/100002","id":100002,"name":"firmware.bin","uploader":{"login":"releasebot","id":99887766},"content_type":"application/octet-stream","state":"uploaded","size":1572864,"download_count":187,"browser_download_url":"https://github.com/crosspoint-reader/crosspoint-reader/releases/download/v2.4.1/firmware.bin"},{"url":"https://api.github.com/repos/crosspoint-reader/crosspoint-reader/releases/assets/100003","id":100003,"name":"checksums.sha256","uploader":{"login":"releasebot","id":99887766},"content_type":"text/plain","state":"uploaded","size":192,"download_count":15,"browser_download_url":"https://github.com/crosspoint-reader/crosspoint-reader/releases/download/v2.4.1/checksums.sha256"}],"body":"## What's Changed\n\n* Fixed orientation crash","reactions":{"url":"https://api.github.com/repos/crosspoint-reader/crosspoint-reader/releases/12345/reactions","total_count":5,"+1":3}})";

void feedChunked(ReleaseJsonParser& p, const char* json, size_t chunkSize) {
  size_t len = strlen(json);
  for (size_t off = 0; off < len; off += chunkSize) {
    size_t n = len - off < chunkSize ? len - off : chunkSize;
    p.feed(json + off, n);
  }
}

}  // namespace

TEST(ReleaseJsonParser, RealisticPrettyPrinted) {
  ReleaseJsonParser p;
  p.feed(kRealisticPretty, strlen(kRealisticPretty));

  EXPECT_TRUE(p.foundTag());
  EXPECT_TRUE(p.foundFirmware());
  EXPECT_STREQ(p.getTagName(), "v2.4.1");
  EXPECT_STREQ(p.getFirmwareUrl(),
               "https://github.com/crosspoint-reader/crosspoint-reader/releases/download/v2.4.1/firmware.bin");
  EXPECT_EQ(p.getFirmwareSize(), 1572864u);
}

TEST(ReleaseJsonParser, RealisticMinified) {
  ReleaseJsonParser p;
  p.feed(kRealisticMinified, strlen(kRealisticMinified));

  EXPECT_TRUE(p.foundTag());
  EXPECT_TRUE(p.foundFirmware());
  EXPECT_STREQ(p.getTagName(), "v2.4.1");
  EXPECT_STREQ(p.getFirmwareUrl(),
               "https://github.com/crosspoint-reader/crosspoint-reader/releases/download/v2.4.1/firmware.bin");
  EXPECT_EQ(p.getFirmwareSize(), 1572864u);
}

TEST(ReleaseJsonParser, PrettyAndMinifiedAgree) {
  ReleaseJsonParser pretty;
  pretty.feed(kRealisticPretty, strlen(kRealisticPretty));

  ReleaseJsonParser minified;
  minified.feed(kRealisticMinified, strlen(kRealisticMinified));

  ASSERT_TRUE(pretty.foundTag());
  ASSERT_TRUE(pretty.foundFirmware());
  ASSERT_TRUE(minified.foundTag());
  ASSERT_TRUE(minified.foundFirmware());

  EXPECT_STREQ(pretty.getTagName(), minified.getTagName());
  EXPECT_STREQ(pretty.getFirmwareUrl(), minified.getFirmwareUrl());
  EXPECT_EQ(pretty.getFirmwareSize(), minified.getFirmwareSize());
}

TEST(ReleaseJsonParser, FirmwareNotFirstAsset) {
  const char* json = R"({
      "tag_name": "v1.0.0",
      "assets": [
        {"name": "source.tar.gz", "browser_download_url": "https://example.com/src.tar.gz", "size": 500000},
        {"name": "docs.pdf", "browser_download_url": "https://example.com/docs.pdf", "size": 120000},
        {"name": "firmware.bin", "browser_download_url": "https://example.com/firmware.bin", "size": 987654},
        {"name": "checksums.txt", "browser_download_url": "https://example.com/checksums.txt", "size": 256}
      ]
    })";

  ReleaseJsonParser p;
  p.feed(json, strlen(json));

  EXPECT_TRUE(p.foundTag());
  EXPECT_TRUE(p.foundFirmware());
  EXPECT_STREQ(p.getTagName(), "v1.0.0");
  EXPECT_STREQ(p.getFirmwareUrl(), "https://example.com/firmware.bin");
  EXPECT_EQ(p.getFirmwareSize(), 987654u);
}

TEST(ReleaseJsonParser, FieldOrderUrlBeforeName) {
  const char* json = R"({
      "tag_name": "v3.0",
      "assets": [{
        "browser_download_url": "https://example.com/fw.bin",
        "name": "firmware.bin",
        "size": 2222
      }]
    })";

  ReleaseJsonParser p;
  p.feed(json, strlen(json));

  EXPECT_TRUE(p.foundFirmware());
  EXPECT_STREQ(p.getFirmwareUrl(), "https://example.com/fw.bin");
  EXPECT_EQ(p.getFirmwareSize(), 2222u);
}

TEST(ReleaseJsonParser, FieldOrderSizeBeforeUrl) {
  const char* json = R"({
      "tag_name": "v3.1",
      "assets": [{
        "size": 3333,
        "browser_download_url": "https://example.com/fw2.bin",
        "name": "firmware.bin"
      }]
    })";

  ReleaseJsonParser p;
  p.feed(json, strlen(json));

  EXPECT_TRUE(p.foundFirmware());
  EXPECT_STREQ(p.getFirmwareUrl(), "https://example.com/fw2.bin");
  EXPECT_EQ(p.getFirmwareSize(), 3333u);
}

TEST(ReleaseJsonParser, FieldOrderNameFirst) {
  const char* json = R"({
      "tag_name": "v3.2",
      "assets": [{
        "name": "firmware.bin",
        "size": 4444,
        "browser_download_url": "https://example.com/fw3.bin"
      }]
    })";

  ReleaseJsonParser p;
  p.feed(json, strlen(json));

  EXPECT_TRUE(p.foundFirmware());
  EXPECT_STREQ(p.getFirmwareUrl(), "https://example.com/fw3.bin");
  EXPECT_EQ(p.getFirmwareSize(), 4444u);
}

TEST(ReleaseJsonParser, AssetsBeforeTagName) {
  // tag_name appears after assets in the JSON
  const char* json = R"({
      "name": "Release",
      "assets": [{
        "name": "firmware.bin",
        "browser_download_url": "https://example.com/fw.bin",
        "size": 5555
      }],
      "tag_name": "v4.0"
    })";

  ReleaseJsonParser p;
  p.feed(json, strlen(json));

  EXPECT_TRUE(p.foundTag());
  EXPECT_TRUE(p.foundFirmware());
  EXPECT_STREQ(p.getTagName(), "v4.0");
  EXPECT_STREQ(p.getFirmwareUrl(), "https://example.com/fw.bin");
  EXPECT_EQ(p.getFirmwareSize(), 5555u);
}

TEST(ReleaseJsonParser, ChunkedFeedingSmallChunks) {
  ReleaseJsonParser p;
  feedChunked(p, kRealisticPretty, 64);

  EXPECT_TRUE(p.foundTag());
  EXPECT_TRUE(p.foundFirmware());
  EXPECT_STREQ(p.getTagName(), "v2.4.1");
  EXPECT_STREQ(p.getFirmwareUrl(),
               "https://github.com/crosspoint-reader/crosspoint-reader/releases/download/v2.4.1/firmware.bin");
  EXPECT_EQ(p.getFirmwareSize(), 1572864u);
}

TEST(ReleaseJsonParser, ChunkedFeedingByteByByte) {
  ReleaseJsonParser p;
  feedChunked(p, kRealisticMinified, 1);

  EXPECT_TRUE(p.foundTag());
  EXPECT_TRUE(p.foundFirmware());
  EXPECT_STREQ(p.getTagName(), "v2.4.1");
  EXPECT_EQ(p.getFirmwareSize(), 1572864u);
}

TEST(ReleaseJsonParser, ChunkedFeedingVariousChunkSizes) {
  for (size_t chunkSize : {3u, 7u, 13u, 31u, 97u, 128u, 256u, 512u, 1024u}) {
    ReleaseJsonParser p;
    feedChunked(p, kRealisticPretty, chunkSize);

    EXPECT_TRUE(p.foundTag()) << "chunkSize=" << chunkSize;
    EXPECT_TRUE(p.foundFirmware()) << "chunkSize=" << chunkSize;
    EXPECT_STREQ(p.getTagName(), "v2.4.1") << "chunkSize=" << chunkSize;
    EXPECT_STREQ(p.getFirmwareUrl(),
                 "https://github.com/crosspoint-reader/crosspoint-reader/releases/download/v2.4.1/firmware.bin")
        << "chunkSize=" << chunkSize;
    EXPECT_EQ(p.getFirmwareSize(), 1572864u) << "chunkSize=" << chunkSize;
  }
}

TEST(ReleaseJsonParser, MissingTagName) {
  const char* json = R"({
      "name": "Some Release",
      "draft": false,
      "assets": [{
        "name": "firmware.bin",
        "browser_download_url": "https://example.com/fw.bin",
        "size": 1000
      }]
    })";

  ReleaseJsonParser p;
  p.feed(json, strlen(json));

  EXPECT_FALSE(p.foundTag());
  EXPECT_TRUE(p.foundFirmware());
  EXPECT_STREQ(p.getTagName(), "");
}

TEST(ReleaseJsonParser, MissingFirmwareBinAsset) {
  const char* json = R"({
      "tag_name": "v1.0.0",
      "assets": [
        {"name": "source.zip", "browser_download_url": "https://example.com/src.zip", "size": 1000},
        {"name": "docs.tar.gz", "browser_download_url": "https://example.com/docs.tar.gz", "size": 2000}
      ]
    })";

  ReleaseJsonParser p;
  p.feed(json, strlen(json));

  EXPECT_TRUE(p.foundTag());
  EXPECT_FALSE(p.foundFirmware());
  EXPECT_STREQ(p.getTagName(), "v1.0.0");
  EXPECT_STREQ(p.getFirmwareUrl(), "");
  EXPECT_EQ(p.getFirmwareSize(), 0u);
}

TEST(ReleaseJsonParser, EmptyAssetsArray) {
  const char* json = R"({"tag_name": "v1.0.0", "assets": []})";

  ReleaseJsonParser p;
  p.feed(json, strlen(json));

  EXPECT_TRUE(p.foundTag());
  EXPECT_FALSE(p.foundFirmware());
}

TEST(ReleaseJsonParser, NoAssetsKey) {
  const char* json = R"({"tag_name": "v1.0.0", "name": "Release"})";

  ReleaseJsonParser p;
  p.feed(json, strlen(json));

  EXPECT_TRUE(p.foundTag());
  EXPECT_FALSE(p.foundFirmware());
}

TEST(ReleaseJsonParser, TruncatedBeforeTagValue) {
  const char* json = R"({"tag_name": )";

  ReleaseJsonParser p;
  p.feed(json, strlen(json));

  EXPECT_FALSE(p.foundTag());
  EXPECT_FALSE(p.foundFirmware());
}

TEST(ReleaseJsonParser, TruncatedInsideTagValue) {
  const char* json = R"({"tag_name": "v2.4)";

  ReleaseJsonParser p;
  p.feed(json, strlen(json));

  EXPECT_FALSE(p.foundTag());
}

TEST(ReleaseJsonParser, TruncatedInsideAssetsArray) {
  const char* json = R"({"tag_name": "v2.4.1", "assets": [{"name": "firm)";

  ReleaseJsonParser p;
  p.feed(json, strlen(json));

  EXPECT_TRUE(p.foundTag());
  EXPECT_STREQ(p.getTagName(), "v2.4.1");
  EXPECT_FALSE(p.foundFirmware());
}

TEST(ReleaseJsonParser, TruncatedAfterFirmwareName) {
  // Found the name but connection dropped before URL/size
  const char* json = R"({"tag_name":"v1.0","assets":[{"name":"firmware.bin","browser_dow)";

  ReleaseJsonParser p;
  p.feed(json, strlen(json));

  EXPECT_TRUE(p.foundTag());
  EXPECT_FALSE(p.foundFirmware());
}

TEST(ReleaseJsonParser, TruncatedRealisticJson) {
  std::string full(kRealisticPretty);
  for (size_t cutPoint : {10u, 50u, 100u, 200u, 500u, 1000u, 1500u, 2000u}) {
    if (cutPoint >= full.size()) continue;

    ReleaseJsonParser p;
    p.feed(full.c_str(), cutPoint);
    (void)p.foundTag();
    (void)p.foundFirmware();
  }
  SUCCEED();
}

TEST(ReleaseJsonParser, NestedObjectsInAsset) {
  // Asset with deeply nested "uploader" object -- should not confuse depth tracking
  const char* json = R"({
      "tag_name": "v5.0",
      "assets": [{
        "name": "firmware.bin",
        "uploader": {
          "login": "bot",
          "id": 42,
          "permissions": {"admin": false, "push": true, "pull": true}
        },
        "browser_download_url": "https://example.com/fw5.bin",
        "size": 8888
      }]
    })";

  ReleaseJsonParser p;
  p.feed(json, strlen(json));

  EXPECT_TRUE(p.foundFirmware());
  EXPECT_STREQ(p.getFirmwareUrl(), "https://example.com/fw5.bin");
  EXPECT_EQ(p.getFirmwareSize(), 8888u);
}

TEST(ReleaseJsonParser, NestedObjectsAtTopLevel) {
  // Multiple nested objects at the top level before/after tag_name and assets
  const char* json = R"({
      "author": {"login": "dev", "id": 1, "nested": {"deep": true}},
      "tag_name": "v6.0",
      "reactions": {"url": "https://reactions", "total_count": 0, "+1": 0},
      "assets": [{"name": "firmware.bin", "browser_download_url": "https://fw6", "size": 1111}],
      "mentions_count": 3
    })";

  ReleaseJsonParser p;
  p.feed(json, strlen(json));

  EXPECT_TRUE(p.foundTag());
  EXPECT_TRUE(p.foundFirmware());
  EXPECT_STREQ(p.getTagName(), "v6.0");
  EXPECT_EQ(p.getFirmwareSize(), 1111u);
}

TEST(ReleaseJsonParser, ArraysAtTopLevel) {
  // A non-assets array at the top level should not interfere
  const char* json = R"({
      "tag_name": "v7.0",
      "labels": ["release", "stable"],
      "assets": [{"name": "firmware.bin", "browser_download_url": "https://fw7", "size": 7070}]
    })";

  ReleaseJsonParser p;
  p.feed(json, strlen(json));

  EXPECT_TRUE(p.foundTag());
  EXPECT_TRUE(p.foundFirmware());
  EXPECT_STREQ(p.getTagName(), "v7.0");
  EXPECT_EQ(p.getFirmwareSize(), 7070u);
}

TEST(ReleaseJsonParser, ResetAndReuse) {
  ReleaseJsonParser p;

  const char* json1 =
      R"({"tag_name":"v1.0","assets":[{"name":"firmware.bin","browser_download_url":"https://a","size":1}]})";
  p.feed(json1, strlen(json1));
  EXPECT_TRUE(p.foundTag());
  EXPECT_STREQ(p.getTagName(), "v1.0");
  EXPECT_STREQ(p.getFirmwareUrl(), "https://a");
  EXPECT_EQ(p.getFirmwareSize(), 1u);

  p.reset();

  const char* json2 =
      R"({"tag_name":"v2.0","assets":[{"name":"firmware.bin","browser_download_url":"https://b","size":2}]})";
  p.feed(json2, strlen(json2));
  EXPECT_TRUE(p.foundTag());
  EXPECT_STREQ(p.getTagName(), "v2.0");
  EXPECT_STREQ(p.getFirmwareUrl(), "https://b");
  EXPECT_EQ(p.getFirmwareSize(), 2u);
}

TEST(ReleaseJsonParser, ResetClearsState) {
  ReleaseJsonParser p;

  const char* json =
      R"({"tag_name":"v1.0","assets":[{"name":"firmware.bin","browser_download_url":"https://a","size":100}]})";
  p.feed(json, strlen(json));
  EXPECT_TRUE(p.foundTag());
  EXPECT_TRUE(p.foundFirmware());

  p.reset();

  EXPECT_FALSE(p.foundTag());
  EXPECT_FALSE(p.foundFirmware());
  EXPECT_STREQ(p.getTagName(), "");
  EXPECT_STREQ(p.getFirmwareUrl(), "");
  EXPECT_EQ(p.getFirmwareSize(), 0u);
}

TEST(ReleaseJsonParser, PartialAssetNameMatch) {
  // "firmware.bin.bak" should NOT match "firmware.bin"
  const char* json = R"({
      "tag_name": "v1.0",
      "assets": [
        {"name": "firmware.bin.bak", "browser_download_url": "https://bak", "size": 100},
        {"name": "firmware.bin.old", "browser_download_url": "https://old", "size": 200}
      ]
    })";

  ReleaseJsonParser p;
  p.feed(json, strlen(json));

  EXPECT_TRUE(p.foundTag());
  EXPECT_FALSE(p.foundFirmware());
}

TEST(ReleaseJsonParser, FirmwareBinExactMatch) {
  // Only exact "firmware.bin" matches, not similar names
  const char* json = R"({
      "tag_name": "v1.0",
      "assets": [
        {"name": "FIRMWARE.BIN", "browser_download_url": "https://upper", "size": 100},
        {"name": "firmware.bin", "browser_download_url": "https://exact", "size": 200},
        {"name": "firmware.bin2", "browser_download_url": "https://suffix", "size": 300}
      ]
    })";

  ReleaseJsonParser p;
  p.feed(json, strlen(json));

  EXPECT_TRUE(p.foundFirmware());
  EXPECT_STREQ(p.getFirmwareUrl(), "https://exact");
  EXPECT_EQ(p.getFirmwareSize(), 200u);
}

TEST(ReleaseJsonParser, ConfiguredFirmwareNameMatch) {
  const char* json = R"({
      "tag_name": "1.6.1",
      "assets": [
        {"name": "firmware-x4pro.bin", "browser_download_url": "https://legacy", "size": 100},
        {"name": "crosspoint-1.6.1-sticky.bin", "browser_download_url": "https://sticky", "size": 200},
        {"name": "crosspoint-1.6.1-x4pro.bin", "browser_download_url": "https://x4pro", "size": 300}
      ]
    })";

  ReleaseJsonParser p;
  p.setFirmwareAssetName("crosspoint-1.6.1-x4pro.bin");
  p.feed(json, strlen(json));

  EXPECT_TRUE(p.foundFirmware());
  EXPECT_STREQ(p.getFirmwareUrl(), "https://x4pro");
  EXPECT_EQ(p.getFirmwareSize(), 300u);
}

TEST(ReleaseJsonParser, ConfiguredFirmwareNameIsNotOverwrittenBySameSuffix) {
  const char* json = R"({
      "tag_name": "1.6.1",
      "assets": [
        {"name": "crosspoint-1.6.1-x4pro.bin", "browser_download_url": "https://correct", "size": 100},
        {"name": "crosspoint-1.6.0-x4pro.bin", "browser_download_url": "https://stale", "size": 200}
      ]
    })";

  ReleaseJsonParser p;
  p.setFirmwareAssetName("crosspoint-1.6.1-x4pro.bin");
  p.feed(json, strlen(json));

  EXPECT_TRUE(p.foundFirmware());
  EXPECT_STREQ(p.getFirmwareUrl(), "https://correct");
  EXPECT_EQ(p.getFirmwareSize(), 100u);
}

TEST(ReleaseJsonParser, ConfiguredFirmwareNameRejectsDifferentVersion) {
  const char* json = R"({
      "tag_name": "1.6.1",
      "assets": [
        {"name": "crosspoint-1.6.0-x4pro.bin", "browser_download_url": "https://stale", "size": 200}
      ]
    })";

  ReleaseJsonParser p;
  p.setFirmwareAssetName("crosspoint-1.6.1-x4pro.bin");
  p.feed(json, strlen(json));

  EXPECT_FALSE(p.foundFirmware());
}

TEST(ReleaseJsonParser, LargeSize) {
  // 16MB firmware (maximum flash size)
  const char* json =
      R"({"tag_name":"v1.0","assets":[{"name":"firmware.bin","browser_download_url":"https://fw","size":16777216}]})";

  ReleaseJsonParser p;
  p.feed(json, strlen(json));

  EXPECT_EQ(p.getFirmwareSize(), 16777216u);
}

TEST(ReleaseJsonParser, SizeZero) {
  const char* json =
      R"({"tag_name":"v1.0","assets":[{"name":"firmware.bin","browser_download_url":"https://fw","size":0}]})";

  ReleaseJsonParser p;
  p.feed(json, strlen(json));

  EXPECT_TRUE(p.foundFirmware());
  EXPECT_EQ(p.getFirmwareSize(), 0u);
}

TEST(ReleaseJsonParser, MinimalValidJson) {
  const char* json = R"({"tag_name":"v0","assets":[{"name":"firmware.bin","browser_download_url":"u","size":1}]})";

  ReleaseJsonParser p;
  p.feed(json, strlen(json));

  EXPECT_TRUE(p.foundTag());
  EXPECT_TRUE(p.foundFirmware());
  EXPECT_STREQ(p.getTagName(), "v0");
  EXPECT_STREQ(p.getFirmwareUrl(), "u");
  EXPECT_EQ(p.getFirmwareSize(), 1u);
}

TEST(ReleaseJsonParser, ChunkedRealisticEveryBoundary) {
  // Two-chunk split at every byte boundary on a compact JSON
  const char* json =
      R"({"tag_name":"v2.0","assets":[{"name":"firmware.bin","browser_download_url":"https://example.com/fw","size":9999}]})";
  size_t len = strlen(json);

  for (size_t split = 0; split <= len; ++split) {
    ReleaseJsonParser p;
    if (split > 0) p.feed(json, split);
    if (split < len) p.feed(json + split, len - split);

    EXPECT_TRUE(p.foundTag()) << "split=" << split;
    EXPECT_TRUE(p.foundFirmware()) << "split=" << split;
    EXPECT_STREQ(p.getTagName(), "v2.0") << "split=" << split;
    EXPECT_STREQ(p.getFirmwareUrl(), "https://example.com/fw") << "split=" << split;
    EXPECT_EQ(p.getFirmwareSize(), 9999u) << "split=" << split;
  }
}

namespace {
std::string crossTwinsRelease(const std::string& tag, const std::string& published, bool prerelease = true,
                              bool draft = false, const std::string& assetName = "") {
  char generatedName[96]{};
  makeX3X4AssetName(tag.c_str(), generatedName, sizeof(generatedName));
  const std::string name = assetName.empty() ? generatedName : assetName;
  return "{\"assets\":[{\"size\":4269216,\"browser_download_url\":\"https://github.com/meat-holic/CrossTwins/"
         "releases/download/" +
         tag + "/" + name + "\",\"name\":\"" + name + "\"}],\"tag_name\":\"" + tag + "\",\"published_at\":\"" +
         published + "\",\"draft\":" + (draft ? "true" : "false") +
         ",\"prerelease\":" + (prerelease ? "true" : "false") + "}";
}
}  // namespace

TEST(CrossTwinsReleaseList, RenamedRepositoryAcceptsExactCt3Asset) {
  EXPECT_STREQ(cross_twins::firmwareRepository, "meat-holic/CrossTwins");
  const auto body = "[" + crossTwinsRelease("v1.6.5-ct.3", "2026-10-11T01:58:11Z", false) + "]";
  ReleaseListParser p;
  ASSERT_TRUE(p.feed(body.data(), body.size()));
  ASSERT_TRUE(p.finishPage());
  EXPECT_TRUE(p.foundFirmware());
  EXPECT_FALSE(p.hasRejectedFirmwareUrl());
  EXPECT_STREQ(
      p.getFirmwareUrl(),
      "https://github.com/meat-holic/CrossTwins/releases/download/v1.6.5-ct.3/crosspoint-1.6.5-ct.3-x3-x4.bin");
}
TEST(CrossTwinsReleaseList, RejectedUrlIsDistinctFromMissingFirmware) {
  for (const char* url :
       {"https://github.com/chohoyeon-ops/CrossTwins/releases/download/v1.6.5-ct.3/crosspoint-1.6.5-ct.3-x3-x4.bin",
        "https://github.com/other/CrossTwins/releases/download/v1.6.5-ct.3/crosspoint-1.6.5-ct.3-x3-x4.bin",
        "https://github.com/meat-holic/Other/releases/download/v1.6.5-ct.3/crosspoint-1.6.5-ct.3-x3-x4.bin",
        "http://github.com/meat-holic/CrossTwins/releases/download/v1.6.5-ct.3/crosspoint-1.6.5-ct.3-x3-x4.bin",
        "https://github.com.evil.example/meat-holic/CrossTwins/releases/download/v1.6.5-ct.3/"
        "crosspoint-1.6.5-ct.3-x3-x4.bin",
        "https://github.com/meat-holic/CrossTwins/releases/download/v1.6.5-ct.2/crosspoint-1.6.5-ct.3-x3-x4.bin",
        "https://github.com/meat-holic/CrossTwins/releases/download/v1.6.5-ct.3/crosspoint-1.6.5-ct.3-x3-x4.bin?x=1"}) {
    auto body = "[" + crossTwinsRelease("v1.6.5-ct.3", "2026-10-11T01:58:11Z") + "]";
    const auto start = body.find("https://github.com/");
    body.replace(start, body.find('"', start) - start, url);
    ReleaseListParser p;
    ASSERT_TRUE(p.feed(body.data(), body.size()));
    ASSERT_TRUE(p.finishPage());
    EXPECT_TRUE(p.foundRelease());
    EXPECT_FALSE(p.foundFirmware()) << url;
    EXPECT_TRUE(p.hasRejectedFirmwareUrl()) << url;
  }
}
TEST(CrossTwinsReleaseList, UrlRejectionFollowsSelectedReleaseAcrossPages) {
  auto rejected = "[" + crossTwinsRelease("v1.6.5-ct.2", "2026-10-10T01:58:11Z") + "]";
  rejected.replace(rejected.find("meat-holic"), strlen("meat-holic"), "chohoyeon-ops");
  const auto accepted = "[" + crossTwinsRelease("v1.6.5-ct.3", "2026-10-11T01:58:11Z") + "]";
  ReleaseListParser p;
  ASSERT_TRUE(p.feed(rejected.data(), rejected.size()));
  ASSERT_TRUE(p.finishPage());
  EXPECT_TRUE(p.hasRejectedFirmwareUrl());
  p.beginPage();
  ASSERT_TRUE(p.feed(accepted.data(), accepted.size()));
  ASSERT_TRUE(p.finishPage());
  EXPECT_TRUE(p.foundFirmware());
  EXPECT_FALSE(p.hasRejectedFirmwareUrl());
  p.beginPage();
  ASSERT_TRUE(p.feed(rejected.data(), rejected.size()));
  ASSERT_TRUE(p.finishPage());
  EXPECT_TRUE(p.foundFirmware());
  EXPECT_FALSE(p.hasRejectedFirmwareUrl());
}
TEST(CrossTwinsReleaseList, NewestRejectedUrlNeverFallsBackToOlderFirmware) {
  auto rejected = crossTwinsRelease("v1.6.5-ct.3", "2026-10-11T01:58:11Z");
  rejected.replace(rejected.find("meat-holic"), strlen("meat-holic"), "chohoyeon-ops");
  const auto body = "[" + crossTwinsRelease("v1.6.5-ct.2", "2026-10-10T01:58:11Z") + "," + rejected + "]";
  ReleaseListParser p;
  ASSERT_TRUE(p.feed(body.data(), body.size()));
  ASSERT_TRUE(p.finishPage());
  EXPECT_STREQ(p.getTagName(), "v1.6.5-ct.3");
  EXPECT_FALSE(p.foundFirmware());
  EXPECT_TRUE(p.hasRejectedFirmwareUrl());
}
TEST(CrossTwinsReleaseList, EmptyFirmwareUrlIsMalformedResponse) {
  auto body = "[" + crossTwinsRelease("v1.6.5-ct.3", "2026-10-11T01:58:11Z") + "]";
  const auto start = body.find("https://github.com/");
  body.erase(start, body.find('"', start) - start);
  ReleaseListParser p;
  EXPECT_FALSE(p.feed(body.data(), body.size()) && p.finishPage());
}
TEST(CrossTwinsFirmwareVersion, OtaTestBuildIsBelowCt3AndEqualToCt2) {
  EXPECT_EQ(compareFirmwareVersions("v1.6.5-ct.3", "1.6.5-ct.2+ota-test"), FirmwareVersionOrder::Newer);
  EXPECT_EQ(compareFirmwareVersions("v1.6.5-ct.2", "1.6.5-ct.2+ota-test"), FirmwareVersionOrder::Equal);
  EXPECT_EQ(compareFirmwareVersions("v1.6.5-ct.3", "1.6.5-ct.3+abc1234"), FirmwareVersionOrder::Equal);
  EXPECT_EQ(compareFirmwareVersions("v1.6.5-ct.3", "1.6.5-dev-master-108ac7b"), FirmwareVersionOrder::Older);
  char name[96]{};
  ASSERT_TRUE(makeX3X4AssetName("v1.6.5-ct.3+abc1234", name, sizeof(name)));
  EXPECT_STREQ(name, "crosspoint-1.6.5-ct.3-x3-x4.bin");
}

TEST(CrossTwinsReleaseList, SelectsMostRecentlyPublishedStableOrPrereleaseExcludesDraft) {
  const std::string body = "[" + crossTwinsRelease("v1.7.0", "2026-10-09T01:58:11Z", false) + "," +
                           crossTwinsRelease("v9.0.0", "2026-10-11T01:58:11Z", false, true) + "," +
                           crossTwinsRelease("v1.6.5-ct.1", "2026-10-10T01:58:11Z") + "]";
  ReleaseListParser p;
  ASSERT_TRUE(p.feed(body.data(), body.size()));
  ASSERT_TRUE(p.finishPage());
  EXPECT_EQ(p.pageCount(), 3u);
  EXPECT_STREQ(p.getTagName(), "v1.6.5-ct.1");
  ASSERT_TRUE(p.foundFirmware());
  EXPECT_STREQ(
      p.getFirmwareUrl(),
      "https://github.com/meat-holic/CrossTwins/releases/download/v1.6.5-ct.1/crosspoint-1.6.5-ct.1-x3-x4.bin");
  EXPECT_EQ(p.getFirmwareSize(), 4269216u);
}
TEST(CrossTwinsReleaseList, StableCanBeNewest) {
  const auto body = "[" + crossTwinsRelease("v1.6.5-ct.2", "2026-10-09T01:58:11Z") + "," +
                    crossTwinsRelease("v1.6.5", "2026-10-10T01:58:11Z", false) + "]";
  ReleaseListParser p;
  ASSERT_TRUE(p.feed(body.data(), body.size()));
  ASSERT_TRUE(p.finishPage());
  EXPECT_STREQ(p.getTagName(), "v1.6.5");
}
TEST(CrossTwinsReleaseList, SelectionPersistsAcrossPages) {
  ReleaseListParser p;
  for (const auto& body : {"[" + crossTwinsRelease("v1.6.5-ct.1", "2026-10-09T01:58:11Z") + "]",
                           "[" + crossTwinsRelease("v1.6.5-ct.2", "2026-10-10T01:58:11Z") + "]", std::string("[]")}) {
    p.beginPage();
    ASSERT_TRUE(p.feed(body.data(), body.size()));
    ASSERT_TRUE(p.finishPage());
  }
  EXPECT_EQ(p.pageCount(), 0u);
  EXPECT_STREQ(p.getTagName(), "v1.6.5-ct.2");
}
TEST(CrossTwinsReleaseList, EmptyAndDraftOnlyHaveNoRelease) {
  for (const auto& body : {std::string("[]"), std::string("[{\"draft\":true,\"published_at\":null}]")}) {
    ReleaseListParser p;
    ASSERT_TRUE(p.feed(body.data(), body.size()));
    ASSERT_TRUE(p.finishPage());
    EXPECT_FALSE(p.foundRelease());
  }
}
TEST(CrossTwinsReleaseList, RejectsMalformedAndWrongShapeResponses) {
  for (const char* body : {"",
                           " ",
                           "{}",
                           "{\"message\":\"API rate limit exceeded\"}",
                           "<html>error</html>",
                           "[",
                           "[{}",
                           "[]garbage",
                           "[][]",
                           "[null]",
                           "[true]",
                           "[1]",
                           "[[]]",
                           "[{\"draft\":false,}]",
                           "[{\"draft\":false \"tag_name\":\"v1.0.0\"}]",
                           "[{\"draft\":false]}",
                           "[{\"draft\":tru}]",
                           "[{\"draft\":01}]",
                           "[{\"draft\":false,\"ignored\":1e}]",
                           "[{\"draft\":false,\"ignored\":\"\\q\"}]",
                           "[{\"draft\":false,\"ignored\":\"\\uXYZ0\"}]",
                           "[{\"draft\":false,\"ignored\":\"\n\"}]",
                           "[{\"draft\":false,\"draft\":true}]",
                           "[{\"draft\":\"false\"}]"}) {
    ReleaseListParser p;
    const bool fed = p.feed(body, strlen(body));
    EXPECT_FALSE(fed && p.finishPage()) << body;
  }
}
TEST(CrossTwinsReleaseList, MissingOrInvalidPublicationIsNotSkippedToOlderRelease) {
  for (const auto& date : {"", "2026-02-30T01:00:00Z", "2026-13-01T01:00:00Z", "2026-01-01T24:00:00Z", "yesterday"}) {
    const auto body = "[" + crossTwinsRelease("v1.6.5-ct.1", "2026-10-09T01:58:11Z") + "," +
                      crossTwinsRelease("v1.6.5-ct.2", date) + "]";
    ReleaseListParser p;
    EXPECT_FALSE(p.feed(body.data(), body.size()) && p.finishPage()) << date;
  }
}
TEST(CrossTwinsReleaseList, OnlyExactCombinedAssetAndRepositoryUrlAccepted) {
  for (const auto& name :
       {"crosspoint-1.6.5-ct.1-x4pro.bin", "crosspoint-1.6.5-ct.1-sticky.bin", "crosspoint-1.6.5-ct.1-x3-x4-merged.bin",
        "crosspoint-v1.6.5-ct.1-x3-x4.bin", "crosspoint-1.6.4-x3-x4.bin", "firmware.bin"}) {
    const auto body = "[" + crossTwinsRelease("v1.6.5-ct.1", "2026-10-10T01:58:11Z", true, false, name) + "]";
    ReleaseListParser p;
    ASSERT_TRUE(p.feed(body.data(), body.size()));
    ASSERT_TRUE(p.finishPage());
    EXPECT_FALSE(p.foundFirmware()) << name;
  }
  auto body = "[" + crossTwinsRelease("v1.6.5-ct.1", "2026-10-10T01:58:11Z") + "]";
  body.replace(body.find("https://github.com/"), strlen("https://github.com/"), "http://evil.example/");
  ReleaseListParser p;
  ASSERT_TRUE(p.feed(body.data(), body.size()));
  ASSERT_TRUE(p.finishPage());
  EXPECT_FALSE(p.foundFirmware());
}
TEST(CrossTwinsReleaseList, NoFallbackWhenNewestReleaseLacksAsset) {
  const auto body = "[" + crossTwinsRelease("v1.6.5-ct.1", "2026-10-09T01:58:11Z") + "," +
                    crossTwinsRelease("v1.6.5-ct.2", "2026-10-10T01:58:11Z", true, false, "firmware.bin") + "]";
  ReleaseListParser p;
  ASSERT_TRUE(p.feed(body.data(), body.size()));
  ASSERT_TRUE(p.finishPage());
  EXPECT_STREQ(p.getTagName(), "v1.6.5-ct.2");
  EXPECT_FALSE(p.foundFirmware());
  EXPECT_FALSE(p.hasRejectedFirmwareUrl());
}
TEST(CrossTwinsReleaseList, EveryChunkBoundaryAndLongIgnoredBody) {
  auto body = "[" + crossTwinsRelease("v1.6.5-ct.1", "2026-10-10T01:58:11Z") + "]";
  body.insert(2, "\"body\":\"" + std::string(4096, 'x') + "\\n\\uAC00\",");
  for (size_t split = 0; split <= body.size(); ++split) {
    ReleaseListParser p;
    ASSERT_TRUE(p.feed(body.data(), split)) << split;
    ASSERT_TRUE(p.feed(body.data() + split, body.size() - split)) << split;
    ASSERT_TRUE(p.finishPage()) << split;
    ASSERT_TRUE(p.foundFirmware()) << split;
  }
}
TEST(CrossTwinsReleaseList, TruncatedDocumentNeverCompletes) {
  const auto body = "[" + crossTwinsRelease("v1.6.5-ct.1", "2026-10-10T01:58:11Z") + "]";
  for (size_t len = 0; len < body.size(); ++len) {
    ReleaseListParser p;
    p.feed(body.data(), len);
    EXPECT_FALSE(p.finishPage()) << len;
  }
}
TEST(CrossTwinsReleaseList, RejectsOverlongCriticalFieldAndDuplicateField) {
  for (const auto& tag : {std::string(64, 'x'), std::string(600, 'x')}) {
    const auto body = "[{\"tag_name\":\"" + tag + "\",\"draft\":true}]";
    ReleaseListParser p;
    EXPECT_FALSE(p.feed(body.data(), body.size()) && p.finishPage());
  }
  auto body = "[" + crossTwinsRelease("v1.6.5-ct.1", "2026-10-10T01:58:11Z") + "]";
  body.insert(2, "\"tag_name\":\"v1.0.0\",");
  ReleaseListParser p;
  EXPECT_FALSE(p.feed(body.data(), body.size()) && p.finishPage());
}
TEST(CrossTwinsFirmwareVersion, SameReleaseBuildMetadataNeverReinstalls) {
  for (const char* current : {"1.6.5-ct.1", "1.6.5-ct.1+325dc82", "v1.6.5-ct.1+other.SHA"})
    EXPECT_EQ(compareFirmwareVersions("v1.6.5-ct.1", current), FirmwareVersionOrder::Equal);
  EXPECT_EQ(compareFirmwareVersions("v1.6.5+new", "1.6.5+old"), FirmwareVersionOrder::Equal);
}
TEST(CrossTwinsFirmwareVersion, SemanticOrdering) {
  for (const auto& pair : {std::pair{"v1.6.5-ct.2", "1.6.5-ct.1+SHA"},
                           {"1.6.5-ct.10", "1.6.5-ct.9"},
                           {"1.6.5", "1.6.5-ct.2"},
                           {"1.6.6-ct.1", "1.6.5"},
                           {"2.0.0", "1.99.99"},
                           {"1.7.0", "1.6.99"},
                           {"1.6.5-rc.2", "1.6.5-rc.1"},
                           {"1.6.5-rc.1", "1.6.5-rc"},
                           {"1.6.5-rc", "1.6.5-ct.1"},
                           {"1.6.5-beta", "1.6.5-1"},
                           {"999999999999999999999.0.0", "99999999999999999999.0.0"}}) {
    EXPECT_EQ(compareFirmwareVersions(pair.first, pair.second), FirmwareVersionOrder::Newer);
    EXPECT_EQ(compareFirmwareVersions(pair.second, pair.first), FirmwareVersionOrder::Older);
  }
  EXPECT_EQ(compareFirmwareVersions("1.6.5-ct.1", "1.6.5"), FirmwareVersionOrder::Older);
  EXPECT_EQ(compareFirmwareVersions("1.6.5-ct.1", "1.6.5-dev-master-325dc82"), FirmwareVersionOrder::Older);
}
TEST(CrossTwinsFirmwareVersion, InvalidInputsAreIncomparable) {
  for (const char* bad : {"", "v", "garbage", "1.6", "1.6.5rc", "01.6.5", "1.6.5.1", "1.6.5-", "1.6.5-ct.01",
                          "1.6.5-ct..1", "1.6.5+", "1.6.5+bad/SHA", "1.6.5+SHA+SHA", " 1.6.5"}) {
    EXPECT_EQ(compareFirmwareVersions(bad, "1.6.5"), FirmwareVersionOrder::Invalid) << bad;
    EXPECT_EQ(compareFirmwareVersions("1.6.5", bad), FirmwareVersionOrder::Invalid) << bad;
  }
}
TEST(CrossTwinsFirmwareVersion, AssetVersionStripsVAndBuildMetadata) {
  char name[96]{};
  ASSERT_TRUE(makeX3X4AssetName("v1.6.5-ct.1+325dc82", name, sizeof(name)));
  EXPECT_STREQ(name, "crosspoint-1.6.5-ct.1-x3-x4.bin");
  ASSERT_TRUE(makeX3X4AssetName("1.6.5", name, sizeof(name)));
  EXPECT_STREQ(name, "crosspoint-1.6.5-x3-x4.bin");
  EXPECT_FALSE(makeX3X4AssetName("v1.6.5-ct.1", name, 8));
  EXPECT_FALSE(makeX3X4AssetName("../1.6.5", name, sizeof(name)));
}
TEST(CrossTwinsReleaseList, RejectsNonObjectAssetEntries) {
  for (const char* entry : {"null", "false", "42", "\"bad\"", "[]"}) {
    auto body = "[" + crossTwinsRelease("v1.6.5-ct.1", "2026-10-10T01:58:11Z") + "]";
    body.insert(body.find("[", body.find("assets")) + 1, std::string(entry) + ",");
    ReleaseListParser p;
    EXPECT_FALSE(p.feed(body.data(), body.size()) && p.finishPage()) << entry;
  }
}
TEST(CrossTwinsReleaseList, DuplicateCombinedAssetsAreNotInstallable) {
  auto body = "[" + crossTwinsRelease("v1.6.5-ct.1", "2026-10-10T01:58:11Z") + "]";
  const size_t start = body.find("{", body.find("assets"));
  const size_t end = body.find("}", start);
  const auto duplicate = body.substr(start, end - start + 1);
  body.insert(end + 1, "," + duplicate);
  ReleaseListParser p;
  ASSERT_TRUE(p.feed(body.data(), body.size()));
  ASSERT_TRUE(p.finishPage());
  EXPECT_FALSE(p.foundFirmware());
}
TEST(CrossTwinsReleaseList, InvalidSizeAndMissingRequiredMetadataAreRejected) {
  const auto original = "[" + crossTwinsRelease("v1.6.5-ct.1", "2026-10-10T01:58:11Z") + "]";
  for (const char* size : {"-1", "1.5", "1e6", "18446744073709551616"}) {
    auto body = original;
    body.replace(body.find("4269216"), 7, size);
    ReleaseListParser p;
    EXPECT_FALSE(p.feed(body.data(), body.size()) && p.finishPage()) << size;
  }
  for (const char* field : {"draft", "prerelease", "tag_name", "published_at", "assets"}) {
    auto body = original;
    body.replace(body.find(field), strlen(field), "ignored_field");
    ReleaseListParser p;
    EXPECT_FALSE(p.feed(body.data(), body.size()) && p.finishPage()) << field;
  }
}
