# wxWidgets 3.3.4 modal and accelerator observations

The first PR284 coverage run failed `original_run_command` and
`original_default_fonts` on both Linux backends:

https://github.com/1298345095/wxnotepad/actions/runs/45932713653

PR285's independent X11 map-readiness fix was then merged into the upgrade
branch at `6d4db037`, starting run
https://github.com/1298345095/wxnotepad/actions/runs/45936197133 .
Local controls at that revision still failed both cases on 3.3.4, while the
unchanged 3.3.3 cases passed on the same host and both backends.

## Accelerator observation

Upstream commit
[1043551376](https://github.com/wxWidgets/wxWidgets/commit/10435513765b6e7804c2d4a0b5b23d45c063a483)
deliberately changes wxGTK's default accelerator priority. In the pinned 3.3.4
`src/gtk/window.cpp`, wx sends `CHAR_HOOK`, lets GTK activate a matching menu
accelerator, and sends `KEY_DOWN` only if the key was not handled.

Tagged observation of the same real Alt+F12 recorded `CHAR_HOOK` with `wxMOD_ALT`
on both SDKs. Only 3.3.3 also emitted `KEY_DOWN`. On 3.3.4 the correct native font
chooser had already appeared and its initial values were correct when the
old `KEY_DOWN`-specific observer failed.

The font fixture observes the real F12 key and exact Alt modifier at `CHAR_HOOK`
before accelerator dispatch. It retains the complete native chooser, cancel,
font-setting, Shift+F12, completion and document/history checks. The earlier
modifier-loss regression remains covered by the chooser's rejection of the
incorrect bare-F12 Select Scheme dialog as well as the exact modifier check.

## Modal-exit observation

3.3.4 changes wxGTK `IsModal()` to test whether `m_modalLoop` exists.
`EndModal()` hides the dialog and requests loop exit; the loop object is
destroyed after the executing callback returns. Changes include
[ee3afacf15](https://github.com/wxWidgets/wxWidgets/commit/ee3afacf157574c2a180c1b45a43c040481b6875),
[1f3403d587](https://github.com/wxWidgets/wxWidgets/commit/1f3403d5872559a9ed4c2d468896a7ea0e534db1)
and [c25ca728cc](https://github.com/wxWidgets/wxWidgets/commit/c25ca728cc3ce457cc82f1a8cf81a4a7a94ab14a).

For the literal `$HOME` executable sample, both SDKs reported a hidden dialog,
`wxID_OK` and no launch feedback after clicking Run. At that callback boundary,
`IsModal()` was false on 3.3.3 and true on 3.3.4. The latter was incorrectly
interpreted as a failed launch.

The callback checks that successful launch hides the window. The existing
`IsModal()==false` requirement is checked at `wxModalDialogHook::Exit`, where the
modal loop has actually finished. This also checks the hidden state, OK return
code and receipt of the exit callback. All literal executable-name, actual-child
argv, working-directory and disk-byte assertions remain unchanged. An exit-check
failure preserves any earlier action error.

## Approval and validation

The user explicitly approved these two observation-point changes after reviewing
the exact candidate and old/new evidence. Neither product behavior nor samples
were changed. The existing 90-second Run Command and 60-second font limits,
inner waits, default concurrency and 80%/3% quality gates remain.

Both complete cases passed with the candidate on 3.3.3 and 3.3.4, under X11 and
Wayland: eight Linux case executions. Both Windows 3.3.4 cases also passed.
Canonical clang-format passed for all 574 maintained files. Hosted final-state
checks remain required separately.

Local evidence and the approved proposal are retained under
`F:/wxnotepad-evidence/wxwidgets-3.3.4-upgrade-2026-11-26/ci-284/`, with native
controls under `/home/ctrls/wxnotepad-wxwidgets-3.3.4-2026-11-26/ci-followup/`.
