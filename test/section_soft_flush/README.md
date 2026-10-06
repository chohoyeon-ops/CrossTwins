# EPUB soft-flush continuity coverage

`EpubSoftFlush` is part of the existing parser target and executes production
character-data, layout/extraction, paragraph close, page construction and Page
serialization code against the existing renderer/SD host adapters. It covers
750/320 threshold edges, multiple flushes, different callback batch positions,
Korean/CJK, hyphenation on/off, tracking/word spacing, indentation, margins,
paragraph spacing on/off, footnotes and multipage visible offsets.

The renderer has deterministic 8 px glyph/4 px space metrics. The existing
hyphenator adapter returns no dictionary-pattern break points; Korean fallback
and production layout still execute. These are layout-representation checks,
not device font rasterization or proof that every DP line-breaking input has
the same optimum at every batch size. This change preserves the existing
bounded batching/line-breaking algorithm and fixes paragraph-boundary semantics.

`SectionSoftFlushTest` compiles the production Section constants and the exact
open/read/version-gate prefix. It checks finalized/partial invalidation and
acceptance of rebuilt-version files. Together with the actual Page roundtrip
test it checks the compatibility change, but does not exercise the complete
Section header/LUT writer, SD recovery or background build/resume scheduler.

Device checks: rebuild a version-50 section, reopen warm, resume from a partial
build, and compare long Korean/CJK paragraph indent/spacing, links/footnotes,
visible-text restoration, page turns and sleep/reboot. Check both hyphenation
settings with SD fonts and embedded CSS.
