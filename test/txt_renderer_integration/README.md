# Production TXT typography integration

This target links the repository GfxRenderer, FontCacheManager, SdCardFont,
EpdFont/Family, Utf8, and MiniBidi/shaping sources. Hardware/storage/allocation
adapters are host-only; the storage adapter serves genuine .cpfont v4 bytes
built by FontFixture.h. Measurement and fitting algorithms are not duplicated.
The uncompressed fixture does not exercise FontDecompressor or image-file I/O.

The default-policy reproduction intentionally asserts the existing divergence:
AV is 16 px cold, 15 px after production scan/prewarm, then 16 px after an
unrelated-page prewarm or resident-cache eviction. At a 15 px viewport the
one-line page changes from source [0,1), nextOffset 1 to [0,2), nextOffset 2.
The fi font-table ligature changes 12 px/two glyphs to 8 px/one glyph.

CROSSPOINT_RENDERER_TEST_TRACE is defined only on this host executable. The
observer records glyph positions/advances from the actual draw loop and its
final rounded cursor. Firmware has no observer fields or branches. Future TXT
policy tests can reuse the same fixture and snapshots, while default-policy
reproduction remains a regression test for full typography behavior.

The fixture also provides 12.4 fractional advance 137, a missing-codepoint
replacement of advance 80, zero-advance U+0301, Hangul, and Arabic presentation
forms. Real BiDi/contextual shaping of Lam-Alef is asserted independently of
font-table ligature substitution. Device timing, heap, SD hardware, and actual
typography appearance remain unverified.

Phase 4.2 adds a caller-owned metadata session with a 64-entry fixed cache and
one retained HAL read handle. Cache misses read exactly one 16-byte EpdGlyph;
no bitmap or prewarm/advance-cache allocation is involved. Failure injection
at the host HAL boundary distinguishes checked-handle resource failure from
open/read I/O failure. Production checked HAL allocation is not linked on the
host (it depends on the SDK); the firmware build compiles its nothrow path.
The new metadata API returns the same A advance in cold/warm/unrelated/evicted
states; the original default-policy renderer divergence deliberately remains.

Default ESP32-C3 DWARF sizes for Phase 4.2: TxtMetricContext 2080 bytes,
HalFile::Impl 80 bytes. The defined upper bounds are 2112 and 256 bytes,
respectively; cache storage is embedded in the context and the metadata read
size is 16 bytes. Context allocation is the caller's responsibility; the API
is not wired to TXT yet in this phase, so firmware linker GC omits its unused
lookup code and no session heap usage is present in the build totals.

Phase 4.3 adds opt-in TextTypography and checked measure/draw results. Both
Rendered measurement and ordinary drawing use the same GlyphWalker; the
legacy Layout fast path is preserved. TXT disables font-table K/L but retains
visual BiDi/contextual Arabic preparation, combining overlays, per-glyph snap,
and tracking based on original visual codepoints. Exact bitmap lookup cannot
substitute replacement after OOM; bitmap availability is reported separately
from stable metadata advance. The SD overflow path now uses checked HAL open.

Built-in numeric references were obtained by compiling/executing
`git show HEAD:lib/GfxRenderer/GfxRenderer.cpp` (HEAD 4e8e8684) with the same
production EpdFont/Family and actual pretendard_10_regular asset. Only its
relative Memory.h include was relocated for the ignored build artifact.
The original renderer source and six-line baseline driver are preserved under
build/txt-phase1-test/OriginalGfx.cpp and BuiltinBaseline.cpp for this session.
Rendered advances: AV=25, fi=12, 0123,!?=65, A V=31, Hangul-ga+AV=43,
Arabic Lam-Alef=12. Permanent tests assert these captured values against
current production measurement AND draw trace, rather than parity alone.

Phase 4.4 scan requirements are per resolved font/style. GlyphsOnly requests
prepare TXT bitmaps; any FullTypography consumer upgrades the group. A bit in
already allocated scan entries prioritizes full-consumer glyphs over body-only
glyphs when the 512-codepoint scan is saturated; default-only scans retain the
previous capacity and ordering. No per-consumer string/cache allocation is added.

Cold TXT-only prewarm, subset, rebuild, eviction, and bitmap-arena retry all
assert zero int8_t (mini kerning matrix) allocation attempts through the host
allocator used by production SdCardFont. A mixed full consumer asserts a matrix
is built. Existing full tables may remain resident during a TXT-only subset hit;
this phase suppresses new TXT-requested preparation rather than globally deleting
another consumer's font data. A full request after glyph-only rebuilding tops up
missing matrix/ligature pointers, including ligature-only fonts.

The TXT retry regression fails the 12,928-byte full bitmap arena once while
reporting an 8 KiB largest block, then verifies the production shrink path keeps
32 glyphs / 4096 bitmap bytes. The omitted suffix loads on demand, deterministic
metadata width stays 800 px, and a subsequent unrestricted scan restores all
101 glyphs. No matrix allocation occurs at any point in that body-only test.

Phase 4.5 also compiles the actual TxtReaderActivity.cpp and Txt.cpp. Test-only
base activity/UI adapters supply desktop dependencies; index build, save/load,
page reconstruction, source reads, progress gating and ProgressFile publication
execute production code. Host HAL supports path-specific short reads/writes and
rename failure; nothrow operator new/new[] injection exercises real context,
page-window and page-offset allocation branches.

The source partition oracle compares every PageLine consumed range and nextOffset
with subsequent index starts before/after default warm, eviction and cache reopen.
An 8191-byte ASCII prefix + three-byte Hangul + LF revealed a terminator ownership
bug (sourceEnd 8195 / nextOffset 8194); production pagination now consumes that
paragraph terminator before its chunk-boundary early exit. LF, CRLF and EOF edge
fixtures verify the corrected contract.

Fresh deterministic-policy v7 caches are tested here. Invalidation of old v7
full-typography semantics intentionally remains Phase 4.6. This phase does not
claim all-system OOM recovery: inherited line/string/BiDi allocations are not
nothrow, and full UI typography preparation remains its existing path. TXT checked
progress uses an opt-in backup publication path; default EPUB/XTC calls retain
the existing remove/rename behavior. Backup recovery is tested after both publish
and rollback rename fail; FAT metadata operations are not power-loss atomic.
