#include <Epub/BookMetadataCache.h>
#include <Epub/Epub/EpubProgress.h>
#include <HalStorage.h>
#include <gtest/gtest.h>

#include <cstdint>
#include <string>
#include <utility>
#include <vector>

#include "util/BookProgress.h"

namespace {
using Bytes = std::vector<uint8_t>;

struct SpineData {
  std::string href;
  uint32_t cumulativeSize;
  int16_t tocIndex;
};

struct TocData {
  std::string title;
  std::string href;
  std::string anchor;
  uint8_t level;
  int16_t spineIndex;
};

struct BookBinFixture {
  Bytes bytes;
  uint32_t lutOffset = 0;
  uint32_t spineLutOffset = 0;
  uint32_t tocLutOffset = 0;
  std::vector<uint32_t> spineEntryOffsets;
  std::vector<uint32_t> tocEntryOffsets;
};

constexpr char BOOK_PATH[] = "sample.epub";
constexpr char CACHE_PATH[] = "/.crosspoint/epub_sample.epub";

void appendU16(Bytes& bytes, const uint16_t value) {
  bytes.push_back(static_cast<uint8_t>(value));
  bytes.push_back(static_cast<uint8_t>(value >> 8));
}

void appendU32(Bytes& bytes, const uint32_t value) {
  for (int i = 0; i < 4; ++i) bytes.push_back(static_cast<uint8_t>(value >> (i * 8)));
}

void putU32(Bytes& bytes, const size_t offset, const uint32_t value) {
  for (int i = 0; i < 4; ++i) bytes.at(offset + i) = static_cast<uint8_t>(value >> (i * 8));
}

void appendString(Bytes& bytes, const std::string& value) {
  appendU32(bytes, static_cast<uint32_t>(value.size()));
  bytes.insert(bytes.end(), value.begin(), value.end());
}

BookBinFixture makeBookBin(const std::vector<SpineData>& spines, const std::vector<TocData>& toc) {
  BookBinFixture fixture;
  Bytes& bytes = fixture.bytes;
  bytes.push_back(BookMetadataCache::BOOK_CACHE_VERSION);
  appendU32(bytes, 0);
  appendU16(bytes, static_cast<uint16_t>(spines.size()));
  appendU16(bytes, static_cast<uint16_t>(toc.size()));
  appendString(bytes, "Book");
  appendString(bytes, "Author");
  appendString(bytes, "ko");
  appendString(bytes, "cover.jpg");
  appendString(bytes, "");

  fixture.lutOffset = static_cast<uint32_t>(bytes.size());
  putU32(bytes, 1, fixture.lutOffset);
  fixture.spineLutOffset = static_cast<uint32_t>(bytes.size());
  bytes.resize(bytes.size() + (spines.size() + toc.size()) * sizeof(uint32_t), 0);
  fixture.tocLutOffset = fixture.spineLutOffset + static_cast<uint32_t>(spines.size() * sizeof(uint32_t));

  for (size_t i = 0; i < spines.size(); ++i) {
    fixture.spineEntryOffsets.push_back(static_cast<uint32_t>(bytes.size()));
    putU32(bytes, fixture.spineLutOffset + i * sizeof(uint32_t), fixture.spineEntryOffsets.back());
    appendString(bytes, spines[i].href);
    appendU32(bytes, spines[i].cumulativeSize);
    appendU16(bytes, static_cast<uint16_t>(spines[i].tocIndex));
  }

  for (size_t i = 0; i < toc.size(); ++i) {
    fixture.tocEntryOffsets.push_back(static_cast<uint32_t>(bytes.size()));
    putU32(bytes, fixture.tocLutOffset + i * sizeof(uint32_t), fixture.tocEntryOffsets.back());
    appendString(bytes, toc[i].title);
    appendString(bytes, toc[i].href);
    appendString(bytes, toc[i].anchor);
    bytes.push_back(toc[i].level);
    appendU16(bytes, static_cast<uint16_t>(toc[i].spineIndex));
  }
  return fixture;
}

BookBinFixture basicBook() {
  return makeBookBin({{"OPS/one.xhtml", 100, 0}, {"OPS/two.xhtml", 300, 0}},
                     {{"제1장 시작", "OPS/one.xhtml", "top", 0, 0}});
}

Bytes progressRecord(const uint16_t spine, const uint16_t page, const uint16_t total, const size_t length) {
  Bytes bytes;
  appendU16(bytes, spine);
  appendU16(bytes, page);
  if (length >= 6) appendU16(bytes, total);
  while (bytes.size() < length) bytes.push_back(0xA5);
  bytes.resize(length);
  return bytes;
}

BookProgressSnapshot loadSnapshot(const Bytes& record, BookBinFixture fixture, const bool includeChapter = true,
                                  const size_t progressReadLimit = SIZE_MAX) {
  Storage.clear();
  const std::string progressPath = std::string(CACHE_PATH) + "/progress.bin";
  Storage.put(progressPath, record);
  if (progressReadLimit != SIZE_MAX) Storage.setReadLimit(progressPath, progressReadLimit);
  Storage.put(std::string(CACHE_PATH) + "/book.bin", std::move(fixture.bytes));
  return loadBookProgressSnapshot(BOOK_PATH, includeChapter);
}
}  // namespace

TEST(BookProgressSnapshot, PreservesFourSixAndTenByteProgressMeaning) {
  EXPECT_EQ(loadSnapshot(progressRecord(1, 5, 10, 4), basicBook(), false).percent, 33);
  EXPECT_EQ(loadSnapshot(progressRecord(1, 5, 10, 6), basicBook(), false).percent, 67);
  EXPECT_EQ(loadSnapshot(progressRecord(1, 5, 10, 10), basicBook(), false).percent, 67);
  EXPECT_EQ(loadBookProgress(BOOK_PATH), 67);
}

TEST(BookProgressSnapshot, RejectsUnsupportedProgressLengthsAndKeepsUnsupportedTextBooksEmpty) {
  for (const size_t length : {0, 3, 5, 7, 9, 11, 14}) {
    EXPECT_EQ(loadSnapshot(progressRecord(1, 5, 10, length), basicBook(), false).percent, -1) << length;
  }

  Storage.clear();
  const BookProgressSnapshot txt = loadBookProgressSnapshot("/books/note.txt", true);
  EXPECT_EQ(txt.percent, -1);
  EXPECT_EQ(txt.chapterTitle[0], '\0');
  EXPECT_EQ(Storage.openCount, 0u);
  const BookProgressSnapshot markdown = loadBookProgressSnapshot("/books/note.md", true);
  EXPECT_EQ(markdown.percent, -1);
  EXPECT_EQ(Storage.openCount, 0u);
}

TEST(BookProgressSnapshot, RejectsShortReadsForSupportedProgressLengths) {
  EXPECT_EQ(loadSnapshot(progressRecord(1, 5, 10, 10), basicBook(), false, 6).percent, -1);
  EXPECT_EQ(loadSnapshot(progressRecord(1, 5, 10, 6), basicBook(), false, 4).percent, -1);
}

TEST(BookProgressSnapshot, CompletionAndUnknownChapterProgressKeepLegacySemantics) {
  const BookProgressSnapshot complete = loadSnapshot(progressRecord(2, 0, 10, 6), basicBook());
  EXPECT_EQ(complete.percent, 100);
  EXPECT_EQ(complete.chapterTitle[0], '\0');

  const BookProgressSnapshot sentinel = loadSnapshot(progressRecord(1, UINT16_MAX, 10, 6), basicBook(), false);
  EXPECT_EQ(sentinel.percent, 33);
  const BookProgressSnapshot noTotal = loadSnapshot(progressRecord(1, 9, 0, 4), basicBook(), false);
  EXPECT_EQ(noTotal.percent, 33);
}

TEST(BookProgressSnapshot, UsesAnEarlierSpineTocEntryForTheCurrentChapterTitle) {
  BookBinFixture fixture = makeBookBin({{"OPS/one.xhtml", 100, 0}, {"OPS/two.xhtml", 300, 0}},
                                       {{"Earlier Chapter", "OPS/one.xhtml", "", 0, 0}});
  const BookProgressSnapshot snapshot = loadSnapshot(progressRecord(1, 5, 10, 6), std::move(fixture));

  EXPECT_EQ(snapshot.percent, 67);
  EXPECT_STREQ(snapshot.chapterTitle, "Earlier Chapter");
}

TEST(BookProgressSnapshot, SkipsUnmatchedTocEntriesAndUsesTheSelectedChapter) {
  BookBinFixture fixture = makeBookBin({{"OPS/one.xhtml", 100, 1}}, {{"Table of Contents", "toc.xhtml", "", 0, -1},
                                                                     {"Chapter 1", "OPS/one.xhtml", "", 0, 0}});
  const BookProgressSnapshot snapshot = loadSnapshot(progressRecord(0, 5, 10, 6), std::move(fixture));

  EXPECT_EQ(snapshot.percent, 50);
  EXPECT_STREQ(snapshot.chapterTitle, "Chapter 1");
}

TEST(BookProgressSnapshot, UsesFileEndAsTheLastSpineBoundaryWhenThereIsNoToc) {
  const BookBinFixture fixture = makeBookBin({{"OPS/one.xhtml", 100, -1}, {"OPS/two.xhtml", 300, -1}}, {});
  EXPECT_EQ(loadSnapshot(progressRecord(1, 5, 10, 6), fixture, false).percent, 67);
}

TEST(BookProgressSnapshot, InvalidHeaderSpineLutTruncationAndCumulativeSizesRejectPercent) {
  auto invalidHeader = basicBook();
  invalidHeader.bytes[0] = 9;
  EXPECT_EQ(loadSnapshot(progressRecord(1, 5, 10, 6), std::move(invalidHeader)).percent, -1);

  auto invalidLut = basicBook();
  putU32(invalidLut.bytes, invalidLut.spineLutOffset + sizeof(uint32_t), 0xFFFFFFFF);
  EXPECT_EQ(loadSnapshot(progressRecord(1, 5, 10, 6), std::move(invalidLut)).percent, -1);

  auto truncated = basicBook();
  truncated.bytes.resize(truncated.spineEntryOffsets.back() + 7);
  EXPECT_EQ(loadSnapshot(progressRecord(1, 5, 10, 6), std::move(truncated)).percent, -1);

  EXPECT_EQ(loadSnapshot(progressRecord(3, 5, 10, 6), basicBook()).percent, -1);

  auto decreasing = basicBook();
  const size_t lastCumulativeOffset = decreasing.spineEntryOffsets.back() + 4 + std::string("OPS/two.xhtml").size();
  putU32(decreasing.bytes, lastCumulativeOffset, 50);
  EXPECT_EQ(loadSnapshot(progressRecord(1, 5, 10, 6), std::move(decreasing)).percent, -1);
}

TEST(BookProgressSnapshot, RejectsSpineRecordsThatCrossTheirNextEntryBoundary) {
  auto overlap = makeBookBin({{"OPS/one.xhtml", 100, 0}, {std::string(300, 'x'), 400, 0}, {"OPS/three.xhtml", 500, 0}},
                             {{"Chapter", "OPS/one.xhtml", "", 0, 0}});
  putU32(overlap.bytes, overlap.spineEntryOffsets[0], static_cast<uint32_t>(std::string("OPS/one.xhtml").size() + 6));

  EXPECT_EQ(loadSnapshot(progressRecord(0, 5, 10, 6), std::move(overlap), false).percent, -1);
}

TEST(BookProgressSnapshot, RejectsLastSpineRecordsThatCrossTheFirstTocBoundary) {
  auto overlap = makeBookBin({{"OPS/one.xhtml", 100, 0}, {"OPS/two.xhtml", 300, 0}},
                             {{std::string(300, 'x'), "OPS/one.xhtml", "", 0, 0}});
  putU32(overlap.bytes, overlap.spineEntryOffsets.back(),
         static_cast<uint32_t>(std::string("OPS/two.xhtml").size() + 6));

  EXPECT_EQ(loadSnapshot(progressRecord(1, 5, 10, 6), std::move(overlap), false).percent, -1);
}

TEST(BookProgressSnapshot, KeepsProgressWhenFirstTocPointerFallsInsideLastSpine) {
  auto ambiguousToc = basicBook();
  putU32(ambiguousToc.bytes, ambiguousToc.tocLutOffset,
         static_cast<uint32_t>(ambiguousToc.spineEntryOffsets.back() + 1));

  const BookProgressSnapshot snapshot = loadSnapshot(progressRecord(1, 5, 10, 6), std::move(ambiguousToc));
  EXPECT_EQ(snapshot.percent, 67);
  EXPECT_EQ(snapshot.chapterTitle[0], '\0');
}

TEST(BookProgressSnapshot, BrokenOptionalTocDoesNotDiscardValidPercentage) {
  auto invalidTocLut = basicBook();
  putU32(invalidTocLut.bytes, invalidTocLut.tocLutOffset, 0xFFFFFFFF);
  BookProgressSnapshot snapshot = loadSnapshot(progressRecord(1, 5, 10, 6), std::move(invalidTocLut));
  EXPECT_EQ(snapshot.percent, 67);
  EXPECT_EQ(snapshot.chapterTitle[0], '\0');

  auto invalidNextTocLut = makeBookBin({{"one.xhtml", 100, 0}, {"two.xhtml", 300, 0}},
                                       {{"First", "one.xhtml", "", 0, 0}, {"Next", "two.xhtml", "", 0, 0}});
  putU32(invalidNextTocLut.bytes, invalidNextTocLut.tocLutOffset + sizeof(uint32_t), 0xFFFFFFFF);
  snapshot = loadSnapshot(progressRecord(1, 5, 10, 6), std::move(invalidNextTocLut));
  EXPECT_EQ(snapshot.percent, 67);
  EXPECT_EQ(snapshot.chapterTitle[0], '\0');

  auto malformedToc = basicBook();
  putU32(malformedToc.bytes, malformedToc.tocEntryOffsets[0], 0xFFFFFFFF);
  snapshot = loadSnapshot(progressRecord(1, 5, 10, 6), std::move(malformedToc));
  EXPECT_EQ(snapshot.percent, 67);
  EXPECT_EQ(snapshot.chapterTitle[0], '\0');

  auto malformedUtf8 = makeBookBin({{"one.xhtml", 100, 0}, {"two.xhtml", 300, 0}},
                                   {{std::string("\xC0\xAF", 2), "one.xhtml", "", 0, 0}});
  snapshot = loadSnapshot(progressRecord(1, 5, 10, 6), std::move(malformedUtf8));
  EXPECT_EQ(snapshot.percent, 67);
  EXPECT_EQ(snapshot.chapterTitle[0], '\0');

  auto overlongTitle =
      makeBookBin({{"one.xhtml", 100, 0}, {"two.xhtml", 300, 0}}, {{std::string(300, 'x'), "one.xhtml", "", 0, 0}});
  snapshot = loadSnapshot(progressRecord(1, 5, 10, 6), std::move(overlongTitle));
  EXPECT_EQ(snapshot.percent, 67);
  EXPECT_EQ(snapshot.chapterTitle[0], '\0');
}

TEST(BookProgressSnapshot, OmitsATocTitleThatCrossesTheNextTocEntryBoundary) {
  BookBinFixture overlap = makeBookBin({{"OPS/one.xhtml", 100, 0}, {"OPS/two.xhtml", 300, 0}},
                                       {{"First", "OPS/one.xhtml", "", 0, 0}, {"Next", "OPS/two.xhtml", "", 0, 0}});
  const size_t titleLength = overlap.tocEntryOffsets[1] - overlap.tocEntryOffsets[0] - sizeof(uint32_t);
  putU32(overlap.bytes, overlap.tocEntryOffsets[0], static_cast<uint32_t>(titleLength));

  const BookProgressSnapshot snapshot = loadSnapshot(progressRecord(1, 5, 10, 6), std::move(overlap));
  EXPECT_EQ(snapshot.percent, 67);
  EXPECT_EQ(snapshot.chapterTitle[0], '\0');
}

TEST(BookProgressSnapshot, ReadsAConstantNumberOfSpineRecordsForLargeBooks) {
  std::vector<SpineData> spines;
  spines.reserve(2000);
  for (int i = 0; i < 2000; ++i) {
    spines.push_back({"OPS/chapter" + std::to_string(i) + ".xhtml", static_cast<uint32_t>((i + 1) * 10), 0});
  }
  BookBinFixture fixture = makeBookBin(spines, {{"Chapter 1", "OPS/chapter0.xhtml", "", 0, 0}});
  const BookProgressSnapshot snapshot = loadSnapshot(progressRecord(1000, 50, 100, 6), std::move(fixture));

  EXPECT_GT(snapshot.percent, 0);
  EXPECT_LT(snapshot.percent, 100);
  EXPECT_EQ(std::string(snapshot.chapterTitle), "Chapter 1");
  EXPECT_LT(BookMetadataCache::lastBookBinBytesRead, 200u);
  EXPECT_LT(BookMetadataCache::lastBookBinReadCalls, 32u);
}

TEST(EpubProgress, SharedCumulativeFormulaMatchesReaderProgress) {
  EXPECT_FLOAT_EQ(epub::calculateProgressFromCumulative(100, 300, 300, 0.5f), 2.0f / 3.0f);
  EXPECT_FLOAT_EQ(epub::calculateProgressFromCumulative(100, 300, 0, 0.5f), 0.0f);
}
