#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

#include "HyphenationCommon.h"
#include "SerializedHyphenationTrie.h"

// Encapsulates every language-specific dial the Liang algorithm needs at runtime.  The helpers are
// intentionally represented as bare function pointers because we invoke them inside tight loops and
// want to avoid the overhead of std::function or functors.  The minima default to the TeX-recommended
// "2/2" split but individual languages (English, for example) can override them.
struct LiangWordConfig {
  static constexpr size_t kDefaultMinPrefix = 2;
  static constexpr size_t kDefaultMinSuffix = 2;
  // Predicate used to reject non-alphabetic characters before pattern lookup.  Returning false causes
  // the entire word to be skipped, matching the behavior of classic TeX hyphenation tables.
  bool (*isLetter)(uint32_t);
  // Language-specific case folding that matches how the TeX patterns were authored (usually lower-case
  // ASCII for Latin and lowercase Cyrillic for Russian).  Patterns are stored in UTF-8, so this must
  // operate on Unicode scalars rather than bytes.
  uint32_t (*toLower)(uint32_t);
  // Minimum codepoints required on the left/right of any break.  These correspond to TeX's
  // lefthyphenmin and righthyphenmin knobs.
  size_t minPrefix;
  size_t minSuffix;

  // Lightweight aggregate constructor so call sites can declare `const LiangWordConfig config(...)`
  // without verbose member assignment boilerplate.
  LiangWordConfig(bool (*letterFn)(uint32_t), uint32_t (*lowerFn)(uint32_t), size_t prefix = kDefaultMinPrefix,
                  size_t suffix = kDefaultMinSuffix)
      : isLetter(letterFn), toLower(lowerFn), minPrefix(prefix), minSuffix(suffix) {}
};

// Caller-owned storage for the fixed-size Liang evaluator. On ESP32-C3 this is
// about 1.2 KiB and can be reused across TXT words without per-word allocation.
struct LiangWorkspace {
  static constexpr size_t MAX_WORD_BYTES = 160;
  static constexpr size_t MAX_WORD_CHARS = 70;

  uint8_t bytes[MAX_WORD_BYTES];
  size_t charByteOffsets[MAX_WORD_CHARS];
  int32_t byteToCharIndex[MAX_WORD_BYTES];
  uint8_t scores[MAX_WORD_CHARS];
  size_t byteLen = 0;
  size_t charCount_ = 0;

  bool empty() const { return byteLen == 0; }
  size_t charCount() const { return charCount_; }
};

enum class LiangBreakStatus : uint8_t { Ok, InvalidInput, OutputTooSmall, InvalidPatterns };

// Shared Liang pattern evaluator used by every language-specific hyphenator.
std::vector<size_t> liangBreakIndexes(const std::vector<CodepointInfo>& cps,
                                      const SerializedHyphenationPatterns& patterns, const LiangWordConfig& config);

// Bounded variants share the same trie scoring core. Output indexes are byte
// offsets for the ASCII form and codepoint indexes for the CodepointInfo form.
LiangBreakStatus liangBreakIndexesInto(const CodepointInfo* cps, size_t cpCount,
                                       const SerializedHyphenationPatterns& patterns, const LiangWordConfig& config,
                                       LiangWorkspace& workspace, uint8_t* output, size_t outputCapacity,
                                       size_t& outputCount);
LiangBreakStatus liangBreakIndexesAsciiInto(const char* word, size_t wordLength,
                                            const SerializedHyphenationPatterns& patterns,
                                            const LiangWordConfig& config, LiangWorkspace& workspace,
                                            uint8_t* output, size_t outputCapacity, size_t& outputCount);
