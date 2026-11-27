# File Favorites

File → Favorites follows Notepad4 `89b8b36e0ee2c23f73be922315f17261ccfbbe1a`:
Open Favorites (`Alt+I`), Add Current File (`Alt+K`), and Manage (`Alt+F9`).
These file links are separate from Favorite Schemes.

The original source boundaries are `Notepad4.cpp`'s `IDM_FILE_OPENFAV`,
`IDM_FILE_ADDTOFAV`, `IDM_FILE_MANAGEFAV`, `LoadSettings` and `SaveSettings`;
`Dialogs.cpp::FavoritesDlg`, `AddToFavDlg`, and `OpenWithDlgProc`; and
`Helpers.cpp::PathCreateFavLnk` and `PathGetLnkPath`. The independent original
capture is reproducible with `tests/reference_capture/capture_file_favorites.py`.
It checks its own executable copy against the pinned baseline's recorded SHA256.

`[Settings] Favorites` uses the same executable-directory-relative paths,
environment expansion, and `%CSIDL:MYDOCUMENTS%` token as other imported path
preferences. An absent or empty value uses the current user's Documents directory.
The settings store preserves unrelated INI lines when saving. The directory
button in the Favorites chooser changes this preference; Manage opens the actual
directory with the existing desktop-launch adapter.

Windows creates native `.lnk` files with `IShellLink`/`IPersistFile` and obtains
display names through the shell. Opening reads the shortcut's actual target,
expands environment fields, and resolves relative targets against the link's
directory. It does not invoke the target's default association. Existing favorite
names are rejected before saving a link.

Linux maps favorite entries to symbolic links with the entered display name,
without a `.lnk` suffix. Absolute and relative symbolic links, including links to
directories, resolve through the filesystem. Windows `.lnk` files are unsupported
on Linux and produce a localized resolution error; their binary contents are
never opened as document text. Ordinary files and directories in the Favorites
directory remain selectable, as in the original's all-objects list.

Open Favorites asks to save before showing its chooser. Save remains effective
if that chooser is later cancelled; Discard permits loading the selected favorite;
Cancel stops before the chooser. A file uses the existing document-load path,
including encoding, scheme detection, file watching, and recent files. A directory
opens the existing native file chooser at that directory. A successfully resolved
link to a missing target offers the original Create Yes/No/Cancel choice. Yes
creates a new empty file exclusively and loads it through the existing entry;
No retains the current document. **Approved original defect fix:** Cancel retains
the current window and document. The original posts Exit after this Cancel.
The user explicitly selected “取消后保留窗口，记录为缺陷修复（推荐）”.
The complete independent original Yes/No/Cancel observations remain unchanged.
Duplicate names, permission failures, malformed links and unsupported shortcuts
report localized errors without replacing the document.

The Add dialog permits horizontal resizing only, and the Favorites chooser permits
both dimensions. Their controls use native size measurements and translated labels.
`packaged_file_favorites` exercises the real bundled application, OS links and
native controls in English, Simplified Chinese and Traditional Chinese. Its
directory-handler registration is confined to its private Linux XDG directories;
Windows Manage uses the existing independent Explorer observer and closes only
the newly created owned-directory window. File Add/Open do not change host file
associations.

On Windows, native Alt+F9 dispatch runs in `MSWTranslateMessage`, after message
retrieval. Dispatching Manage from the keyboard hook lets the shell's nested
message processing replay F9 as Select Encoding. The packaged regression checks
the exact Explorer directory, the absence of an extra modal dialog, and a clean
application exit after the native shortcut.
