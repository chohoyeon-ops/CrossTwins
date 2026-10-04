#pragma once

#include <cstddef>
#include <cstdint>

// Format-specific action visibility for the shared Reader Options menu. Keep
// the policy separate from activities so its action set can be host-tested.
namespace readermenu {

enum class Profile : uint8_t { Epub, Txt };

enum class Action : uint8_t {
  SELECT_CHAPTER,
  FOOTNOTES,
  TEXT_SETTINGS,
  NIGHT_MODE,
  FRONTLIGHT,
  GO_TO_PERCENT,
  AUTO_PAGE_TURN,
  ROTATE_SCREEN,
  BOOKMARKS,
  TOGGLE_BOOKMARK,
  SCREENSHOT,
  DISPLAY_QR,
  GO_HOME,
  SYNC,
  DELETE_CACHE,
  DICTIONARY,
  Count
};

constexpr size_t actionCount(const Profile profile) {
  return profile == Profile::Txt ? 6 : static_cast<size_t>(Action::Count);
}

constexpr Action actionAt(const Profile profile, const size_t visibleIndex) {
  if (profile == Profile::Txt) {
    switch (visibleIndex) {
      case 0:
        return Action::NIGHT_MODE;
      case 1:
        return Action::ROTATE_SCREEN;
      case 2:
        return Action::SCREENSHOT;
      case 3:
        return Action::DISPLAY_QR;
      case 4:
        return Action::GO_HOME;
      case 5:
        return Action::TEXT_SETTINGS;
      default:
        return Action::Count;
    }
  }
  return visibleIndex < static_cast<size_t>(Action::Count) ? static_cast<Action>(visibleIndex) : Action::Count;
}

constexpr bool supportsAction(const Profile profile, const Action action) {
  if (profile == Profile::Epub) return action != Action::Count;
  return action == Action::TEXT_SETTINGS || action == Action::NIGHT_MODE || action == Action::ROTATE_SCREEN ||
         action == Action::SCREENSHOT || action == Action::DISPLAY_QR || action == Action::GO_HOME;
}

}  // namespace readermenu
