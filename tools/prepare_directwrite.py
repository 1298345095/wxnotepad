"""Prepare the pinned Notepad4 DirectWrite backend without changing its algorithms.

Generated files retain their upstream copyright and license notices.
Adapt subclassability and host factories; retain the original monitor/SPI setup.
"""
from pathlib import Path
import sys

core, output = map(Path, sys.argv[1:3])
source = (core / 'scintilla/win32/SurfaceD2D.cxx').read_text(encoding='utf-8')
anchor = 'class SurfaceD2D final : public Surface {'
assert source.count(anchor) == 1
source = '#pragma once' + chr(10) + source.replace(anchor, 'class SurfaceD2D : public Surface {')
anchor = 'std::shared_ptr<Font> Font::Allocate(const FontParameters &fp) {'
assert source.count(anchor) == 1
source = source[:source.index(anchor)] + '} // namespace Scintilla::Internal\n'
output.mkdir(parents=True, exist_ok=True)
(output / 'original_directwrite.inc').write_text(source, encoding='utf-8')

# Keep original monitor selection, SPI contrast validation and COM ownership.
window_source = (core / 'scintilla/win32/ScintillaWin.cxx').read_text(encoding='utf-8')
start = window_source.index('bool ScintillaWin::UpdateRenderingParams(bool force) noexcept {')
end = window_source.index('#if _WIN32_WINNT >= _WIN32_WINNT_WIN7', window_source.index(chr(10) + '}' + chr(10), start))
params = window_source[start:end].replace('ScintillaWin::UpdateRenderingParams', 'WindowTarget::UpdateRenderingParams')
notice = window_source[:window_source.index('#include')]
(output / 'original_rendering_params.inc').write_text(notice + params, encoding='utf-8')
