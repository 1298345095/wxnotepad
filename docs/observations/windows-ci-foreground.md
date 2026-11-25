# Windows CI foreground acquisition research

Researched 2026-11-24 for the native Windows context-menu tests on the
GitHub Actions Windows 2025 image. This is a research note, not a verified fix.

## Findings

The documented routes are an existing foreground-capable process granting a
specific child foreground permission, or a temporary foreground-lock setting
in an isolated CI desktop. Both require foreground permission at setup time.
Neither `AttachThreadInput` success nor a new controller thread is a documented
substitute for that permission. [1][2][3][4]

The smallest useful next experiment is to set the **session's** foreground lock
timeout to zero once, record the API result and readback, then run the original
first-right-click test with plain foreground activation. This can test a
specific policy explanation while retaining the original inputs, assertions,
and menu/peer/case budgets. It does not prove the observed failure is caused by
the lock timeout, and zero does not disable the other foreground restrictions.
[1][4]

## Existing observations supplied to this investigation

These are project experiment receipts, not claims established by the external
sources below:

- The original UI-thread helper attaches to the old foreground thread, calls
  `BringWindowToTop`, `SetForegroundWindow`, `SetActiveWindow`, and editor
  `SetFocus`, then detaches. It can report the root as foreground immediately,
  but the first `SendInput` right-click is followed about 15 ms later by a null
  or Terminal foreground; the expected right-click message does not arrive.
- Hiding Windows Terminal was verified (`visible_after=false`, process still
  alive); the failure still occurred with foreground null.
- Removing attachment fails the existing foreground precondition.
- A fresh controller thread successfully attached to the previous foreground
  queue, but `SetForegroundWindow(root)` returned **zero inside that thread**;
  the foreground remained the old window. The UI's local active/focus HWNDs
  remained its own root/editor. A single UI `PeekMessage` dispatch did not fix it.
- Standard output/error are pipes and the console HWND is null.

The controller receipt is a rejected foreground request. It must not be
described as a successful request waiting for asynchronous activation. The
original helper's later loss of foreground is consistent with a queue-state
problem, but its exact Windows-internal cause has not been established.

## Hosted experiment results

After this research, the isolated runner reported an original foreground-lock
timeout of 2,147,483,647 ms (about 24.9 days). Setting the session value to zero
succeeded, and final readback confirmed restoration to the original value.
The same non-attached activation that failed the second process in the control
then passed 20 complete menu cases with Terminal hidden and another 20 with
Terminal visible. The visible run recorded successful existing handoffs from
the independent clipboard peer to the editor. No extra input or longer test
deadlines were introduced.

- [Direct activation control, failed](https://github.com/1298345095/wxnotepad/actions/runs/45517852308)
- [Zero timeout, hidden Terminal, 20 passed](https://github.com/1298345095/wxnotepad/actions/runs/45522754770)
- [Zero timeout, visible Terminal, 20 passed](https://github.com/1298345095/wxnotepad/actions/runs/45523449889)

These results support preparing the disposable CI desktop's foreground policy
and removing GUI-queue attachment. They do not independently establish the
cause of the earlier empty native clipboard paste; its original content
assertions remain part of each complete run.

## What Windows documents

`SetForegroundWindow` requires a desktop application, no active menu, and no
foreground-process lock, plus at least one qualifying condition: expired
foreground-lock timeout, already foreground, started by the foreground
process, no foreground window, receipt of the last input, or debugging of one
of the processes. Microsoft explicitly says a request can still be denied
even when those conditions are met. A zero return means the window was not
brought to the foreground. The API does not document a useful `GetLastError`
contract for explaining its rejection. [1]

`AllowSetForegroundWindow(targetPid)` is the supported transfer mechanism. Its
caller must already be able to set the foreground; otherwise the call fails.
The grant can be lost on subsequent user input directed elsewhere or another
ASFW grant. Calling it from the failing process itself, or from an equally
unprivileged launcher, is not a documented bootstrap. Use the specific target
PID rather than `ASFW_ANY` if a grant-capable launcher is established. [2]

`AttachThreadInput` shares input states, including focus and keyboard state.
It requires both threads to have message queues, cannot cross desktops, and
resets key state. Its documentation does not promise to grant foreground
permission. Successful attachment only establishes successful attachment. [3]

Raymond Chen explains that successful cross-queue `SetForegroundWindow` first
selects the foreground input queue, then asynchronously asks the destination
to activate its window. Threads in the selected foreground queue have
foreground permission. The destination must process messages; an external
automation controller can use a bounded `SendMessageTimeout(WM_NULL)` before
checking `GetForegroundWindow`. This synchronization is relevant **after an
accepted request**. It does not turn a zero-return request into an accepted
one. The article also cautions against attaching automation to the application
queue because one hung party can hang the other. [5]

`SendInput` reports insertion into the system input stream, not delivery to an
asserted editor HWND. It is subject to UIPI and does not reset existing keyboard
state. Its official contract does not say that injecting arbitrary mouse
motion grants the caller foreground rights. The “caller received the last
input” criterion is about receipt, not merely synthesizing an event. [1][6]

## Candidate A: temporary CI foreground-lock setting

`SPI_GETFOREGROUNDLOCKTIMEOUT` retrieves a DWORD millisecond value.
`SPI_SETFOREGROUNDLOCKTIMEOUT` takes the new timeout **as the `pvParam` value**,
not a pointer to a DWORD. Its calling thread must already be able to change
the foreground window, or it fails. `fWinIni=0` avoids `SPIF_UPDATEINIFILE`
profile persistence and the optional settings broadcast. This changes a
desktop/session setting, rather than narrowing permission to one process.
[4]

Suggested experiment, scoped to the disposable hosted CI desktop:

1. Read and record the original timeout, foreground HWND/thread/PID, caller
   thread/PID, and menu state before any setting change.
2. Attempt one setter call directly from CI setup with zero and `fWinIni=0`.
   Record the BOOL/error immediately and GET the effective value. Establish
   actual setup eligibility from this receipt rather than assuming it.
3. Continue the policy experiment only when the setter succeeds and readback
   is zero. If the initial value was already zero, merely writing zero supplies
   no evidence that timeout policy explains the failure.
4. Detach any setup queues, then use plain `SetForegroundWindow` and local
   active/focus handling. Preserve the actual first right-click and all its
   original assertions and timeout budgets; do not add input retries.
5. Restore the saved setting in the same setup process's `finally`/scope
   cleanup while foreground rights are still available, and record restoration
   result/readback. Restoration has the same permission prerequisite.

An attachment-based setup bootstrap is deferred. The first experiment should
establish whether setup can set the policy directly. If it fails, stop with
that receipt; moving the same call to a new unprivileged thread does not supply
a documented new permission source. A successful attachment or matching
foreground HWND alone must not be treated as proof that a setter will succeed.

This is simpler than a persistent launcher/broker. It cannot prevent an
unrelated app from taking foreground, bypass an active menu or an explicit
foreground-process lock, or guarantee that zero resolves this failure. [1][4]

## Candidate B: explicit foreground permission transfer

A longer-lived launcher that demonstrably has foreground rights can create
the test process, call `AllowSetForegroundWindow(testPid)`, and let the test
activate its own window. This is the narrow documented design for cooperating
processes. Each grant's return value must be checked, and unrelated input can
invalidate the grant. The current CI shell/launcher has not yet been shown to
possess those rights, so an ASFW call alone is not a complete solution. [2]

A persistent broker that keeps/reacquires a real foreground window and hands
permission from one independent test process to the next would need lifecycle
and shutdown coordination. That added mechanism is justified only if the
smaller session-policy experiment cannot solve the actual failure. There is no
primary Windows source here guaranteeing that merely spawning a fresh helper
process grants it foreground permission regardless of its parent.

## Online workarounds and their limits

- **ALT injection / extra activation clicks:** Microsoft's
  `LockSetForegroundWindow` documentation says real ALT or an action that
  causes Windows to change foreground re-enables calls. Online ALT workarounds
  exploit this, but extra keys/clicks change the required input sequence. [7]
- **UI Automation `SetFocus`:** it is a valid keyboard-focus API, but its
  contract does not promise a general foreground-policy bypass. Microsoft's
  WPF HWND provider implementation uses a registered hotkey, synthesized key
  input, and `SwitchToThisWindow`. Therefore “use UIA instead” does not establish
  compliance with a no-extra-keystrokes requirement. This is a specific
  upstream implementation, not proof every UIA provider uses the same path.
  [8][9]
- **`SwitchToThisWindow`:** Microsoft's own documentation says it is not
  intended for general use and may change or disappear. It is unsuitable as
  the foundation of this test harness. [10]
- **pywinauto mouse motion:** upstream `HwndWrapper.set_focus` moves the mouse
  offscreen, shows/restores the target, calls `SetForegroundWindow`, and waits
  for GUI idle. This is evidence of a deployed workaround, not an official
  guarantee that mouse motion transfers foreground rights. It should not be
  copied on the assumption that `SendInput` itself grants permission. [11][6]
- **FlaUI:** its window `SetForeground` calls `SetForegroundWindow` and waits
  for responsiveness; native child focus uses attachment to the target thread.
  Replacing the harness with this API would not provide a distinct documented
  solution to a zero-return foreground request. [12]
- **Longer sleeps / one `PeekMessage` / deferred `WM_NULL`:** these can address
  accepted asynchronous activation, not the observed controller rejection.
  Do not expand the existing 3 s menu, 5 s peer, or 60 s case budgets. [5]
- **Topmost, hiding or killing Terminal, repeated clicks, direct
  `WM_CONTEXTMENU`:** these either do not grant foreground permission, have
  already failed the scoped experiment, or change what the first native
  right-click test actually validates. They are not selected candidates.

## Relevant upstream reports

Windows Terminal [#14131][13] concerns deadlock while a foreground GUI waits
on `system()` and Terminal attaches input queues; its original fix shipped in
Terminal 1.15.2874. [#19749][14] discusses console-host/COM foreground-permission
forwarding and further deadlocks. These establish real upstream risks around
queue attachment and permission forwarding, but neither establishes the cause
of this test's rejected call or the later foreground-null state. In particular,
one issue participant's assertion that newly spawned processes always gain
foreground rights is not a documented Windows guarantee.

The runner-images maintainer's [Windows 11 ARM investigation][15] distinguishes
an interactive desktop from successful foreground acquisition and reports
that forcing the timeout to zero did not fix that ARM case. This is a different
image and architecture; it supports measuring the API outcome, not predicting
the result on Windows Server 2025 x64.

## Sources

[1]: https://learn.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-setforegroundwindow
[2]: https://learn.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-allowsetforegroundwindow
[3]: https://learn.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-attachthreadinput
[4]: https://learn.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-systemparametersinfoa#SPI_SETFOREGROUNDLOCKTIMEOUT
[5]: https://devblogs.microsoft.com/oldnewthing/20161118-00/?p=94745
[6]: https://learn.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-sendinput
[7]: https://learn.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-locksetforegroundwindow
[8]: https://learn.microsoft.com/en-us/windows/win32/api/uiautomationclient/nf-uiautomationclient-iuiautomationelement-setfocus
[9]: https://github.com/dotnet/wpf/blob/9d00e554b9e5539f1e5b5867d30e443e93a76886/src/Microsoft.DotNet.Wpf/src/UIAutomation/UIAutomationClient/MS/Internal/Automation/HwndProxyElementProvider.cs#L1978
[10]: https://learn.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-switchtothiswindow
[11]: https://github.com/pywinauto/pywinauto/blob/46b1b0ae8b6cc1a5cb066ada7f88e44677231a47/pywinauto/controls/hwndwrapper.py
[12]: https://github.com/FlaUI/FlaUI/blob/fd7cc64ab01908a0ae4cc7d05e704caa34f98d16/src/FlaUI.Core/AutomationElements/AutomationElement.cs
[13]: https://github.com/microsoft/terminal/issues/14131
[14]: https://github.com/microsoft/terminal/issues/19749#issuecomment-3781966914
[15]: https://github.com/actions/runner-images/issues/14049#issuecomment-5217338266
