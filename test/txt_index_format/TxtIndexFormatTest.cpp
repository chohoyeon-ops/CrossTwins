#include <gtest/gtest.h>

#include <TxtIndexFormat.h>
#include <TxtPageLayout.h>

#include <array>
#include <cstdint>
#include <vector>

namespace {
std::vector<uint8_t> makeHeader(uint8_t version, uint32_t fileSize, uint32_t pageCount) {
  const size_t headerSize = txt::headerSizeForVersion(version);
  std::vector<uint8_t> data(headerSize, 0);
  txt::writeLe32(data.data(), txt::INDEX_MAGIC);
  data[4] = version;
  txt::writeLe32(data.data() + 5, fileSize);
  txt::writeLe32(data.data() + 9, 420);
  txt::writeLe32(data.data() + 13, 24);
  txt::writeLe32(data.data() + 17, 17);
  txt::writeLe32(data.data() + 21, 12);
  data[25] = 2;
  if (version == txt::INDEX_VERSION_V5 || version == txt::INDEX_VERSION_V6 || version == txt::INDEX_VERSION_V7 ||
      version == txt::INDEX_VERSION_V8) {
    txt::writeLe32(data.data() + 26, 20);
    data[30] = 18;
    if (version == txt::INDEX_VERSION_V6 || version == txt::INDEX_VERSION_V7 || version == txt::INDEX_VERSION_V8) {
      txt::writeLe32(data.data() + 31, 480);
      txt::writeLe32(data.data() + 35, 10);
      txt::writeLe32(data.data() + 39, 20);
      data[43] = 1;
      data[44] = 2;
      txt::writeLe32(data.data() + 45, pageCount);
    } else {
      txt::writeLe32(data.data() + 31, pageCount);
    }
  } else {
    txt::writeLe32(data.data() + 26, pageCount);
  }
  return data;
}

bool parseWithPageStorage(const std::vector<uint8_t>& headerBytes, uint32_t pageCount, txt::IndexHeader& header) {
  return txt::parseIndexHeader(headerBytes.data(), headerBytes.size(),
                               headerBytes.size() + static_cast<size_t>(pageCount) * sizeof(uint32_t), header);
}
}  // namespace

TEST(TxtIndexFormat, ParsesV3ThroughV8PageCountLocations) {
  for (const uint8_t version : {txt::INDEX_VERSION_V3, txt::INDEX_VERSION_V4, txt::INDEX_VERSION_V5,
                                txt::INDEX_VERSION_V6, txt::INDEX_VERSION_V7, txt::INDEX_VERSION_V8}) {
    const auto raw = makeHeader(version, 1000, 3);
    txt::IndexHeader header;
    ASSERT_TRUE(parseWithPageStorage(raw, 3, header));
    EXPECT_EQ(header.version, version);
    EXPECT_EQ(header.sourceFileSize, 1000u);
    EXPECT_EQ(header.pageCount, 3u);
    EXPECT_EQ(header.headerSize, txt::headerSizeForVersion(version));
    if (version == txt::INDEX_VERSION_V5 || version == txt::INDEX_VERSION_V6 ||
        version == txt::INDEX_VERSION_V7 || version == txt::INDEX_VERSION_V8) {
      EXPECT_EQ(header.lineAdvance, 20);
      EXPECT_EQ(header.fontPointSize, 18);
    }
    if (version == txt::INDEX_VERSION_V6 || version == txt::INDEX_VERSION_V7 ||
        version == txt::INDEX_VERSION_V8) {
      EXPECT_EQ(header.viewportHeight, 480);
      EXPECT_EQ(header.paragraphGap, 10);
      EXPECT_EQ(header.paragraphIndentWidth, 20);
      EXPECT_EQ(header.extraParagraphSpacing, 1);
      EXPECT_EQ(header.paragraphIndentSpaces, 2);
    }
  }
}

TEST(TxtIndexFormat, RejectsTruncatedHeadersAndOffsetTables) {
  auto v5 = makeHeader(txt::INDEX_VERSION_V5, 1000, 2);
  txt::IndexHeader header;
  EXPECT_FALSE(txt::parseIndexHeader(v5.data(), v5.size() - 1, v5.size() + 8, header));
  EXPECT_FALSE(txt::parseIndexHeader(v5.data(), v5.size(), v5.size() + 4, header));

  auto v6 = makeHeader(txt::INDEX_VERSION_V6, 1000, 2);
  EXPECT_FALSE(txt::parseIndexHeader(v6.data(), v6.size() - 1, v6.size() + 8, header));
  EXPECT_FALSE(txt::parseIndexHeader(v6.data(), v6.size(), v6.size() + 4, header));

  auto v8 = makeHeader(txt::INDEX_VERSION_V8, 1000, 2);
  EXPECT_FALSE(txt::parseIndexHeader(v8.data(), v8.size() - 1, v8.size() + 8, header));
  EXPECT_FALSE(txt::parseIndexHeader(v8.data(), v8.size(), v8.size() + 4, header));

  auto v4 = makeHeader(txt::INDEX_VERSION_V4, 1000, 2);
  EXPECT_FALSE(txt::parseIndexHeader(v4.data(), v4.size(), v4.size() + 4, header));
}

TEST(TxtIndexFormat, RejectsInvalidMagicVersionAndPageBounds) {
  auto raw = makeHeader(txt::INDEX_VERSION_V6, 1000, 2);
  txt::IndexHeader header;
  raw[0] ^= 1;
  EXPECT_FALSE(parseWithPageStorage(raw, 2, header));

  raw = makeHeader(txt::INDEX_VERSION_V5, 1000, 2);
  raw[4] = 9;
  EXPECT_FALSE(parseWithPageStorage(raw, 2, header));

  raw = makeHeader(txt::INDEX_VERSION_V6, 1000, 0);
  EXPECT_FALSE(parseWithPageStorage(raw, 0, header));

  raw = makeHeader(txt::INDEX_VERSION_V6, 1, 3);
  EXPECT_FALSE(parseWithPageStorage(raw, 3, header));
}

TEST(TxtIndexFormat, LayoutIdentityInvalidatesGeometryTypographyAndEffectiveSpacing) {
  const auto raw = makeHeader(txt::INDEX_VERSION_V8, 1000, 3);
  txt::IndexHeader header;
  ASSERT_TRUE(parseWithPageStorage(raw, 3, header));
  const txt::LayoutKey key{420, 24, 17, 12, 2, 20, 18, 480, 10, 20, 1, 2};
  EXPECT_TRUE(txt::matchesLayout(header, key));
  EXPECT_FALSE(txt::matchesLayout(header, {419, 24, 17, 12, 2, 20, 18, 480, 10, 20, 1, 2}));
  EXPECT_FALSE(txt::matchesLayout(header, {420, 23, 17, 12, 2, 20, 18, 480, 10, 20, 1, 2}));
  EXPECT_FALSE(txt::matchesLayout(header, {420, 24, 18, 12, 2, 20, 18, 480, 10, 20, 1, 2}));
  EXPECT_FALSE(txt::matchesLayout(header, {420, 24, 17, 13, 2, 20, 18, 480, 10, 20, 1, 2}));
  EXPECT_FALSE(txt::matchesLayout(header, {420, 24, 17, 12, 2, 21, 18, 480, 10, 20, 1, 2}));
  EXPECT_FALSE(txt::matchesLayout(header, {420, 24, 17, 12, 2, 20, 20, 480, 10, 20, 1, 2}));
  EXPECT_FALSE(txt::matchesLayout(header, {420, 24, 17, 12, 2, 20, 18, 479, 10, 20, 1, 2}));
  EXPECT_FALSE(txt::matchesLayout(header, {420, 24, 17, 12, 2, 20, 18, 480, 11, 20, 1, 2}));
  EXPECT_FALSE(txt::matchesLayout(header, {420, 24, 17, 12, 2, 20, 18, 480, 10, 21, 1, 2}));
  EXPECT_FALSE(txt::matchesLayout(header, {420, 24, 17, 12, 2, 20, 18, 480, 10, 20, 0, 2}));
  EXPECT_FALSE(txt::matchesLayout(header, {420, 24, 17, 12, 2, 20, 18, 480, 10, 20, 1, 3}));
  const auto oldRaw = makeHeader(txt::INDEX_VERSION_V5, 1000, 3);
  txt::IndexHeader oldHeader;
  ASSERT_TRUE(parseWithPageStorage(oldRaw, 3, oldHeader));
  EXPECT_FALSE(txt::matchesLayout(oldHeader, key));

  const auto phase3Raw = makeHeader(txt::INDEX_VERSION_V6, 1000, 3);
  txt::IndexHeader phase3Header;
  ASSERT_TRUE(parseWithPageStorage(phase3Raw, 3, phase3Header));
  EXPECT_FALSE(txt::matchesLayout(phase3Header, key));

  const auto phase45Raw = makeHeader(txt::INDEX_VERSION_V7, 1000, 3);
  txt::IndexHeader phase45Header;
  ASSERT_TRUE(parseWithPageStorage(phase45Raw, 3, phase45Header));
  EXPECT_FALSE(txt::matchesLayout(phase45Header, key));
}

TEST(TxtIndexFormat, LegacyV6AndV7IndexesAreStaleButSupportProgressMigration) {
  const txt::LayoutKey currentLayout{420, 24, 17, 12, 2, 20, 18, 480, 10, 20, 1, 2};
  const std::array<uint32_t, 4> offsets{0, 10, 20, 30};
  const auto readOffset = [&offsets](uint32_t page, uint32_t& offset) {
    if (page >= offsets.size()) return false;
    offset = offsets[page];
    return true;
  };
  for (const uint8_t version : {txt::INDEX_VERSION_V6, txt::INDEX_VERSION_V7}) {
    const auto raw = makeHeader(version, 40, 4);
    txt::IndexHeader header;
    ASSERT_TRUE(parseWithPageStorage(raw, 4, header));
    EXPECT_FALSE(txt::matchesLayout(header, currentLayout));

    uint32_t anchor = 0;
    ASSERT_TRUE(txt::deriveLegacyPageAnchor(
        header, 2, 40, readOffset,
        [](uint32_t offset) { return offset == 0 || offset == 10 || offset == 20 || offset == 30; }, anchor));
    EXPECT_EQ(anchor, 20u);
  }
}

TEST(TxtIndexFormat, KeepsLegacyProgressPrefixAndAddsValidatedSourceAnchor) {
  uint8_t encoded[8];
  txt::writeProgressRecord(encoded, 0x1234, 0x10203040);
  EXPECT_EQ(encoded[0], 0x34);
  EXPECT_EQ(encoded[1], 0x12);
  EXPECT_EQ(encoded[2], 0);
  EXPECT_EQ(encoded[3], 0);

  txt::ProgressRecord record;
  ASSERT_TRUE(txt::parseProgressRecord(encoded, 4, 5000, record));
  EXPECT_EQ(record.page, 0x1234);
  EXPECT_FALSE(record.hasAnchorRecord);
  EXPECT_FALSE(record.hasSourceAnchor);

  ASSERT_TRUE(txt::parseProgressRecord(encoded, sizeof(encoded), 0x10203040, record));
  EXPECT_TRUE(record.hasAnchorRecord);
  EXPECT_TRUE(record.hasSourceAnchor);
  EXPECT_EQ(record.sourceAnchor, 0x10203040u);

  EXPECT_TRUE(txt::parseProgressRecord(encoded, sizeof(encoded), 100, record));
  EXPECT_TRUE(record.hasAnchorRecord);
  EXPECT_FALSE(record.hasSourceAnchor);
}

TEST(TxtIndexFormat, ReopensWithSourceAnchorAfterPageOffsetsChange) {
  uint8_t encoded[8];
  txt::writeProgressRecord(encoded, 1, 100);
  txt::ProgressRecord progress;
  ASSERT_TRUE(txt::parseProgressRecord(encoded, sizeof(encoded), 500, progress));
  ASSERT_TRUE(progress.hasSourceAnchor);

  const std::vector<size_t> rebuiltPageOffsets{0, 70, 130, 190};
  EXPECT_EQ(txt::PageLayout::pageForSourceAnchor(rebuiltPageOffsets, progress.sourceAnchor), 1u);
}

TEST(TxtIndexFormat, MigratesPageProgressUsingValidatedV3ThroughV7PageOffsets) {
  const std::array<uint32_t, 4> offsets{0, 10, 20, 30};
  const auto readOffset = [&offsets](uint32_t page, uint32_t& offset) {
    if (page >= offsets.size()) return false;
    offset = offsets[page];
    return true;
  };
  const auto isBoundary = [](uint32_t offset) { return offset == 0 || offset == 10 || offset == 20 || offset == 30; };
  const uint8_t legacyProgress[4] = {2, 0, 0, 0};
  txt::ProgressRecord progress;
  ASSERT_TRUE(txt::parseProgressRecord(legacyProgress, sizeof(legacyProgress), 40, progress));
  EXPECT_FALSE(progress.hasAnchorRecord);

  for (const uint8_t version : {txt::INDEX_VERSION_V3, txt::INDEX_VERSION_V4, txt::INDEX_VERSION_V5,
                                txt::INDEX_VERSION_V6, txt::INDEX_VERSION_V7}) {
    const auto raw = makeHeader(version, 40, offsets.size());
    txt::IndexHeader header;
    ASSERT_TRUE(parseWithPageStorage(raw, offsets.size(), header));
    uint32_t anchor = 0;
    EXPECT_TRUE(txt::deriveLegacyPageAnchor(header, progress.page, 40, readOffset, isBoundary, anchor));
    EXPECT_EQ(anchor, 20u);
    const std::vector<size_t> rebuiltPageOffsets{0, 17, 25, 35};
    EXPECT_EQ(txt::PageLayout::pageForSourceAnchor(rebuiltPageOffsets, anchor), 1u);
    EXPECT_FALSE(txt::deriveLegacyPageAnchor(header, 4, 40, readOffset, isBoundary, anchor));
    EXPECT_FALSE(txt::deriveLegacyPageAnchor(header, 2, 41, readOffset, isBoundary, anchor));
  }
}

TEST(TxtIndexFormat, RejectsLegacyAnchorsWithInvalidNeighborsOrUtf8Boundary) {
  auto raw = makeHeader(txt::INDEX_VERSION_V4, 40, 4);
  txt::IndexHeader header;
  ASSERT_TRUE(parseWithPageStorage(raw, 4, header));
  const std::array<uint32_t, 4> offsets{0, 10, 9, 30};
  const auto readOffset = [&offsets](uint32_t page, uint32_t& offset) {
    if (page >= offsets.size()) return false;
    offset = offsets[page];
    return true;
  };
  uint32_t anchor = 0;
  EXPECT_FALSE(txt::deriveLegacyPageAnchor(header, 1, 40, readOffset, [](uint32_t) { return true; }, anchor));

  const std::array<uint32_t, 4> ordered{0, 10, 20, 30};
  const auto readOrdered = [&ordered](uint32_t page, uint32_t& offset) {
    if (page >= ordered.size()) return false;
    offset = ordered[page];
    return true;
  };
  EXPECT_FALSE(txt::deriveLegacyPageAnchor(header, 2, 40, readOrdered, [](uint32_t) { return false; }, anchor));
}
