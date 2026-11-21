# Original editor context menu

The reference is Notepad4 `89b8b36e0ee2c23f73be922315f17261ccfbbe1a`,
`Notepad4.rc::IDR_POPUPMENU` and `Notepad4.cpp::WM_CONTEXTMENU`.
Native observations use an isolated portable reference process and owned files.
The nine commands and four separators retain the original order, captions and
mnemonics in English, Simplified Chinese and Traditional Chinese.

Right-click preserves an existing selection, including reversed, rectangular
and multiple selections. With no selection, the host uses the original core's
PositionFromPoint and GotoPos. The keyboard menu key and Shift+F10 anchor the
native popup at the current caret. wxWidgets converts the current client point
to native screen coordinates. On Wayland the menu's native size is measured
without mapping a popup, then placed with the same work-area calculation as
completion and CallTip. This keeps entries within the monitor when a compositor
does not honor the menu's requested edge constraints.

Undo, Redo, clipboard commands, Delete and Select All use the existing editor
operations. The original nonempty-document availability for Cut and Delete is
retained in read-only mode; the core rejects document changes. Paste follows
CanPaste. Toggle Fold uses the existing Current Block command, including its
original selection endpoints inside hidden text. Open uses the existing selected
path/link action and its ordinary independent editor/desktop launch paths.
Popup construction adds no command bindings.

The right-button bridge reuses the original Scintilla RightButtonDownWithModifiers
to cancel completion and CallTip modes. wxSTC's right-down focus and event-skip
pattern lets the native host emit the application context-menu event. wxGTK
normally emits it only for RIGHT_DOWN; its filtered RIGHT_DCLICK receives the
same application event so rapid repeated right-clicks still open one popup.

`original_editor_context_menu` exercises physical mouse/keyboard invocation,
actual native menu groups and availability, Escape and outside-click cancellation,
read-only state, directed selections, independent stock-widget clipboard
outcomes, Current Block and a real child loading disk bytes and a caret location.
Its placement checks use scrolled and wrapped documents and native monitor bounds.
Wayland menu placement is observed after GtkMenu's public `popped-up` notification
and first native frame, including asynchronous configure/map. Real menu keys and
mnemonics supply the Wayland input serial for clipboard actions. The `_zh_CN` and `_zh_TW` cases inspect both
pointer and keyboard popups using the compiled application catalogs.

The Windows focus-resumption check inserts a committed literal character through
native Unicode input packets. This checks focus, exact insertion and Undo after
Escape; it does not establish real IME preedit interaction. Failure-only native
observations distinguish menu readiness/key receipt and independent peer focus
failures from command or clipboard byte mismatches.
