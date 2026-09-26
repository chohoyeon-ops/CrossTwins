#!/usr/bin/env python3
"""List the Hangul syllables used by the built-in Korean UI translation."""

from pathlib import Path


repo_root = Path(__file__).resolve().parents[3]
source = repo_root / "lib/I18n/translations/korean.yaml"
output = repo_root / "lib/EpdFont/builtinFonts/ui-korean-character-set.txt"
codepoints = sorted({ord(char) for char in source.read_text(encoding="utf-8") if "\uac00" <= char <= "\ud7a3"})
output.write_text(
    "# Hangul syllables used by lib/I18n/translations/korean.yaml\n"
    + "\n".join(f"U+{codepoint:04X}" for codepoint in codepoints)
    + "\n",
    encoding="utf-8",
    newline="\n",
)
print(f"Generated {output}: {len(codepoints)} Hangul syllables")
