#include <Epub/Page.h>
#include <GfxRenderer.h>
#include <gtest/gtest.h>

#include <filesystem>
#include <memory>
#include <string>
#include <tuple>
#include <vector>

#define class struct
#define private public
#include "Epub/parsers/ChapterHtmlSlimParser.h"
#undef private
#undef class

namespace {
using Line = std::tuple<int, int, std::vector<std::pair<std::string, int>>>;
std::vector<Line> snapshot(const Page& page) {
  std::vector<Line> result;
  for (const auto& element : page.elements) {
    if (element->getTag() != TAG_PageLine) continue;
    const auto& line = static_cast<const PageLine&>(*element);
    std::vector<std::pair<std::string, int>> words;
    for (uint16_t i = 0; i < line.getBlock()->wordCount(); ++i)
      words.emplace_back(line.getBlock()->wordText(i), line.getBlock()->wordXpos(i));
    result.emplace_back(line.xPos, line.yPos, std::move(words));
  }
  return result;
}
struct Fixture {
  GfxRenderer renderer;
  std::string filepath = "unused.xhtml";
  ChapterHtmlSlimParser parser{
      nullptr, filepath, renderer, 0,     1.0f, true, static_cast<uint8_t>(CssTextAlign::Left), 56, 30000,
      false,   false,    {},       false, "",   ""};
  Fixture(bool css = false, bool hyphen = false, CssTextAlign align = CssTextAlign::Left) {
    parser.insideBody = true;
    parser.embeddedStyle = css;
    parser.hyphenationEnabled = hyphen;
    parser.setParagraphIndentSpaces(4);
    parser.setTextSpacing(-1, 150);
    BlockStyle root;
    root.alignment = align;
    root.textAlignDefined = true;
    parser.blockStyleStack.push_back(root);
    ChapterHtmlSlimParser::startElement(&parser, "p", nullptr);
    BlockStyle style;
    style.alignment = align;
    style.marginTop = 17;
    style.paddingTop = 3;
    style.marginBottom = 7;
    style.paddingBottom = 5;
    parser.currentTextBlock = std::make_unique<ParsedText>(hyphen, false, style, 4);
  }
  void feed(size_t count, size_t batch, const std::string& word = "word") {
    for (size_t start = 0; start < count; start += batch) {
      std::string text;
      for (size_t i = start; i < count && i < start + batch; ++i) text += word + " ";
      ChapterHtmlSlimParser::characterData(&parser, text.data(), static_cast<int>(text.size()));
    }
  }
  void finish() {
    parser.flushPartWordBuffer();
    parser.makePages();
  }
};
TEST(EpubSoftFlush, ThresholdEdgesAndMultipleFlushesIndentAndSpaceOnlyOnce) {
  for (bool css : {false, true}) {
    const size_t threshold = css ? 320 : 750;
    for (size_t count : {threshold - 1, threshold, threshold + 1, 2 * threshold + 19}) {
      Fixture f(css);
      f.feed(count, 1);
      EXPECT_EQ(f.parser.wordsExtractedInBlock > 0, count > threshold);
      f.finish();
      ASSERT_NE(f.parser.currentPage, nullptr);
      const auto lines = snapshot(*f.parser.currentPage);
      ASSERT_EQ(lines.size(), count);
      for (size_t i = 0; i < lines.size(); ++i) {
        EXPECT_EQ(std::get<1>(lines[i]), 20 + 16 * i) << "css=" << css << " words=" << count << " line=" << i;
        ASSERT_EQ(std::get<2>(lines[i]).size(), 1u);
        EXPECT_EQ(std::get<2>(lines[i])[0].second, i == 0 ? 24 : 0) << "line=" << i;
      }
      EXPECT_EQ(f.parser.currentPageNextY, 20 + 16 * count + 12 + 8);
    }
  }
}
TEST(EpubSoftFlush, KoreanCjkAndHyphenationKeepLayoutAcrossDifferentBatchBoundaries) {
  for (const std::string word : {"word", "한국어", "漢字漢字"}) {
    for (bool hyphen : {false, true}) {
      Fixture one(false, hyphen), frequent(false, hyphen), different(false, hyphen);
      // One final callback versus repeated production flushes at different positions.
      one.feed(1555, 1555, word);
      frequent.feed(1555, 1, word);
      different.feed(1555, 37, word);
      one.finish();
      frequent.finish();
      different.finish();
      EXPECT_EQ(snapshot(*one.parser.currentPage), snapshot(*frequent.parser.currentPage));
      EXPECT_EQ(snapshot(*one.parser.currentPage), snapshot(*different.parser.currentPage));
    }
  }
}
TEST(EpubSoftFlush, FollowingParagraphAndFreshParserHaveTheirOwnFirstLine) {
  Fixture f;
  f.feed(800, 1);
  // Let the real close-element path finalize the retained line and replace the block.
  ChapterHtmlSlimParser::endElement(&f.parser, "p");
  ASSERT_TRUE(f.parser.currentTextBlock->isEmpty());
  const int before = f.parser.currentPageNextY;
  ChapterHtmlSlimParser::startElement(&f.parser, "p", nullptr);
  f.feed(1, 1);
  f.finish();
  const auto lines = snapshot(*f.parser.currentPage);
  EXPECT_EQ(std::get<2>(lines.back())[0].second, 24);
  EXPECT_GE(std::get<1>(lines.back()), before);
  Fixture fresh;
  fresh.feed(1, 1);
  fresh.finish();
  EXPECT_EQ(std::get<2>(snapshot(*fresh.parser.currentPage)[0])[0].second, 24);
}
TEST(EpubSoftFlush, NonNaturalAlignmentRemainsUnindented) {
  Fixture f(false, false, CssTextAlign::Center);
  f.feed(800, 1);
  f.finish();
  // 29 px tracked word centered in 56 px; paragraph-indent setting is irrelevant.
  for (const auto& line : snapshot(*f.parser.currentPage)) EXPECT_EQ(std::get<2>(line)[0].second, 13);
}
TEST(EpubSoftFlush, DisabledParagraphSpacingStaysDisabledAcrossFlushes) {
  Fixture f;
  f.parser.extraParagraphSpacing = false;
  f.feed(1555, 1);
  f.finish();
  EXPECT_EQ(f.parser.currentPageNextY, 20 + 16 * 1555 + 12);
}
TEST(EpubSoftFlush, PageBreaksAndVisibleOffsetsDoNotDependOnBatchPositions) {
  using Pages = std::vector<std::pair<uint32_t, std::vector<Line>>>;
  auto parse = [](size_t batch) {
    Fixture f;
    f.parser.viewportHeight = 128;
    Pages pages;
    f.parser.completePageFn = [&](std::unique_ptr<Page> page, auto, auto, uint32_t offset) {
      pages.emplace_back(offset, snapshot(*page));
    };
    f.feed(1555, batch);
    f.finish();
    pages.emplace_back(f.parser.currentPageVisibleOffset, snapshot(*f.parser.currentPage));
    EXPECT_GT(pages.size(), 1u);
    EXPECT_EQ(std::get<2>(pages[0].second[0])[0].second, 24);
    for (size_t i = 1; i < pages.size(); ++i) {
      EXPECT_GT(pages[i].first, pages[i - 1].first);
      for (const auto& line : pages[i].second) EXPECT_EQ(std::get<2>(line)[0].second, 0);
    }
    return pages;
  };
  EXPECT_EQ(parse(1), parse(37));
  EXPECT_EQ(parse(1), parse(1555));
}
TEST(EpubSoftFlush, FutureFootnotesAndTrailingSpacingWaitForLogicalEnd) {
  Fixture f(true);
  FootnoteEntry first, future;
  std::strcpy(first.number, "1");
  std::strcpy(first.href, "#first");
  std::strcpy(future.number, "2");
  std::strcpy(future.href, "#last");
  f.parser.pendingFootnotes.emplace_back(10, first);
  f.parser.pendingFootnotes.emplace_back(999, future);
  f.feed(321, 1);
  ASSERT_NE(f.parser.currentPage, nullptr);
  EXPECT_EQ(f.parser.currentPage->footnotes.size(), 1u);
  EXPECT_EQ(f.parser.pendingFootnotes.size(), 1u);
  EXPECT_EQ(f.parser.currentPageNextY, 20 + 16 * f.parser.wordsExtractedInBlock);
  f.finish();
  EXPECT_TRUE(f.parser.pendingFootnotes.empty());
  EXPECT_EQ(f.parser.currentPage->footnotes.size(), 2u);
  EXPECT_EQ(f.parser.currentPageNextY, 20 + 16 * 321 + 12 + 8);
}
TEST(EpubSoftFlush, ColdAndSerializedWarmPageHaveIdenticalLayoutAndFootnotes) {
  Fixture f;
  FootnoteEntry note;
  std::strcpy(note.number, "1");
  std::strcpy(note.href, "#cached-note");
  f.parser.pendingFootnotes.emplace_back(10, note);
  f.feed(800, 1, "한국어");
  f.finish();
  const auto cold = snapshot(*f.parser.currentPage);
  const auto path = (std::filesystem::temp_directory_path() / "crosspoint-soft-flush-page.bin").string();
  {
    HalFile file;
    ASSERT_TRUE(file.open(path.c_str(), "wb"));
    ASSERT_TRUE(f.parser.currentPage->serialize(file));
  }
  HalFile file;
  ASSERT_TRUE(file.open(path.c_str(), "rb"));
  auto warm = Page::deserialize(file);
  ASSERT_NE(warm, nullptr);
  EXPECT_EQ(snapshot(*warm), cold);
  EXPECT_EQ(warm->footnotes.size(), f.parser.currentPage->footnotes.size());
  ASSERT_EQ(warm->footnotes.size(), 1u);
  EXPECT_STREQ(warm->footnotes[0].href, "#cached-note");
  file.close();
  std::filesystem::remove(path);
}
}  // namespace
