"""Prepare the pinned Notepad4 DirectWrite backend without changing its algorithms.

The generated file retains its upstream copyright and license notice.
Only subclassability and the host-provided Font/Surface factories are adapted.
"""
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
