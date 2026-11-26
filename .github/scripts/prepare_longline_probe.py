"""Generate observed source copies; never edit the supplied source tree.

Usage: prepare_longline_probe.py --source-root REPO --output-dir FRESH_TREE
                                [--helper-path CANDIDATE_HELPER]
                                [--search-path CANDIDATE_FIXTURE]
                                [--native-search-path CANDIDATE_FEEDBACK_HELPER]
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


def native_search(text):
    text = replace_once(text, '#pragma once\n', '#pragma once\n#include "diagnostics/longline_probe.h"\n')
    iteration_target = ('0' if 'inline void DispatchX11Iteration() {\n' in text
                        else 'reinterpret_cast<std::intptr_t>(window)')
    iteration = ('    gtk_main_iteration_do(FALSE);\n'
                 '#if wxUSE_EXCEPTIONS\n'
                 '    // Native callbacks store exceptions until control returns to C++.\n'
                 '    wxTheApp->RethrowStoredException();\n'
                 '#endif\n')
    text = replace_once(text, iteration,
                        '    {\n'
                        '        const LonglineProbe::Scope iterationTiming(LonglineProbe::Kind::GtkIterationBegin,\n'
                        f'            LonglineProbe::Kind::GtkIterationEnd, {iteration_target});\n'
                        + iteration + '    }\n')
    for method, kind in (('ProcessPendingEvents', 'PendingEvents'), ('ProcessIdle', 'ProcessIdle')):
        call = f'    wxTheApp->{method}();\n'
        text = replace_once(text, call,
                            '    {\n'
                            f'        const LonglineProbe::Scope dispatchPartTiming(LonglineProbe::Kind::{kind}Begin,\n'
                            f'            LonglineProbe::Kind::{kind}End, reinterpret_cast<std::intptr_t>(window));\n'
                            + call + '    }\n')
    legacy = '        if (dialog->GetName() == "Search feedback") wxTheApp->CallAfter([dialog] { Click(dialog, wxID_OK); });\n'
    if legacy in text:
        text = replace_once(text, legacy,
                            '        if (dialog->GetName() == "Search feedback") {\n'
                            '            wxTheApp->CallAfter([dialog] { Click(dialog, wxID_OK); });\n'
                            '        }\n')
    text = replace_once(text, '        if (dialog->GetName() == "Search feedback") {\n',
                        '        if (dialog->GetName() == "Search feedback") {\n'
                        '            LonglineProbe::Event(LonglineProbe::Kind::FeedbackEnter, 0,\n'
                        '                reinterpret_cast<std::intptr_t>(dialog), dialog->IsShown(), dialog->IsModal());\n')
    text = replace_once(text, '            wxTheApp->CallAfter([dialog] { Click(dialog, wxID_OK); });\n',
                        '            wxTheApp->CallAfter([dialog] {\n'
                        '                LonglineProbe::Event(LonglineProbe::Kind::FeedbackFired, 0,\n'
                        '                    reinterpret_cast<std::intptr_t>(dialog), -1, dialog->IsModal(), -1);\n'
                        '                Click(dialog, wxID_OK);\n'
                        '            });\n'
                        '            LonglineProbe::Event(LonglineProbe::Kind::FeedbackQueued, 0,\n'
                        '                reinterpret_cast<std::intptr_t>(dialog), 0);\n')
    text = replace_once(text, 'inline void Click(wxWindow* dialog, int id) {\n',
                        'inline void Click(wxWindow* dialog, int id) {\n'
                        '    const LonglineProbe::Scope clickTiming(LonglineProbe::Kind::FeedbackClickBegin,\n'
                        '        LonglineProbe::Kind::FeedbackClickEnd, reinterpret_cast<std::intptr_t>(dialog),\n'
                        '        id, dialog->IsShown(), dialog->GetName() == "Search feedback");\n')
    if 'class FeedbackResponse final' in text:
        queued = ('        if (!timer_.StartOnce(10)) {\n'
                  '            Cancel(dialog_.get());\n'
                  '            throw std::runtime_error("schedule the native search feedback response");\n'
                  '        }\n')
        text = replace_once(text, queued, queued +
                            '        LonglineProbe::Event(LonglineProbe::Kind::FeedbackQueued, 0,\n'
                            '            reinterpret_cast<std::intptr_t>(dialog_.get()), 1,\n'
                            '            !opening_ || opening_->Released());\n')
        text = replace_once(text, '        auto* dialog = dialog_.get();\n',
                            '        auto* dialog = dialog_.get();\n'
                            '        LonglineProbe::Event(LonglineProbe::Kind::FeedbackFired, 0,\n'
                            '            reinterpret_cast<std::intptr_t>(dialog), !opening_ || opening_->Released(),\n'
                            '            dialog && dialog->IsModal(), opening_ ?\n'
                            '                std::chrono::duration_cast<std::chrono::microseconds>(\n'
                            '                    opening_->Deadline() - OpeningShortcut::Clock::now()).count() : -1);\n')
        text = replace_once(text, '    void Exit(wxDialog* dialog) override { response_.Cancel(dialog); }\n',
                            '    void Exit(wxDialog* dialog) override {\n'
                            '        LonglineProbe::Event(LonglineProbe::Kind::FeedbackExit, 0,\n'
                            '            reinterpret_cast<std::intptr_t>(dialog));\n'
                            '        response_.Cancel(dialog);\n'
                            '    }\n')
    else:
        text = replace_once(text, '        return wxID_NONE;\n    }\n};\n#ifdef __WXMSW__\n',
                            '        return wxID_NONE;\n    }\n'
                            '    void Exit(wxDialog* dialog) override {\n'
                            '        LonglineProbe::Event(LonglineProbe::Kind::FeedbackExit, 0,\n'
                            '            reinterpret_cast<std::intptr_t>(dialog));\n'
                            '    }\n};\n#ifdef __WXMSW__\n')
    return text


def search_session(text):
    text = '#include "diagnostics/longline_probe.h"\n' + text
    start = text.index('bool SearchSession::SaveSelectionAsFindText(bool selectWord) {\n')
    end = text.index('\nstd::optional<FindResult> SearchSession::FindSelection(', start)
    prefix, body, suffix = text[:start], text[start:end], text[end:]
    body = replace_once(body, '    using Scintilla::Message;\n',
                        '    const LonglineProbe::Scope saveFindTiming(LonglineProbe::Kind::SaveFindBegin,\n'
                        '        LonglineProbe::Kind::SaveFindEnd, selectWord, reinterpret_cast<std::intptr_t>(&editor_));\n'
                        '    using Scintilla::Message;\n')
    rejection = '    if (length <= 0 || static_cast<size_t>(length) >= SearchLimits::SelectionBytes) return false;\n'
    body = replace_once(body, rejection,
                        '    LonglineProbe::Event(LonglineProbe::Kind::SaveFindLength, saveFindTiming.Id(), length);\n'
                        '    if (length <= 0 || static_cast<size_t>(length) >= SearchLimits::SelectionBytes) {\n'
                        '        LonglineProbe::Event(LonglineProbe::Kind::SaveFindResult, saveFindTiming.Id(), 0, length);\n'
                        '        return false;\n'
                        '    }\n')
    body = replace_once(body, '    return true;\n',
                        '    LonglineProbe::Event(LonglineProbe::Kind::SaveFindResult, saveFindTiming.Id(), 1, length);\n'
                        '    return true;\n')
    return prefix + body + suffix


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
    parser.add_argument('--native-search-path', type=Path)
    args = parser.parse_args()
    source = args.source_root.resolve()
    output = args.output_dir.resolve()
    if output.exists():
        parser.error('--output-dir must be fresh')
    paths = ['tests/search_continuation.cpp', 'tests/native_key_helpers.h', 'src/editor/editor_core.cpp',
             'tests/native_search_helpers.h', 'src/search/search_session.cpp']
    contents = {}
    hashes = {}
    for relative in paths:
        path = source / relative
        if relative == paths[0] and args.search_path:
            path = args.search_path
        if relative == paths[1] and args.helper_path:
            path = args.helper_path
        if relative == paths[3] and args.native_search_path:
            path = args.native_search_path
        raw = path.read_bytes()
        contents[relative] = raw.decode('utf-8')
        hashes[relative] = {'source': str(path.resolve()), 'sha256': hashlib.sha256(raw).hexdigest()}
    generated = {paths[0]: fixture(contents[paths[0]]), paths[1]: helper(contents[paths[1]]),
                 paths[2]: core(contents[paths[2]]), paths[3]: native_search(contents[paths[3]]),
                 paths[4]: search_session(contents[paths[4]])}
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
                'record_capacity': 32768, 'record_bytes': 80,
                'dispatch_payloads': {
                    'gtk_iteration_begin': (
                        '0 (the native-only helper has no window argument); span includes the existing native-exception rethrow boundary'
                        if 'inline void DispatchX11Iteration() {\n' in contents[paths[3]] else
                        'window; span includes the existing native-exception rethrow boundary'),
                    'pending_events_begin': 'window; existing wx ProcessPendingEvents call only',
                    'process_idle_begin': 'window; existing wx ProcessIdle call only',
                    'save_find_begin': 'selectWord, editor, unused, unused',
                    'save_find_length': 'selected byte count, unused, unused, unused',
                    'save_find_result': 'accepted, selected byte count, unused, unused'},
                'key_payloads': {
                    'key_observed': 'phase(1=CHAR_HOOK/2=DOWN/3=UP), wxkey, modifiers, wxTarget',
                    'key_budget': 'remaining_us, received, checkpoint(1=initial/2=before-dispatch/3=after-dispatch/4=loop-end), expectedKey',
                    'key_deadline': 'steady_clock deadline_ns, expectedKey, modifiers, unused',
                    'x11_queue_prefix': 'cached X event count, first type, keycode, state; XEventsQueued(QueuedAlready) then non-consuming XPeekEvent',
                    'x11_queue_identity': 'window, serial, time, send_event for that first queued X event',
                    'x11_generic_prefix': 'extension opcode, evtype, cookie, data_present for an already cached GenericEvent; no XGetEventData call',
                    'gdk_queue_prefix': 'first cached type, keyval, state, time; GDK peek only copies its cached queue prefix and neither reads new X events nor dispatches/removes an event'},
                'feedback_payloads': {
                    'feedback_enter': 'dialog, shown, modal, unused',
                    'feedback_queued': 'dialog, route(0=CallAfter/1=timer), released_if_timer, unused',
                    'feedback_fired': 'dialog, released_or_minus_one_for_CallAfter, modal, remaining_release_deadline_us_or_minus_one',
                    'feedback_click_begin': 'dialog, button_id, shown, is_search_feedback',
                    'feedback_click_end': 'uncaught_exceptions, unused, unused, unused',
                    'feedback_exit': 'dialog, unused, unused, unused'}}
    (output / 'manifest.json').write_text(json.dumps(manifest, indent=2) + '\n', encoding='utf-8')
    print(json.dumps({'output_dir': str(output), 'files': list(generated)}))


if __name__ == '__main__':
    main()
