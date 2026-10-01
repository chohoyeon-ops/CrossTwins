#include <Bitmap.h>
// Non-text image I/O is outside this target; glyph drawing uses production GlyphBitmap.
BmpReaderError Bitmap::readNextRow(uint8_t*, uint8_t*) const { return BmpReaderError::FileInvalid; }
