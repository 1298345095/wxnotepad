"""Adapt only native factory/host seams in a build-local original source copy."""
from pathlib import Path
import sys

core, output = map(Path, sys.argv[1:3])
source = (core / 'scintilla/win32/SurfaceD2D.cxx').read_text(encoding='utf-8')
anchor = 'class SurfaceD2D final : public Surface {'
assert source.count(anchor) == 1
source = source.replace(anchor, 'class SurfaceD2D : public Surface {')
anchor = 'std::shared_ptr<Font> Font::Allocate(const FontParameters &fp) {'
assert source.count(anchor) == 1
source = source[:source.index(anchor)] + '} // namespace Scintilla::Internal\n'
output.mkdir(parents=True, exist_ok=True)
(output / 'original_directwrite.inc').write_text(source, encoding='utf-8')
