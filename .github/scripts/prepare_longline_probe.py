"""Generate observed source copies; never edit the supplied source tree.

Usage: prepare_longline_probe.py --source-root REPO --output-dir FRESH_TREE
                                [--helper-path CANDIDATE_HELPER]
                                [--search-path CANDIDATE_FIXTURE]
"""
import argparse
import hashlib
import json
from pathlib import Path
import re


def replace_once(text, old, new):
    if text.count(old) != 1:
        raise ValueError(f'Expected one unchanged instrumentation anchor: {old[:120]!r}')
    return text.replace(old, new, 1)


def helper(text):
    text = replace_once(text, '#pragma once\n', '#pragma once\n#include "diagnostics/longline_probe.h"\n')
    text = replace_once(text, '            received_ = true;\n',
                        '            received_ = true;\n'
                        '            LonglineProbe::Event(LonglineProbe::Kind::Release, 0, key_,\n'
                        '                static_cast<wxKeyEvent&>(event).GetModifiers(),\n'
                        '                reinterpret_cast<std::intptr_t>(event.GetEventObject()));\n')
    if 'class ShortcutPresentation final' in text:
        text = replace_once(text, '                acknowledgedEpoch = showEpoch;\n        }\n',
                            '                acknowledgedEpoch = showEpoch;\n'
                            '            LonglineProbe::Event(LonglineProbe::Kind::MapShown, 0,\n'
                            '                reinterpret_cast<std::intptr_t>(window.get()),\n'
                            '                showEpoch, acknowledgedEpoch, shown);\n'
                            '        }\n')
        text = replace_once(text, '                entry.acknowledgedEpoch = entry.showEpoch;\n',
                            '                entry.acknowledgedEpoch = entry.showEpoch;\n'
                            '                LonglineProbe::Event(LonglineProbe::Kind::MapAcknowledged, 0,\n'
                            '                    reinterpret_cast<std::intptr_t>(entry.window.get()),\n'
                            '                    entry.showEpoch, entry.acknowledgedEpoch, event->type);\n')
    start = text.index('inline void Shortcut(EditorFrame* frame, int key, bool control,')
    prefix, body = text[:start], text[start:]
    body = replace_once(body, '    const NativeTrace::Gesture trace(',
                        '    const LonglineProbe::Shortcut shortcutTiming(key, control, alt, shift);\n'
                        '    const NativeTrace::Gesture trace(')
    body = replace_once(body, '    FocusEditor(frame, focusName, focusFile);\n',
                        '    const auto focusTiming = LonglineProbe::Event(LonglineProbe::Kind::FocusBegin);\n'
                        '    FocusEditor(frame, focusName, focusFile);\n'
                        '    LonglineProbe::Event(LonglineProbe::Kind::FocusEnd, focusTiming);\n')
    body = replace_once(body, '    wxString request;\n    {\n',
                        '    wxString request;\n'
                        '    const auto requestTiming = LonglineProbe::Event(LonglineProbe::Kind::RequestBegin);\n'
                        '    {\n')
    body = replace_once(body, '    const auto keyDeadline = Clock::now() + std::chrono::seconds(3);\n',
                        '    LonglineProbe::Event(LonglineProbe::Kind::RequestEnd, requestTiming);\n'
                        '    const auto keyDeadline = Clock::now() + std::chrono::seconds(3);\n')
    body = replace_once(body, '    while (!received && Clock::now() < keyDeadline) {\n',
                        '    while (!received && Clock::now() < keyDeadline) {\n'
                        '        const auto dispatchTiming = LonglineProbe::Event(\n'
                        '            LonglineProbe::Kind::DispatchBegin, 0, received);\n')
    # Only the original release loop is measured here. A candidate may add a
    # separate presentation wait with its own sleep after the final dispatch.
    begin = body.index('    while (!received && Clock::now() < keyDeadline) {\n')
    end = body.index('    if (!received) {\n', begin)
    loop = replace_once(body[begin:end], '        wxMilliSleep(10);\n',
                        '        LonglineProbe::Event(LonglineProbe::Kind::DispatchEnd, dispatchTiming, received);\n'
                        '        wxMilliSleep(10);\n')
    body = body[:begin] + loop + body[end:]
    body = replace_once(body, '#endif\n    NativeSearch::DispatchEvents(frame);\n',
                        '#endif\n'
                        '    const auto finalTiming = LonglineProbe::Event(LonglineProbe::Kind::FinalDispatchBegin);\n'
                        '    NativeSearch::DispatchEvents(frame);\n'
                        '    LonglineProbe::Event(LonglineProbe::Kind::FinalDispatchEnd, finalTiming);\n')
    if 'class ShortcutPresentation final' in prefix:
        body = replace_once(body, '    while (!presentation.Complete() && Clock::now() < keyDeadline) {\n',
                            '    const auto mapTiming = LonglineProbe::Event(LonglineProbe::Kind::MapWaitBegin,\n'
                            '        0, presentation.Complete());\n'
                            '    while (!presentation.Complete() && Clock::now() < keyDeadline) {\n')
        body = replace_once(body, '    if (!presentation.Complete())\n',
                            '    LonglineProbe::Event(LonglineProbe::Kind::MapWaitEnd, mapTiming, presentation.Complete());\n'
                            '    if (!presentation.Complete())\n')
    return prefix + body


def fixture(text):
    text = '#include "diagnostics/longline_probe.h"\n' + text
    text = replace_once(text, 'void SelectionLimit(EditorFrame* frame) {\n',
                        'void SelectionLimit(EditorFrame* frame) {\n'
                        '    const LonglineProbe::Scenario observation;\n')
    text = replace_once(text, 'void ReportStage(const char* stage) {\n',
                        'void ReportStage(const char* stage) {\n'
                        '    LonglineProbe::Stage(stage);\n')
    text = replace_once(text, '            source_ = 0;\n            observedPress_ = true;\n',
                        '            LonglineProbe::Event(LonglineProbe::Kind::NativeIdleRemoved, 0, source_);\n'
                        '            source_ = 0;\n            observedPress_ = true;\n')
    # Observe source installation without changing source ownership or priority.
    for suffix in ('during the selection-limit shortcut', 'after the actual shortcut release'):
        pattern = re.compile(r'^( +)Require\(source_ != 0, "keep real native activity queued ' + re.escape(suffix) + r'"\);$', re.M)
        if len(pattern.findall(text)) != 1:
            raise ValueError(f'Expected unchanged native idle installation: {suffix}')
        text = pattern.sub(lambda m: m.group(0) + '\n' + m.group(1) +
                           'LonglineProbe::Event(LonglineProbe::Kind::NativeIdleAdded, 0, source_);', text)
    return text


def core(text):
    text = '#include "diagnostics/longline_probe.h"\n' + text
    text = replace_once(text, 'void EditorControl::Core::Draw(wxDC& dc) {\n',
                        'void EditorControl::Core::Draw(wxDC& dc) {\n'
                        '    const LonglineProbe::Scope drawTiming(LonglineProbe::Kind::DrawBegin,\n'
                        '        LonglineProbe::Kind::DrawEnd, pdoc->LengthNoExcept(), Wrapping(),\n'
                        '        caret.period, reinterpret_cast<std::intptr_t>(this));\n')
    text = replace_once(text, '    const auto size = window_.GetClientSize();\n',
                        '    const auto size = window_.GetClientSize();\n'
                        '    LonglineProbe::Event(LonglineProbe::Kind::Viewport, drawTiming.Id(),\n'
                        '        size.x, size.y, wrapWidth, vs.textStart + vs.rightMarginWidth);\n')
    text = replace_once(text, '    Paint(surface.get(), rcPaint);\n',
                        '    const auto paintTiming = LonglineProbe::Event(LonglineProbe::Kind::PaintBegin, drawTiming.Id());\n'
                        '    Paint(surface.get(), rcPaint);\n'
                        '    LonglineProbe::Event(LonglineProbe::Kind::PaintEnd, paintTiming);\n')
    text = replace_once(text, 'void EditorControl::Core::TickCaret() {\n',
                        'void EditorControl::Core::TickCaret() {\n'
                        '    const LonglineProbe::Scope tickTiming(LonglineProbe::Kind::CaretTickBegin,\n'
                        '        LonglineProbe::Kind::CaretTickEnd, caret.period, caret.active, caret.on);\n')
    text = replace_once(text, 'void EditorControl::Core::OnIdle(wxIdleEvent& event) {\n',
                        'void EditorControl::Core::OnIdle(wxIdleEvent& event) {\n'
                        '    const LonglineProbe::Scope idleTiming(LonglineProbe::Kind::IdleBegin,\n'
                        '        LonglineProbe::Kind::IdleEnd, Wrapping(), wrapPending.NeedsWrap());\n')
    return text


def stage_hash(label):
    value = 14695981039346656037
    for byte in label.encode():
        value = ((value ^ byte) * 1099511628211) & ((1 << 64) - 1)
    return str(value)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--source-root', type=Path, required=True)
    parser.add_argument('--output-dir', type=Path, required=True)
    parser.add_argument('--helper-path', type=Path)
    parser.add_argument('--search-path', type=Path)
    args = parser.parse_args()
    source = args.source_root.resolve()
    output = args.output_dir.resolve()
    if output.exists():
        parser.error('--output-dir must be fresh')
    paths = ['tests/search_continuation.cpp', 'tests/native_key_helpers.h', 'src/editor/editor_core.cpp']
    contents = {}
    hashes = {}
    for relative in paths:
        path = source / relative
        if relative == paths[0] and args.search_path:
            path = args.search_path
        if relative == paths[1] and args.helper_path:
            path = args.helper_path
        raw = path.read_bytes()
        contents[relative] = raw.decode('utf-8')
        hashes[relative] = {'source': str(path.resolve()), 'sha256': hashlib.sha256(raw).hexdigest()}
    generated = {paths[0]: fixture(contents[paths[0]]), paths[1]: helper(contents[paths[1]]),
                 paths[2]: core(contents[paths[2]])}
    generated['src/diagnostics/longline_probe.h'] = Path(__file__).with_name('longline_probe.h').read_text()
    for relative, text in generated.items():
        destination = output / relative
        destination.parent.mkdir(parents=True, exist_ok=True)
        destination.write_text(text, encoding='utf-8')
    stages = re.findall(r'ReportStage\("([^"]+)"\)', contents[paths[0]])
    manifest = {'source_root': str(source), 'inputs': hashes,
                'outputs': {relative: hashlib.sha256(text.encode()).hexdigest() for relative, text in generated.items()},
                'stage_names': {stage_hash(label): label for label in stages},
                'trace_environment': 'WXNOTEPAD_PAINT_TRACE', 'trace_path_is_exact': True,
                'record_capacity': 8192, 'record_bytes': 80}
    (output / 'manifest.json').write_text(json.dumps(manifest, indent=2) + '\n', encoding='utf-8')
    print(json.dumps({'output_dir': str(output), 'files': list(generated)}))


if __name__ == '__main__':
    main()
