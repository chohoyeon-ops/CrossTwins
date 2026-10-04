#pragma once

#include <cstddef>
#include <cstdint>

// Reader-specific visibility for the shared text settings screen. Keep this
// policy independent of UI classes so the TXT row set can be tested directly.
namespace textsettings {

enum class Profile : uint8_t { Epub, Txt };
enum class Tab : uint8_t { Font, Size, Layout, Style, Count };

enum class LayoutControl : uint8_t {
  LineSpacing,
  WordSpacing,
  CharacterSpacing,
  ParagraphSpacing,
  ParagraphIndentation,
  Alignment,
  ScreenMargin,
  Count
};

enum class StyleControl : uint8_t { FocusReading, Hyphenation, EmbeddedStyle, AntiAliasing, Count };
enum class AlignmentOption : uint8_t { Justified, Left, Center, Right, BookStyle, Count };

constexpr size_t tabCount(const Profile) { return static_cast<size_t>(Tab::Count); }
constexpr bool hasLayoutPreview(const Profile profile) { return profile == Profile::Epub; }

constexpr Tab tabAt(const Profile profile, const size_t visibleIndex) {
  return visibleIndex < tabCount(profile) ? static_cast<Tab>(visibleIndex) : Tab::Count;
}

constexpr size_t layoutControlCount(const Profile profile) {
  return profile == Profile::Txt ? 3 : static_cast<size_t>(LayoutControl::Count);
}

constexpr LayoutControl layoutControlAt(const Profile profile, const size_t visibleIndex) {
  if (profile == Profile::Txt) {
    switch (visibleIndex) {
      case 0:
        return LayoutControl::LineSpacing;
      case 1:
        return LayoutControl::Alignment;
      case 2:
        return LayoutControl::ScreenMargin;
      default:
        return LayoutControl::Count;
    }
  }
  return visibleIndex < static_cast<size_t>(LayoutControl::Count) ? static_cast<LayoutControl>(visibleIndex)
                                                                  : LayoutControl::Count;
}

constexpr size_t styleControlCount(const Profile profile) {
  return profile == Profile::Txt ? 2 : static_cast<size_t>(StyleControl::Count);
}

constexpr StyleControl styleControlAt(const Profile profile, const size_t visibleIndex) {
  if (profile == Profile::Txt) {
    switch (visibleIndex) {
      case 0:
        return StyleControl::Hyphenation;
      case 1:
        return StyleControl::AntiAliasing;
      default:
        return StyleControl::Count;
    }
  }
  return visibleIndex < static_cast<size_t>(StyleControl::Count) ? static_cast<StyleControl>(visibleIndex)
                                                                 : StyleControl::Count;
}

constexpr bool hasLayoutControl(const Profile profile, const LayoutControl control) {
  for (size_t i = 0; i < layoutControlCount(profile); ++i) {
    if (layoutControlAt(profile, i) == control) return true;
  }
  return false;
}

constexpr bool hasStyleControl(const Profile profile, const StyleControl control) {
  for (size_t i = 0; i < styleControlCount(profile); ++i) {
    if (styleControlAt(profile, i) == control) return true;
  }
  return false;
}

constexpr size_t alignmentOptionCount(const Profile profile) {
  return profile == Profile::Txt ? 3 : static_cast<size_t>(AlignmentOption::Count);
}

constexpr AlignmentOption alignmentOptionAt(const Profile profile, const size_t visibleIndex) {
  if (profile == Profile::Txt) {
    switch (visibleIndex) {
      case 0:
        return AlignmentOption::Left;
      case 1:
        return AlignmentOption::Center;
      case 2:
        return AlignmentOption::Right;
      default:
        return AlignmentOption::Count;
    }
  }
  return visibleIndex < static_cast<size_t>(AlignmentOption::Count) ? static_cast<AlignmentOption>(visibleIndex)
                                                                    : AlignmentOption::Count;
}

}  // namespace textsettings
