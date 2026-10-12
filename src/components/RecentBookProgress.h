#pragma once

#include <FreeInkUIGfxRenderer.h>
#include <GfxRenderer.h>

#include <algorithm>

namespace CrossPointUI {
namespace fui = freeink::ui;

inline void drawRecentBookProgress(const GfxRenderer& renderer, freeink::ui::DrawTarget& target,
                                   const freeink::ui::Rect bar, const int percent) {
  if (percent < 0 || bar.width < 3 || bar.height < 3) return;
  target.fill(bar, fui::Paint::solid(fui::Color::White));
  target.stroke(bar, fui::Paint::solid(fui::Color::Black), 1);
  const fui::Rect inner{static_cast<int16_t>(bar.x + 1), static_cast<int16_t>(bar.y + 1),
                        static_cast<int16_t>(bar.width - 2), static_cast<int16_t>(bar.height - 2)};
  const int width = inner.width * std::clamp(percent, 0, 100) / 100;
  if (width <= 0) return;
  const fui::Rect fill{inner.x, inner.y, static_cast<int16_t>(width), inner.height};
  if (percent <= 25) {
    target.fill(fill, fui::Paint::dither(fui::Color::LightGray));
  } else if (percent <= 50) {
    target.fill(fill, fui::Paint::dither(fui::Color::DarkGray));
  } else if (percent <= 75) {
    target.fill(fill, fui::Paint::solid(fui::Color::Black));
    for (int y = fill.y; y < fill.bottom(); ++y) {
      for (int x = fill.x; x < fill.right(); ++x) {
        if ((x & 1) == 0 && (y & 1) == 0) renderer.drawPixel(x, y, false);
      }
    }
  } else {
    target.fill(fill, fui::Paint::solid(fui::Color::Black));
  }
}

}  // namespace CrossPointUI
