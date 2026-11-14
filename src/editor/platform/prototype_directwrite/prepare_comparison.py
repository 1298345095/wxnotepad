"""Add a public-message test bridge to a build-local host copy only."""
from pathlib import Path
import sys

root, output = map(Path, sys.argv[1:3])
source = (root / 'src/editor/editor_control.cpp').read_text(encoding='utf-8')
anchor = 'WXLRESULT EditorControl::MSWWindowProc(WXUINT message, WXWPARAM wParam, WXLPARAM lParam) {'
assert source.count(anchor) == 1
bridge = '''
    // Test-only transport: numeric Scintilla public messages, as on the original HWND.
    constexpr unsigned FirstEditorMessage = 2000;
    constexpr unsigned LastEditorMessage = 5000;
    constexpr unsigned CheckGeometryMessage = WM_APP + 0x35;
    if (message >= FirstEditorMessage && message <= LastEditorMessage)
        return Send(static_cast<Scintilla::Message>(message), wParam, lParam);
    if (message == CheckGeometryMessage) {
        try {
            EditorCoreObservation::Check(*this, 0);
            std::fflush(stdout);
            return 1;
        } catch (const std::exception& error) {
            std::fprintf(stderr, "FAIL: %s\\n", error.what());
            return 0;
        }
    }
'''
source = '#include "prototype_geometry.h"\n' + source.replace(anchor, anchor + bridge)
(output / 'editor_control_comparison.cpp').write_text(source, encoding='utf-8')
