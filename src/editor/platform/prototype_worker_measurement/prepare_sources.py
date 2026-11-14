"""Generate throwaway benchmark copies without changing the port's core."""
from pathlib import Path
import sys

source = Path(__file__).resolve().parent
port, output = map(Path, sys.argv[1:3])
output.mkdir(parents=True, exist_ok=True)
core = (port / 'src/editor/editor_core.cpp').read_text(encoding='utf-8')
anchor = 'EditorControl::Core::Core(EditorControl& window, int selectionOptions) : window_(window) {\n    wMain = static_cast<wxWindow*>(&window_);\n    Initialise();'
assert core.count(anchor) == 1
core = '#include "research_profile.h"\n' + core.replace(anchor, anchor + '\n    if (ResearchRequestedThreads) hardwareConcurrency = ResearchRequestedThreads;\n    ResearchActualThreads = hardwareConcurrency;\n    UpdateParallelLayoutThreshold();')
(output / 'editor_core_probe.cpp').write_text(core, encoding='utf-8')
benchmark = (source / 'benchmark_probe.cpp').read_text(encoding='utf-8')
benchmark = benchmark.replace('#include "prototype_memory.h"', '#include "prototype_memory.h"\n#include "prototype_geometry.h"')
anchor = 'if (frame->GetClientSize() == target && frame->Editor()->GetSize() == target) return;'
assert benchmark.count(anchor) == 1
benchmark = benchmark.replace(anchor, '''if (frame->GetClientSize() == target && frame->Editor()->GetSize() == target) {
            EditorCoreObservation::Check(*frame->Editor(), width);
            return;
        }''')
benchmark = benchmark.replace('    ResearchReport(name, "wrap-and-first-paint", wall, cpu);', '    ResearchReport(name, "wrap-and-first-paint", wall, cpu);\n    EditorCoreObservation::Check(*editor, 680);')
anchor = '        frame->Show();'
assert benchmark.count(anchor) == 1
benchmark = benchmark.replace(anchor, '#ifdef __WXGTK__\n        gtk_scrolled_window_set_overlay_scrolling(GTK_SCROLLED_WINDOW(frame->Editor()->GetHandle()), false);\n#endif\n' + anchor)
(output / 'benchmark_fixed.cpp').write_text(benchmark, encoding='utf-8')
