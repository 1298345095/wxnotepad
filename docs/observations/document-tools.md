# Document tools

Parent #239, ticket #244. Reference source: Notepad4
89b8b36e0ee2c23f73be922315f17261ccfbbe1a, `IDM_FILE_LAUNCH`,
`IDM_FILE_OPENWITH`, `OpenWithDlg` and `OpenWithDlgProc`.

Execute Document requires a named file. Open Document With also accepts an
untitled document: the chosen program starts without a document argument.
SaveBeforeRunningTools uses the existing Save/Discard/Cancel decision before
launching Execute or showing Open With. Discard leaves dirty editor contents
alone and launches the disk file. A cancelled or failed required save stops
before the chooser or launch. The same persisted preference governs Restart;
New Window retains its empty-window behavior.

The original Open With chooser lists entries in a configurable directory.
Browsing changes OpenWithDir immediately, even when the program chooser is
subsequently cancelled. An isolated original process independently recorded
an untitled launch with zero arguments and saved `OpenWithDir=..\changed-programs`
after Browse followed by Cancel. The original's untitled process current
directory in that capture was its portable application directory; named
launches explicitly use the current document's containing directory.

wxnotepad retains the original Settings key and common configuration lifecycle.
An absent directory uses the native Desktop directory, with home as the Linux
fallback. Explicit relative paths resolve against the application. Windows
keeps shell shortcut/program entries and resolves a document shortcut before
passing it to the selected program. Linux lists native executable and desktop
entries, excluding Windows executable/shortcut formats, and delegates desktop
entry argument expansion to GIO. Named launches use the document directory.
GIO's spawn working-directory handling reports both directory and executable
startup failures before returning success.

Native fixtures observe actual menu/control state, selected native directories,
independently read disk bytes and owned handler argv/current directory. Packaged
checks use the delivered English, Simplified Chinese and Traditional Chinese
catalogs, native chooser screenshots and settings round trips. Evidence and the
isolated reference executable remain outside the repository under
`F:/wxnotepad-evidence/next-native-file-tools-2026-11-18/244/`.
