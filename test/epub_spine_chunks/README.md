# Large EPUB spine lookup tests

Tests extract the current BookMetadataCache chunk loop, SizeTarget/hash and
ZipFile central-directory matching implementation. Only the lookup entry point
is renamed for instrumentation; matching logic is unchanged. A memory-backed
file supplies generated ZIP central-directory records, and a streamed spine
adapter supplies chapter hrefs. Path normalization is also production code.

Coverage includes the unchanged small-spine branch, 400/2047/2048/2049/4096/4097/
10000 chapters, reverse ZIP order, Korean paths, repeated hrefs across chunks,
missing entries and an invalid directory. The observed target-count high water
is 2048 (32768 bytes of target payload). This is not a measurement of device
peak heap: deque overhead, output sizes and other metadata still consume memory.
No fully packaged EPUB archive or book.bin serialization is emulated here.

Existing ContentOpfParser tests include large text limits; additional real-parser
tests exercise Korean XML split across UTF-8 boundaries and incomplete XML.
Real large-EPUB opening, cumulative progress/navigation and SD scan time should
be checked on X3/X4.
