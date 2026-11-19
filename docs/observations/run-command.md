# Run Command platform behavior

Tools → Run Command and Ctrl+R open the same native dialog. The existing
SaveBeforeRunningTools decision completes before the dialog is constructed.
Discard permits the tool while retaining the modified buffer. Cancel, a canceled
Save As, or a failed save prevents the dialog. A named document supplies its
quoted full path; an untitled document supplies an empty command.

The launcher uses the named document's directory, or the frame's original
launch directory when untitled. Changing the process working directory later
does not change that fallback. The registered `original_run_command` fixture
launches an independent recorder and reads its actual cwd and argv, including
an executable copied into a path containing spaces and Chinese characters.

## Windows

The first program and raw remaining command line follow pinned Notepad4
89b8b36 `Helpers.cpp::ExtractFirstArgument` and `Dialogs.cpp::RunDlgProc`.
Only ASCII spaces trim/separate an unquoted program. A leading quote extracts
through the next quote; the original `-appid="value with spaces"` handling is
retained. Empty first arguments disable Run even when trailing text exists.
Remaining parameters are kept as native Windows command-line text and expanded
with `ExpandEnvironmentStringsW`. `ShellExecuteExW` uses
`SEE_MASK_DOENVSUBST` for executable environment substitution.

The native browser expands the current executable and arguments before showing
its chooser. Accept replaces the executable and retains the expanded arguments;
cancel retains the original command text. Ordinary launch failure leaves the
dialog editable with feedback and focus on Command. Successful ordinary launch
closes it.

`Notepad4`, `Notepad4.exe`, `wxnotepad`, and `wxnotepad.exe` refer to the current
executable. As in the original quick-exit branch, the dialog ends before the
self launch, and Windows retains `SEE_MASK_NOZONECHECKS`. An alias never resolves
to a separate personal Notepad4 installation.

## Linux

The native mapping is an argv launcher, using the existing GLib exec-confirmed
spawn operation. Unquoted whitespace separates arguments; single and double
quotes group text, including empty arguments. Adjacent quoted/unquoted pieces
form one argument. Backslash escapes the next character outside quotes; inside
double quotes it escapes double quote, backslash, dollar, or newline. An escaped
newline is removed. Single quotes retain their contents literally.

`$NAME` and `${NAME}` expand outside single quotes, including inside double
quotes. Expansion produces text in the same argument, without word splitting.
An unset variable expands to empty text. Expansion is performed once. Names
contain ASCII letters, digits, and underscore. The browser expands the program
for its initial file and retains the remaining argument text on accept.

Pipelines, semicolons, redirections, wildcard patterns, and command substitutions
are ordinary argument text. To request a shell, launch it explicitly, for example
`/bin/sh -c 'printf "%s\n" "$HOME"'`. The dialog never implicitly sends its
command to a shell. An unfinished quote or escape receives editable feedback.

## Width and settings

The pinned original stores `RunDlgSizeX` in `[Window Position WIDTHxHEIGHT]`.
`Helpers.cpp:958` defines `DlgBaseUnit={4,8}`; `ResizeDlg_Proc` on destruction
normalizes the **client width** as `MulDiv(clientWidth,4,MapDialogRect({4,8}).cx)`.
`ResizeDlg_InitEx` performs the inverse. This is horizontal dialog units rather
than physical pixels or device-independent pixels.

When no wx width exists, settings choose the original entry for the current
monitor dimensions. If those dimensions are absent on the new desktop, the first
saved positive original width is used. The dialog converts that client width
with `ConvertDialogToPixels`, using its native font and DPI, and enforces its
native minimum size. Original entries remain intact.

After closing, wxnotepad remembers the native outer width in device-independent
pixels as `[wxnotepad] RunDlgSizeX`; zero means the template width. This follows
the shared SaveSettings/explicit-save lifecycle. Cancel still remembers a resize,
as in the original. No new per-monitor settings schema or history is introduced.
