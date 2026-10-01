#include <GfxRenderer.h>
#include <FontCacheManager.h>
#include <TxtPageLayout.h>
#include <gtest/gtest.h>
#include <memory>
#include <FontPsram.h>
#include <Utf8.h>
#include <builtinFonts/pretendard_10_regular.h>
#include "FontFixture.h"

namespace {
constexpr int FONT_ID = 42;
struct GlyphPosition { uint32_t cp; int x; int advanceFP; };
struct Snapshot {
  int width = 0;
  int finalAdvance = 0;
  std::vector<GlyphPosition> glyphs;
  std::vector<txt::PageLine> lines;
  size_t nextOffset = 0;
};
class TxtRendererIntegration : public ::testing::Test {
 protected:
  SdCardFont font;
  std::unique_ptr<HalDisplay> display;
  std::unique_ptr<GfxRenderer> renderer;
  std::unique_ptr<FontCacheManager> cache;
  std::map<int, SdCardFont*> sdFonts;
  std::map<int, TtfEpdFont*> ttfFonts;
  void SetUp() override {
    sdFontTestReadFailure = sdFontTestOpenFailure = sdFontTestHandleFailure = false;
    sdFontTestFailArrayAllocations = false; sdFontTestFailNextArrayBytes = 0;
    ESP.largestBlock = 200 * 1024;
    makeTypographyFont(); ASSERT_TRUE(font.load("typography.cpfont"));
    display = std::make_unique<HalDisplay>();
    renderer = std::make_unique<GfxRenderer>(*display);
    renderer->begin();
    renderer->insertFont(FONT_ID, EpdFontFamily(font.getEpdFont()));
    renderer->registerSdCardFont(FONT_ID, &font);
    sdFonts.emplace(FONT_ID, &font);
    cache = std::make_unique<FontCacheManager>(renderer->getFontMap(), sdFonts, ttfFonts);
    renderer->setFontCacheManager(cache.get());
  }
  void TearDown() override {
    sdFontTestReadFailure = sdFontTestOpenFailure = sdFontTestHandleFailure = false;
    sdFontTestFailArrayAllocations = false; sdFontTestFailNextArrayBytes = 0;
    ESP.largestBlock = 200 * 1024;
  }
  void warmTxt(const char* text) {
    const GfxRenderer::TextTypography policy{GfxRenderer::TypographyPolicy::TxtDeterministic, nullptr};
    auto scope = cache->createPrewarmScope();
    renderer->drawTextWithTypography(FONT_ID, 0, 0, text, policy);
    scope.endScanAndPrewarm();
  }
  void warm(const char* text) {
    auto scope = cache->createPrewarmScope();
    renderer->drawText(FONT_ID, 0, 0, text);
    scope.endScanAndPrewarm();
  }
  int measure(const char* text, int8_t tracking = 0) {
    return renderer->getTextAdvanceX(FONT_ID, text, EpdFontFamily::REGULAR, tracking,
                                    BidiUtils::BidiBaseDir::AUTO, GfxRenderer::TextMeasureMode::Rendered);
  }

  Snapshot observeTxt(const char* text, SdCardFont::TxtMetricContext& context, int8_t tracking = 0, int viewport = 15) {
    Snapshot s; s.glyphs.reserve(strlen(text)); s.lines.reserve(1);
    const GfxRenderer::TextTypography policy{GfxRenderer::TypographyPolicy::TxtDeterministic, &context};
    const auto measured = renderer->measureTextWithTypography(FONT_ID, text, policy, EpdFontFamily::REGULAR, tracking);
    EXPECT_EQ(SdCardFont::TxtMetricStatus::Ok, measured.metricStatus);
    s.width = measured.advance;
    renderer->textTraceContext = &s;
    renderer->textTrace = [](void* context, uint32_t cp, int x, int advance) {
      auto& snapshot = *static_cast<Snapshot*>(context);
      if (!cp) snapshot.finalAdvance = x;
      else snapshot.glyphs.push_back({cp, x, advance});
    };
    const auto drawn = renderer->drawTextWithTypography(FONT_ID, 0, 0, text, policy, true,
                             EpdFontFamily::REGULAR, BidiUtils::BidiBaseDir::AUTO, tracking);
    EXPECT_EQ(SdCardFont::TxtMetricStatus::Ok, drawn.metricStatus);
    EXPECT_EQ(s.width, drawn.advance); EXPECT_EQ(s.width, s.finalAdvance);
    renderer->textTrace = nullptr; renderer->textTraceContext = nullptr;
    EXPECT_TRUE(txt::PageLayout::loadPage(reinterpret_cast<const uint8_t*>(text), 0, strlen(text), 0, strlen(text),
         1, viewport, true, [&](const char* candidate) {
           const auto width = renderer->measureTextWithTypography(FONT_ID, candidate, policy, EpdFontFamily::REGULAR, tracking);
           EXPECT_EQ(SdCardFont::TxtMetricStatus::Ok, width.metricStatus); return width.advance;
         }, s.lines, s.nextOffset));
    return s;
  }
  Snapshot observe(const char* text, int viewport = 15, int8_t tracking = 0) {
    Snapshot s; s.glyphs.reserve(strlen(text)); s.lines.reserve(1);
    renderer->ensureSdCardFontReady(FONT_ID, text, 1);
    s.width = measure(text, tracking);
    renderer->textTraceContext = &s;
    renderer->textTrace = [](void* context, uint32_t cp, int x, int advance) {
      auto& state = *static_cast<Snapshot*>(context);
      if (cp == 0) state.finalAdvance = x;
      else state.glyphs.push_back({cp, x, advance});
    };
    renderer->drawText(FONT_ID, 0, 0, text, true, EpdFontFamily::REGULAR, BidiUtils::BidiBaseDir::AUTO, tracking);
    renderer->textTrace = nullptr; renderer->textTraceContext = nullptr;
    EXPECT_TRUE(txt::PageLayout::loadPage(reinterpret_cast<const uint8_t*>(text), 0, strlen(text), 0,
              strlen(text), 1, viewport, true, [this, tracking](const char* candidate) { return measure(candidate, tracking); },
              s.lines, s.nextOffset));
    EXPECT_EQ(s.width, s.finalAdvance);
    return s;
  }
};

TEST_F(TxtRendererIntegration, ProductionKerningChangesColdWarmLineAndSourceBoundary) {
  const auto cold = observe("AV");
  warm("AV"); const auto warmState = observe("AV");
  warm("X"); const auto unrelated = observe("AV");
  cache->releaseSdFontCaches(); const auto evicted = observe("AV");
  for (const auto* state : {&cold, &unrelated, &evicted}) {
    EXPECT_EQ(16, state->width); EXPECT_EQ(16, state->finalAdvance);
    ASSERT_EQ(2U, state->glyphs.size()); EXPECT_EQ(8, state->glyphs[1].x);
    ASSERT_EQ(1U, state->lines.size()); EXPECT_EQ("A", state->lines[0].text);
    EXPECT_EQ(0U, state->lines[0].sourceStart); EXPECT_EQ(1U, state->lines[0].sourceEnd);
    EXPECT_EQ(1U, state->nextOffset);
  }
  EXPECT_EQ(15, warmState.width); EXPECT_EQ(15, warmState.finalAdvance);
  ASSERT_EQ(2U, warmState.glyphs.size()); EXPECT_EQ(7, warmState.glyphs[1].x);
  ASSERT_EQ(1U, warmState.lines.size()); EXPECT_EQ("AV", warmState.lines[0].text);
  EXPECT_EQ(0U, warmState.lines[0].sourceStart); EXPECT_EQ(2U, warmState.lines[0].sourceEnd);
  EXPECT_EQ(2U, warmState.nextOffset);
}

TEST_F(TxtRendererIntegration, ProductionFontTableLigatureChangesGlyphSequenceAndWidth) {
  const auto cold = observe("fi", 8);
  warm("fi"); const auto full = observe("fi", 8);
  EXPECT_EQ(12, cold.width); EXPECT_EQ(1U, cold.nextOffset);
  ASSERT_EQ(2U, cold.glyphs.size()); EXPECT_EQ('f', cold.glyphs[0].cp); EXPECT_EQ('i', cold.glyphs[1].cp);
  EXPECT_EQ(8, full.width); EXPECT_EQ(2U, full.nextOffset);
  ASSERT_EQ(1U, full.glyphs.size()); EXPECT_EQ(0xFB01U, full.glyphs[0].cp);
  cache->releaseSdFontCaches(); EXPECT_EQ(12, observe("fi", 8).width);
}

TEST_F(TxtRendererIntegration, FixtureFractionalReplacementAndZeroAdvanceReachProductionDraw) {
  const auto fractional = observe("XX", 18);
  EXPECT_EQ(18, fractional.width); ASSERT_EQ(2U, fractional.glyphs.size());
  EXPECT_EQ(137, fractional.glyphs[0].advanceFP); EXPECT_EQ(9, fractional.glyphs[1].x);
  EXPECT_EQ(5, observe("\xE2\x98\x83", 10).width); // absent snowman resolves replacement
  EXPECT_EQ(80, font.getEpdFont()->getGlyph(0x2603)->advanceX);
  const auto mark = observe("A\xCC\x81", 15);
  EXPECT_EQ(8, mark.width); ASSERT_EQ(2U, mark.glyphs.size());
  EXPECT_EQ(0x301U, mark.glyphs[1].cp); EXPECT_EQ(0, mark.glyphs[1].advanceFP);
  EXPECT_EQ(0, font.getEpdFont()->getGlyph(0x301)->advanceX);
}

TEST_F(TxtRendererIntegration, ProductionBidiAndArabicContextualShapingAreLinked) {
  const auto arabic = observe("\xD9\x84\xD8\xA7", 32); // Lam Alef contextual shaping
  ASSERT_EQ(1U, arabic.glyphs.size()); EXPECT_EQ(0xFEFBU, arabic.glyphs[0].cp);
  EXPECT_EQ(8, arabic.width); EXPECT_EQ(4U, arabic.nextOffset);
  const auto bidi = observe("A \xD9\x84\xD8\xA7", 40);
  EXPECT_EQ(24, bidi.width); ASSERT_EQ(3U, bidi.glyphs.size()); EXPECT_EQ(0xFEFBU, bidi.glyphs[2].cp);
}

TEST_F(TxtRendererIntegration, MetadataContextMemoryIsBounded) {
  EXPECT_LE(sizeof(SdCardFont::TxtMetricContext), SdCardFont::TxtMetricContext::MAX_CONTEXT_BYTES);
  EXPECT_EQ(0U, SdCardFont::TxtMetricContext::CACHE_ALLOCATION_BYTES);
  EXPECT_EQ(16U, SdCardFont::TxtMetricContext::MAX_METADATA_READ_BYTES);
  RecordProperty("host_metric_context_bytes", static_cast<int>(sizeof(SdCardFont::TxtMetricContext)));
}

TEST_F(TxtRendererIntegration, MetadataOnlyCacheHitsMissesEvictionAndColdWarmAgree) {
  auto context = std::make_unique<SdCardFont::TxtMetricContext>();
  ASSERT_EQ(SdCardFont::TxtMetricStatus::Ok, font.openTxtMetrics(*context));
  const auto allocationCount = sdFontTestArrayAllocations;
  const auto cold = font.getTxtGlyphMetric(*context, 'A');
  EXPECT_TRUE(cold.hasGlyph()); EXPECT_EQ(128, cold.glyph.advanceX);
  sdFontTestReads = 0;
  EXPECT_EQ(128, font.getTxtGlyphMetric(*context, 'A').glyph.advanceX);
  EXPECT_EQ(0U, sdFontTestReads);
  EXPECT_EQ(128, font.getTxtGlyphMetric(*context, 'A', EpdFontFamily::BOLD).glyph.advanceX);
  EXPECT_EQ(0U, sdFontTestReads); // requested bold resolves to the same regular metadata
  warm("AVfi");
  context->clearCache();
  const auto warmMetric = font.getTxtGlyphMetric(*context, 'A');
  EXPECT_EQ(0, std::memcmp(&cold.glyph, &warmMetric.glyph, sizeof(EpdGlyph)));
  warm("X"); context->clearCache();
  EXPECT_EQ(128, font.getTxtGlyphMetric(*context, 'A').glyph.advanceX);
  cache->releaseSdFontCaches(); context->clearCache();
  EXPECT_EQ(128, font.getTxtGlyphMetric(*context, 'A').glyph.advanceX);
  const auto beforeMetrics = sdFontTestArrayAllocations;
  for (uint32_t cp = 0xFE8D; cp <= 0xFEFC; ++cp) EXPECT_TRUE(font.getTxtGlyphMetric(*context, cp).hasGlyph());
  sdFontTestReads = 0;
  EXPECT_EQ(128, font.getTxtGlyphMetric(*context, 'A').glyph.advanceX);
  EXPECT_EQ(1U, sdFontTestReads); // A was evicted from bounded 64-entry ring
  EXPECT_EQ(beforeMetrics, sdFontTestArrayAllocations);
  EXPECT_GE(beforeMetrics, allocationCount);
}

TEST_F(TxtRendererIntegration, MetadataDistinguishesZeroReplacementAndMissing) {
  auto context = std::make_unique<SdCardFont::TxtMetricContext>();
  ASSERT_EQ(SdCardFont::TxtMetricStatus::Ok, font.openTxtMetrics(*context));
  const auto zero = font.getTxtGlyphMetric(*context, 0x301);
  EXPECT_EQ(SdCardFont::TxtMetricStatus::Ok, zero.status); EXPECT_TRUE(zero.hasGlyph());
  EXPECT_EQ(0, zero.glyph.advanceX); EXPECT_EQ(0x301U, zero.resolvedCodepoint);
  const auto nonCombiningZero = font.getTxtGlyphMetric(*context, 'Z');
  EXPECT_EQ(SdCardFont::TxtMetricStatus::Ok, nonCombiningZero.status);
  EXPECT_TRUE(nonCombiningZero.hasGlyph()); EXPECT_EQ(0, nonCombiningZero.glyph.advanceX);
  const auto replacement = font.getTxtGlyphMetric(*context, 0x2603);
  EXPECT_EQ(SdCardFont::TxtMetricStatus::Replacement, replacement.status);
  EXPECT_EQ(0xFFFDU, replacement.resolvedCodepoint); EXPECT_EQ(80, replacement.glyph.advanceX);
  makeTypographyFont(false); ASSERT_TRUE(font.load("no-replacement.cpfont"));
  EXPECT_EQ(SdCardFont::TxtMetricStatus::Unavailable, font.getTxtGlyphMetric(*context, 'A').status);
  ASSERT_EQ(SdCardFont::TxtMetricStatus::Ok, font.openTxtMetrics(*context));
  const auto missing = font.getTxtGlyphMetric(*context, 0x2603);
  EXPECT_EQ(SdCardFont::TxtMetricStatus::Missing, missing.status); EXPECT_FALSE(missing.hasGlyph());
  EXPECT_EQ(0, missing.glyph.advanceX);
}

TEST_F(TxtRendererIntegration, MetadataFailuresAreExplicitAndNeverCachedAsMissingOrZero) {
  auto context = std::make_unique<SdCardFont::TxtMetricContext>();
  sdFontTestHandleFailure = true;
  EXPECT_EQ(SdCardFont::TxtMetricStatus::ResourceFailure, font.openTxtMetrics(*context));
  EXPECT_EQ(SdCardFont::TxtMetricStatus::ResourceFailure, font.getTxtGlyphMetric(*context, 'A').status);
  sdFontTestHandleFailure = false; sdFontTestOpenFailure = true;
  EXPECT_EQ(SdCardFont::TxtMetricStatus::IoFailure, font.openTxtMetrics(*context));
  EXPECT_EQ(SdCardFont::TxtMetricStatus::IoFailure, font.getTxtGlyphMetric(*context, 'A').status);
  sdFontTestOpenFailure = false;
  ASSERT_EQ(SdCardFont::TxtMetricStatus::Ok, font.openTxtMetrics(*context));
  sdFontTestReadFailure = true;
  const auto failure = font.getTxtGlyphMetric(*context, 'A');
  EXPECT_EQ(SdCardFont::TxtMetricStatus::IoFailure, failure.status); EXPECT_FALSE(failure.hasGlyph());
  sdFontTestReadFailure = false;
  EXPECT_EQ(128, font.getTxtGlyphMetric(*context, 'A').glyph.advanceX);
}

TEST_F(TxtRendererIntegration, MetadataIsIndependentOfBitmapAllocationFailure) {
  auto context = std::make_unique<SdCardFont::TxtMetricContext>();
  ASSERT_EQ(SdCardFont::TxtMetricStatus::Ok, font.openTxtMetrics(*context));
  const auto before = sdFontTestArrayAllocations;
  sdFontTestFailArrayAllocations = true;
  EXPECT_EQ(nullptr, font.getEpdFont()->getGlyph('A')); // A and replacement bitmap allocations fail
  const auto attempted = sdFontTestArrayAllocations;
  EXPECT_GT(attempted, before);
  const auto metric = font.getTxtGlyphMetric(*context, 'A');
  EXPECT_EQ(SdCardFont::TxtMetricStatus::Ok, metric.status); EXPECT_EQ(128, metric.glyph.advanceX);
  EXPECT_EQ(attempted, sdFontTestArrayAllocations);
  context->clearCache();
  EXPECT_EQ(0, font.getTxtGlyphMetric(*context, 0x301).glyph.advanceX);
  EXPECT_EQ(attempted, sdFontTestArrayAllocations);
}

TEST_F(TxtRendererIntegration, MetadataSurvivesAdvanceTableSaturationBeyond768Glyphs) {
  makeTypographyFont(true, 800); ASSERT_TRUE(font.load("large.cpfont"));
  std::string text; text.reserve(801 * 3);
  for (uint32_t cp = 0xAC00; cp <= 0xAC00 + 800; ++cp) {
    text.push_back(static_cast<char>(0xE0 | (cp >> 12)));
    text.push_back(static_cast<char>(0x80 | ((cp >> 6) & 63)));
    text.push_back(static_cast<char>(0x80 | (cp & 63)));
  }
  ASSERT_EQ(0, font.buildAdvanceTable(text.c_str(), 1));
  EXPECT_TRUE(font.hasAdvanceTable()); EXPECT_EQ(0, font.getAdvance(0xAC00 + 800, 0));
  auto context = std::make_unique<SdCardFont::TxtMetricContext>();
  ASSERT_EQ(SdCardFont::TxtMetricStatus::Ok, font.openTxtMetrics(*context));
  const auto allocations = sdFontTestArrayAllocations;
  for (uint32_t cp = 0xAC00; cp <= 0xAC00 + 800; ++cp) {
    const auto metric = font.getTxtGlyphMetric(*context, cp, EpdFontFamily::BOLD);
    EXPECT_EQ(SdCardFont::TxtMetricStatus::Ok, metric.status); EXPECT_EQ(cp, metric.resolvedCodepoint);
    EXPECT_EQ(128, metric.glyph.advanceX);
  }
  font.clearPersistentCache(); context->clearCache();
  EXPECT_EQ(128, font.getTxtGlyphMetric(*context, 0xAC00 + 800).glyph.advanceX);
  EXPECT_EQ(allocations, sdFontTestArrayAllocations);
}

TEST_F(TxtRendererIntegration, TxtPolicyColdWarmUnrelatedAndEvictedMovementAndFitAgree) {
  auto context = std::make_unique<SdCardFont::TxtMetricContext>();
  ASSERT_EQ(SdCardFont::TxtMetricStatus::Ok, font.openTxtMetrics(*context));
  for (const char* text : {"AV", "fi", "AVfi", "XX", "A,!012", "\xEA\xB0\x80" "AV\xE4\xB8\x80",
                          "A\xCC\x81" "V", "A\xE2\x98\x83" "V", "AZV", "\xD9\x84\xD8\xA7", "A \xD9\x84\xD8\xA7"}) {
    SCOPED_TRACE(text);
    cache->releaseSdFontCaches(); context->clearCache();
    const auto cold = observeTxt(text, *context, 1);
    warm(text); context->clearCache(); const auto full = observeTxt(text, *context, 1);
    warm("X"); context->clearCache(); const auto unrelated = observeTxt(text, *context, 1);
    cache->releaseSdFontCaches(); context->clearCache(); const auto evicted = observeTxt(text, *context, 1);
    for (const auto* state : {&full, &unrelated, &evicted}) {
      EXPECT_EQ(cold.width, state->width); EXPECT_EQ(cold.nextOffset, state->nextOffset);
      ASSERT_EQ(cold.glyphs.size(), state->glyphs.size());
      for (size_t i = 0; i < cold.glyphs.size(); ++i) {
        EXPECT_EQ(cold.glyphs[i].cp, state->glyphs[i].cp); EXPECT_EQ(cold.glyphs[i].x, state->glyphs[i].x);
        EXPECT_EQ(cold.glyphs[i].advanceFP, state->glyphs[i].advanceFP);
      }
      ASSERT_EQ(cold.lines.size(), state->lines.size());
      for (size_t i = 0; i < cold.lines.size(); ++i) {
        EXPECT_EQ(cold.lines[i].text, state->lines[i].text);
        EXPECT_EQ(cold.lines[i].sourceStart, state->lines[i].sourceStart);
        EXPECT_EQ(cold.lines[i].sourceEnd, state->lines[i].sourceEnd);
      }
    }
  }
  EXPECT_EQ(16, observeTxt("AV", *context).width);
  const auto fi = observeTxt("fi", *context); EXPECT_EQ(12, fi.width); EXPECT_EQ(2U, fi.glyphs.size());
  const auto arabic = observeTxt("\xD9\x84\xD8\xA7", *context, 0, 32);
  ASSERT_EQ(1U, arabic.glyphs.size()); EXPECT_EQ(0xFEFBU, arabic.glyphs[0].cp);
}

TEST_F(TxtRendererIntegration, TxtTrackingKeepsOriginalSpaceBoundariesAndCombiningOverlay) {
  auto context = std::make_unique<SdCardFont::TxtMetricContext>();
  ASSERT_EQ(SdCardFont::TxtMetricStatus::Ok, font.openTxtMetrics(*context));
  for (const char* text : {"A V", "A\xC2\xA0" "V", "A\xE3\x80\x80" "V"}) {
    const auto zero = observeTxt(text, *context, 0, 40);
    const auto tracked = observeTxt(text, *context, 3, 40);
    EXPECT_EQ(24, zero.width); EXPECT_EQ(zero.width, tracked.width);
  }
  const auto mark = observeTxt("A\xCC\x81" "V", *context, 3, 40);
  EXPECT_EQ(19, mark.width); ASSERT_EQ(3U, mark.glyphs.size()); EXPECT_EQ(11, mark.glyphs[2].x);
  // U+2000 is not a special renderer space, so deliberately remains a tracked replacement.
  EXPECT_EQ(27, observeTxt("A\xE2\x80\x80" "V", *context, 3, 40).width);
  makeTypographyFont(true, 0, false); ASSERT_TRUE(font.load("missing-spaces.cpfont"));
  ASSERT_EQ(SdCardFont::TxtMetricStatus::Ok, font.openTxtMetrics(*context));
  for (const char* text : {"A\xC2\xA0" "V", "A\xE3\x80\x80" "V"}) {
    const auto replacedSpace = observeTxt(text, *context, 3, 40);
    EXPECT_EQ(21, replacedSpace.width); ASSERT_EQ(3U, replacedSpace.glyphs.size());
    EXPECT_EQ(0xFFFDU, replacedSpace.glyphs[1].cp); EXPECT_EQ(13, replacedSpace.glyphs[2].x);
  }
}

TEST_F(TxtRendererIntegration, TxtBitmapFailureCannotChangeCanonicalPositionsOrOverlayGeometry) {
  auto context = std::make_unique<SdCardFont::TxtMetricContext>();
  ASSERT_EQ(SdCardFont::TxtMetricStatus::Ok, font.openTxtMetrics(*context));
  const auto normal = observeTxt("AV\xCC\x81", *context, 2, 40);
  cache->releaseSdFontCaches(); context->clearCache(); sdFontTestFailArrayAllocations = true;
  const auto failed = observeTxt("AV\xCC\x81", *context, 2, 40);
  EXPECT_EQ(normal.width, failed.width); ASSERT_EQ(normal.glyphs.size(), failed.glyphs.size());
  for (size_t i = 0; i < normal.glyphs.size(); ++i) EXPECT_EQ(normal.glyphs[i].x, failed.glyphs[i].x);
  const GfxRenderer::TextTypography policy{GfxRenderer::TypographyPolicy::TxtDeterministic, context.get()};
  const auto drawn = renderer->drawTextWithTypography(FONT_ID, 0, 0, "AV", policy);
  EXPECT_EQ(SdCardFont::TxtMetricStatus::Ok, drawn.metricStatus); EXPECT_EQ(16, drawn.advance);
  EXPECT_FALSE(drawn.bitmapAvailable); EXPECT_FALSE(drawn.ok());
  sdFontTestFailArrayAllocations = false; sdFontTestHandleFailure = true;
  const auto handleFailure = renderer->drawTextWithTypography(FONT_ID, 0, 0, "AV", policy);
  EXPECT_EQ(SdCardFont::TxtMetricStatus::Ok, handleFailure.metricStatus);
  EXPECT_EQ(16, handleFailure.advance); EXPECT_FALSE(handleFailure.bitmapAvailable);
}

TEST_F(TxtRendererIntegration, TxtMetricFailureIsExplicitInMeasureAndDraw) {
  auto context = std::make_unique<SdCardFont::TxtMetricContext>();
  ASSERT_EQ(SdCardFont::TxtMetricStatus::Ok, font.openTxtMetrics(*context));
  const GfxRenderer::TextTypography policy{GfxRenderer::TypographyPolicy::TxtDeterministic, context.get()};
  sdFontTestReadFailure = true;
  EXPECT_EQ(SdCardFont::TxtMetricStatus::IoFailure, renderer->measureTextWithTypography(FONT_ID, "AV", policy).metricStatus);
  EXPECT_EQ(SdCardFont::TxtMetricStatus::IoFailure, renderer->drawTextWithTypography(FONT_ID, 0, 0, "AV", policy).metricStatus);
  sdFontTestReadFailure = false;
  EXPECT_TRUE(renderer->measureTextWithTypography(FONT_ID, "AV", policy).ok());
  sdFontTestHandleFailure = true;
  EXPECT_EQ(SdCardFont::TxtMetricStatus::ResourceFailure, font.openTxtMetrics(*context));
  EXPECT_EQ(SdCardFont::TxtMetricStatus::ResourceFailure,
            renderer->measureTextWithTypography(FONT_ID, "AV", policy).metricStatus);
  EXPECT_EQ(SdCardFont::TxtMetricStatus::ResourceFailure,
            renderer->drawTextWithTypography(FONT_ID, 0, 0, "AV", policy).metricStatus);
}

TEST_F(TxtRendererIntegration, BuiltInDefaultMeasurementAndDrawStillAgree) {
  EpdFont builtin(&pretendard_10_regular);
  constexpr int BUILTIN = 43;
  renderer->insertFont(BUILTIN, EpdFontFamily(&builtin));
  // Advances captured by executing HEAD production GfxRenderer before the movement extraction.
  struct Reference { const char* text; int advance; };
  static constexpr Reference references[] = {{"AV", 25}, {"fi", 12}, {"0123,!?", 65}, {"A V", 31},
      {"\xEA\xB0\x80" "AV", 43}, {"\xD9\x84\xD8\xA7", 12}};
  for (const auto& reference : references) {
    const char* text = reference.text;
    Snapshot trace; trace.glyphs.reserve(strlen(text));
    renderer->textTraceContext = &trace;
    renderer->textTrace = [](void* ctx, uint32_t cp, int x, int adv) {
      auto& snapshot = *static_cast<Snapshot*>(ctx);
      if (!cp) snapshot.finalAdvance = x; else snapshot.glyphs.push_back({cp, x, adv});
    };
    renderer->drawText(BUILTIN, 0, 0, text);
    EXPECT_EQ(renderer->getTextAdvanceX(BUILTIN, text, EpdFontFamily::REGULAR, 0,
                BidiUtils::BidiBaseDir::AUTO, GfxRenderer::TextMeasureMode::Rendered), trace.finalAdvance);
    EXPECT_EQ(reference.advance, trace.finalAdvance);
    renderer->textTrace = nullptr; renderer->textTraceContext = nullptr;
  }
}

TEST_F(TxtRendererIntegration, DefaultTrackingCombiningAndFallbackReferenceArePreserved) {
  warm("AV\xCC\x81");
  const auto reference = observe("A\xCC\x81" "V", 40, 3);
  EXPECT_EQ(18, reference.width); ASSERT_EQ(3U, reference.glyphs.size());
  EXPECT_EQ(0, reference.glyphs[0].x); EXPECT_EQ(0, reference.glyphs[1].x);
  EXPECT_EQ(10, reference.glyphs[2].x);
  EXPECT_EQ(0, reference.glyphs[1].advanceFP);
  EXPECT_EQ(80, font.getEpdFont()->getGlyph(0x2603)->advanceX);
  EXPECT_EQ(nullptr, font.getEpdFont()->getGlyphExact(0x2603));

}

TEST_F(TxtRendererIntegration, TxtOnlyPrewarmPreparesBitmapsWithoutKernMatrixOrLigatureOutputs) {
  const auto matrices = sdFontTestKernMatrixAllocations;
  warmTxt("AVfi");
  EXPECT_EQ(matrices, sdFontTestKernMatrixAllocations);
  const auto* data = font.getEpdFont()->data;
  EXPECT_EQ(nullptr, data->kernMatrix); EXPECT_EQ(nullptr, data->ligaturePairs);
  EXPECT_EQ(0, data->ligaturePairCount); EXPECT_EQ(0, data->kernLeftClassCount);
  const auto reads = sdFontTestReads;
  ASSERT_NE(nullptr, font.getEpdFont()->getGlyphExact('A'));
  ASSERT_NE(nullptr, font.getEpdFont()->getGlyphExact('V'));
  EXPECT_EQ(reads, sdFontTestReads); // actual bitmap glyphs are resident
  bool ligatureResident = false;
  for (uint32_t i = 0; i < data->intervalCount; ++i) {
    if (data->intervals[i].first <= 0xFB01 && data->intervals[i].last >= 0xFB01) ligatureResident = true;
  }
  EXPECT_FALSE(ligatureResident);
  sdFontTestReads = 0; const auto allocations = sdFontTestArrayAllocations;
  warmTxt("AVfi"); EXPECT_EQ(0U, sdFontTestReads); EXPECT_EQ(allocations, sdFontTestArrayAllocations);
  warmTxt("AVfiX"); EXPECT_EQ(nullptr, font.getEpdFont()->data->kernMatrix);
  EXPECT_EQ(nullptr, font.getEpdFont()->data->ligaturePairs);
  cache->releaseSdFontCaches(); warmTxt("AVfi");
  EXPECT_EQ(nullptr, font.getEpdFont()->data->kernMatrix);
  EXPECT_EQ(matrices, sdFontTestKernMatrixAllocations);
}

TEST_F(TxtRendererIntegration, MixedBodyAndStatusShareFullTypographyRequirement) {
  const GfxRenderer::TextTypography policy{GfxRenderer::TypographyPolicy::TxtDeterministic, nullptr};
  const auto matrices = sdFontTestKernMatrixAllocations;
  auto scope = cache->createPrewarmScope();
  renderer->drawTextWithTypography(FONT_ID, 0, 0, "AVfi", policy);
  renderer->drawText(FONT_ID, 0, 0, "AVfi", true, EpdFontFamily::BOLD);
  scope.endScanAndPrewarm();
  EXPECT_NE(nullptr, font.getEpdFont()->data->kernMatrix);
  EXPECT_GT(sdFontTestKernMatrixAllocations, matrices);
  EXPECT_NE(nullptr, font.getEpdFont()->data->ligaturePairs);
  EXPECT_EQ(15, measure("AV")); EXPECT_EQ(8, measure("fi"));
  auto context = std::make_unique<SdCardFont::TxtMetricContext>();
  ASSERT_EQ(SdCardFont::TxtMetricStatus::Ok, font.openTxtMetrics(*context));
  EXPECT_EQ(16, observeTxt("AV", *context).width);
  EXPECT_EQ(12, observeTxt("fi", *context).width);
}

TEST_F(TxtRendererIntegration, FullTypographySurvivesTxtRebuildAndFullSubsetTopUp) {
  warm("AVfi"); EXPECT_EQ(15, measure("AV"));
  const auto* retained = font.getEpdFont()->data->kernMatrix;
  warmTxt("AV"); EXPECT_EQ(retained, font.getEpdFont()->data->kernMatrix); // resident full data may remain
  warmTxt("AVfiX"); EXPECT_EQ(nullptr, font.getEpdFont()->data->kernMatrix); // rebuilt glyph-only view
  warm("AVfi"); EXPECT_EQ(15, measure("AV")); EXPECT_EQ(8, measure("fi"));
  EXPECT_NE(nullptr, font.getEpdFont()->data->kernMatrix);
}

TEST_F(TxtRendererIntegration, DifferentFontConsumersKeepFullTypographyIsolatedFromTxtBody) {
  auto uiFont = std::make_unique<SdCardFont>(); ASSERT_TRUE(uiFont->load("ui.cpfont"));
  constexpr int UI = 44;
  renderer->insertFont(UI, EpdFontFamily(uiFont->getEpdFont()));
  renderer->registerSdCardFont(UI, uiFont.get()); sdFonts.emplace(UI, uiFont.get());
  const GfxRenderer::TextTypography policy{GfxRenderer::TypographyPolicy::TxtDeterministic, nullptr};
  {
    auto scope = cache->createPrewarmScope();
    renderer->drawTextWithTypography(FONT_ID, 0, 0, "AVfi", policy);
    renderer->drawText(UI, 0, 0, "AVfi"); scope.endScanAndPrewarm();
    EXPECT_EQ(nullptr, font.getEpdFont()->data->kernMatrix);
    EXPECT_NE(nullptr, uiFont->getEpdFont()->data->kernMatrix);
    EXPECT_NE(nullptr, uiFont->getEpdFont()->data->ligaturePairs);
  }
  sdFonts.erase(UI); renderer->unregisterSdCardFont(UI);
}

TEST_F(TxtRendererIntegration, CappedTxtBodyCannotDropLateSameStyleFullTypographyGlyphs) {
  makeTypographyFont(true, 512); ASSERT_TRUE(font.load("capped.cpfont"));
  std::string body; body.reserve(512 * 3);
  for (uint32_t cp = 0xAC00; cp < 0xAC00 + 512; ++cp) {
    body.push_back(static_cast<char>(0xE0 | (cp >> 12)));
    body.push_back(static_cast<char>(0x80 | ((cp >> 6) & 63)));
    body.push_back(static_cast<char>(0x80 | (cp & 63)));
  }
  const GfxRenderer::TextTypography policy{GfxRenderer::TypographyPolicy::TxtDeterministic, nullptr};
  auto scope = cache->createPrewarmScope();
  renderer->drawTextWithTypography(FONT_ID, 0, 0, body.c_str(), policy);
  renderer->drawText(FONT_ID, 0, 0, "AV"); scope.endScanAndPrewarm();
  EXPECT_EQ(15, measure("AV")); EXPECT_NE(nullptr, font.getEpdFont()->data->kernMatrix);
}

TEST_F(TxtRendererIntegration, TxtPrewarmHandleFailureIsRecoverableWithoutMetricChange) {
  auto context = std::make_unique<SdCardFont::TxtMetricContext>();
  ASSERT_EQ(SdCardFont::TxtMetricStatus::Ok, font.openTxtMetrics(*context));
  sdFontTestHandleFailure = true; warmTxt("AVfi");
  EXPECT_EQ(nullptr, font.getEpdFont()->data->kernMatrix);
  EXPECT_EQ(128, font.getTxtGlyphMetric(*context, 'A').glyph.advanceX);
  sdFontTestHandleFailure = false; warmTxt("AVfi");
  EXPECT_NE(nullptr, font.getEpdFont()->getGlyphExact('A'));
  EXPECT_EQ(nullptr, font.getEpdFont()->data->kernMatrix);
}

TEST_F(TxtRendererIntegration, LigatureOnlyFullSubsetCanTopUpAfterTxtRebuild) {
  makeTypographyFont(true, 0, true, 1, false); ASSERT_TRUE(font.load("ligature-only.cpfont"));
  warm("fi"); EXPECT_EQ(8, measure("fi"));
  warmTxt("fiX\xEF\xAC\x81"); EXPECT_EQ(nullptr, font.getEpdFont()->data->ligaturePairs);
  warm("fi"); EXPECT_EQ(8, measure("fi"));
  EXPECT_NE(nullptr, font.getEpdFont()->data->ligaturePairs);
  EXPECT_EQ(nullptr, font.getEpdFont()->data->kernMatrix);
}

TEST_F(TxtRendererIntegration, TxtBitmapArenaFailureRetriesSmallerWithoutTypographyPreparation) {
  makeTypographyFont(true, 200, true, 128); ASSERT_TRUE(font.load("large-bitmaps.cpfont"));
  std::string body; body.reserve(100 * 3);
  for (uint32_t cp = 0xAC00; cp < 0xAC00 + 100; ++cp) {
    body.push_back(static_cast<char>(0xE0 | (cp >> 12)));
    body.push_back(static_cast<char>(0x80 | ((cp >> 6) & 63)));
    body.push_back(static_cast<char>(0x80 | (cp & 63)));
  }
  const auto matrices = sdFontTestKernMatrixAllocations;
  ESP.largestBlock = 8 * 1024;
  sdFontTestFailNextArrayBytes = 101 * 128;
  warmTxt(body.c_str());
  EXPECT_EQ(0U, sdFontTestFailNextArrayBytes); // requested full arena allocation failed once
  EXPECT_EQ(32U, font.getStats().uniqueGlyphs); EXPECT_EQ(32U * 128, font.getStats().bitmapBytes);
  EXPECT_EQ(nullptr, font.getEpdFont()->data->kernMatrix);
  EXPECT_EQ(nullptr, font.getEpdFont()->data->ligaturePairs);
  ASSERT_NE(nullptr, font.getEpdFont()->getGlyphExact(0xAC00 + 99)); // omitted suffix draws on demand
  auto context = std::make_unique<SdCardFont::TxtMetricContext>();
  ASSERT_EQ(SdCardFont::TxtMetricStatus::Ok, font.openTxtMetrics(*context));
  const GfxRenderer::TextTypography policy{GfxRenderer::TypographyPolicy::TxtDeterministic, context.get()};
  EXPECT_EQ(800, renderer->measureTextWithTypography(FONT_ID, body.c_str(), policy).advance);
  ESP.largestBlock = 200 * 1024; warmTxt(body.c_str());
  EXPECT_EQ(101U, font.getStats().uniqueGlyphs);
  EXPECT_EQ(nullptr, font.getEpdFont()->data->kernMatrix);
  EXPECT_EQ(matrices, sdFontTestKernMatrixAllocations);
}
} // namespace
