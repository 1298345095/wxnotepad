"""Generate instrumented copies; preserve application/core behavior and original probes."""
from pathlib import Path
import re
import sys

port, output = map(Path, sys.argv[1:3])
source_root = port / 'src/editor/platform/prototype_directwrite'

def replace_once(text, old, new):
    assert text.count(old) == 1, old
    return text.replace(old, new)

def scope(text, function, metric):
    expression = r'(' + re.escape(function) + r'\([^{}]*?\)(?: noexcept)? \{\n)'
    result, count = re.subn(expression, r'\1    DWriteDiagnosis::Scope diagnosticScope(DWriteDiagnosis::Metric::' + metric + ');\n', text)
    assert count == 1, (function, count)
    return result

native = (output / 'original_directwrite.inc').read_text(encoding='utf-8')
for function, metric in [('void SurfaceD2D::DrawTextCommon', 'DrawText'),
                         ('TextLayout LayoutCreate', 'LayoutCreate'),
                         ('std::unique_ptr<Surface> SurfaceD2D::AllocatePixMap', 'PixmapCreate'),
                         ('void SurfaceD2D::MeasureWidths', 'MeasureWidths'),
                         ('void SurfaceD2D::MeasureWidthsUTF8', 'MeasureWidths'),
                         ('XYPOSITION SurfaceD2D::WidthText', 'WidthText'),
                         ('XYPOSITION SurfaceD2D::WidthTextUTF8', 'WidthText')]:
    native = scope(native, function, metric)
native = replace_once(native, '\t// Availability of SetDefaultDllDirectories',
    '\tDWriteDiagnosis::Memory("before-graphics-load");\n\t// Availability of SetDefaultDllDirectories')
native = replace_once(native, '\thDLLDWrite = ::LoadLibraryEx',
    '\tDWriteDiagnosis::Memory("after-d2d-factory");\n\thDLLDWrite = ::LoadLibraryEx')
native = replace_once(native, '\thDLLD3D = ::LoadLibraryEx',
    '\tDWriteDiagnosis::Memory("after-dwrite-factory");\n\thDLLD3D = ::LoadLibraryEx')
native = replace_once(native, '\n#if USE_WIN32_INIT_ONCE\n\treturn TRUE;',
    '\n\tDWriteDiagnosis::Memory("after-d3d-library");\n#if USE_WIN32_INIT_ONCE\n\treturn TRUE;')
(output / 'original_directwrite_diagnostic.inc').write_text('#include "diagnostic_metrics.h"\n' + native, encoding='utf-8')

adapter = (source_root / 'directwrite_surface.cpp').read_text(encoding='utf-8')
adapter = replace_once(adapter, '"original_directwrite.inc"', '"original_directwrite_diagnostic.inc"')
start = adapter.index('    CheckResult(pD2DFactory->CreateHwndRenderTarget(')
end = adapter.index('    if (found == windowTargets.end())', start)
creation = adapter[start:end]
adapter = adapter[:start] + '    DWriteDiagnosis::Memory("before-hwnd-target");\n    {\n        DWriteDiagnosis::Scope diagnosticScope(DWriteDiagnosis::Metric::TargetCreate);\n' + creation + '    }\n    DWriteDiagnosis::Memory("after-hwnd-target");\n' + adapter[end:]
adapter = replace_once(adapter, '                status_ = windowTarget_->target->Resize(desired);',
    '            {\n                DWriteDiagnosis::Scope diagnosticScope(DWriteDiagnosis::Metric::TargetResize);\n                status_ = windowTarget_->target->Resize(desired);\n            }')
adapter = replace_once(adapter, '            status_ = drawing_->EndDraw();',
    '            {\n                DWriteDiagnosis::Scope diagnosticScope(DWriteDiagnosis::Metric::Present);\n                status_ = drawing_->EndDraw();\n            }')
(output / 'directwrite_surface_diagnostic.cpp').write_text(adapter, encoding='utf-8')

core = (output / 'editor_core_probe.cpp').read_text(encoding='utf-8')
core = scope(core, 'void EditorControl::Core::Draw', 'CorePaint')
core = replace_once(core, 'void EditorControl::Core::Resize() {\n    ChangeSize();',
    'void EditorControl::Core::Resize() {\n    {\n        DWriteDiagnosis::Scope diagnosticScope(DWriteDiagnosis::Metric::CoreChangeSize);\n        ChangeSize();\n    }')
core = scope(core, 'void EditorControl::Core::Resize', 'CoreResize')
core = replace_once(core, '    WrapLines(WrapScope::wsAll);',
    '    {\n        DWriteDiagnosis::Scope diagnosticScope(DWriteDiagnosis::Metric::CoreWrap);\n        WrapLines(WrapScope::wsAll);\n    }')
(output / 'editor_core_diagnostic.cpp').write_text('#include "diagnostic_metrics.h"\n' + core, encoding='utf-8')

probe = (output / 'benchmark_fixed.cpp').read_text(encoding='utf-8')
probe = replace_once(probe, 'constexpr int ResizeCount = 12;', 'constexpr int ResizeCount = 60;')
probe = probe.replace('12-natural-resizes', '60-diagnostic-resizes')
probe = replace_once(probe, '    throw std::runtime_error("native resize geometry");',
    '    std::fprintf(stderr, "GEOMETRY requested=%dx%d frame=%dx%d editor=%dx%d client=%dx%d dpi=%d\\n", target.x, target.y, frame->GetClientSize().x, frame->GetClientSize().y, frame->Editor()->GetSize().x, frame->Editor()->GetSize().y, frame->Editor()->GetClientSize().x, frame->Editor()->GetClientSize().y, frame->GetDPI().y);\n    throw std::runtime_error("native resize geometry");')
probe = replace_once(probe, '        ResearchGuiThread = std::this_thread::get_id();',
    '        ResearchGuiThread = std::this_thread::get_id();\n        DWriteDiagnosis::captureMemory = argc != 3 || wxString(argv[2]) != "geometry";\n        if (!DWriteDiagnosis::Initialize()) return false;\n        DWriteDiagnosis::Memory("before-editor-frame");')
probe = replace_once(probe, '                Measure(frame, folder, longLine);',
    '                if (wxString(argv[2]) == "geometry") {\n                    Resize(frame, 680);\n                    std::puts("PASS: diagnostic geometry; no timing or memory sampling");\n                } else Measure(frame, folder, longLine);')
probe = replace_once(probe, '    editor->ToggleWordWrap();',
    '    DWriteDiagnosis::Begin("initial-wrap");\n    editor->ToggleWordWrap();')
probe = replace_once(probe, '    ResearchReport(name, "wrap-and-first-paint", wall, cpu);',
    '    DWriteDiagnosis::End("initial-wrap");\n    ResearchReport(name, "wrap-and-first-paint", wall, cpu);')
probe = replace_once(probe, '    for (int pass = 0; pass < ResizeCount; ++pass)',
    '    DWriteDiagnosis::Begin("warm-resizes");\n    for (int pass = 0; pass < ResizeCount; ++pass)')
probe = replace_once(probe, '    ResearchReport(name, "60-diagnostic-resizes", wall, cpu);',
    '    DWriteDiagnosis::End("warm-resizes");\n    ResearchReport(name, "60-diagnostic-resizes", wall, cpu);')
for stage in ['empty', 'wrapped', 'after-resizes', 'after-new']:
    anchor = f'    PrototypeMemory(name, "{stage}");'
    probe = replace_once(probe, anchor, anchor + f'\n    DWriteDiagnosis::Memory("{stage}");')
probe = replace_once(probe, '            frame->Destroy();\n            ExitMainLoop();',
    '            frame->Destroy();\n            DeletePendingObjects();\n            DWriteDiagnosis::Memory("after-window-destroy");\n            DWriteDiagnosis::Finish();\n            ExitMainLoop();')
(output / 'diagnostic_probe.cpp').write_text('#include "diagnostic_metrics.h"\n' + probe, encoding='utf-8')
