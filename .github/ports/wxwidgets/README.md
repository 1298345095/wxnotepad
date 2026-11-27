# Editor dependency profile

This overlay copies wxWidgets 3.3.3#1 from the pinned vcpkg baseline
`d186940b2f0c4e8e5b4ff804af57d2a26fbfa091`. Local port version 6 retains the
earlier Windows profile, which disables
JPEG, TIFF, WebP and wxRegEx on Windows and removes their package dependencies.
The application's regular-expression commands use the pinned Notepad4 core,
including its standalone Boost.Regex engine, rather than wxRegEx.

Windows also disables the unused wxGrid, wxDataViewCtrl/wxTreeListCtrl and
Doc/View/MDI frameworks. The application owns its document model, uses one
document per top-level window, and does not use those controls. wxGridSizer is
a layout class, independent of the disabled wxGrid table control.

Version 6 also removes these unused implementations from the Windows core/base
DLLs:

- GIF, PCX, TGA, IFF and PNM image codecs and animation controls.
- Calendar/date/time pickers, wxFindReplaceDialog, wizards, splash screens,
  ActiveX containers, joysticks, wxSearchCtrl and rich tooltips.
- SVG file-export drawing and PostScript/AFM printing. SVG artwork still uses
  NanoSVG; printing and page setup still use the Windows native printer path.
- ZIP/TAR streams and archive virtual filesystems. Ordinary file streams and
  zlib remain available for PNG artwork and compressed compatibility tables.

Native ListCtrl/TreeCtrl, book controls, standard dialogs, PNG/ICO/XPM/SVG art,
accessibility, IME, clipboard, drag/drop, file watching and both rendering
technologies remain available. All toolbar images and application icon sizes
remain available, including artwork reserved for future commands. The wxWidgets
archive reference, integrity value and upstream patches are preserved.

`sdk_retained_capabilities` verifies the disabled options against the selected
SDK's actual headers when `WXNOTEPAD_VERIFY_TRIMMED_SDK=ON`. It also creates native
controls and printing/page-setup objects, round-trips zlib and PNG, rasterizes
SVG/XPM artwork and loads the application's Windows icon resource. The ordinary
native suites continue to exercise the editor's dialogs, printing and input.

Both Windows and Linux disable wxSTC. The overlay no longer fetches or copies
wxWidgets' Scintilla/Lexilla submodules, and its SDK license collection covers
wxWidgets itself. The application continues to build the separately pinned
Notepad4 core and retains its Scintilla/Notepad4 notices in `licenses/`.
wxSTC was not linked into the application or included in its distribution;
this change reduces SDK downloads, compilation and installed SDK size.

Linux otherwise keeps the original options and dependency declarations. Its
GTK overlay and static linkage are unchanged.

Version 5 synchronized the wxMSW pending-watch removal vector, and version 7
replaces that partial fix with the upstream wxWidgets MSW file-watcher fixes
(`53e793af35`, `0b744dd44c`, `06ffddba79`; upstream issues
[22381](https://github.com/wxWidgets/wxWidgets/issues/22381) and
[26866](https://github.com/wxWidgets/wxWidgets/pull/26866)). Together they
cancel outstanding overlapped `ReadDirectoryChangesW` I/O via `CancelIoEx` and
wait with `GetOverlappedResult` before freeing watch buffers, protect
`wxIOCPService::m_watches` and `m_removedWatches` with one `wxCriticalSection`,
register watches before starting the first read (with `CancelAdd` rollback),
complete pending removals under the lock in `wxFSWatcherImplMSW::SetUpWatch`
instead of reading `wxFSWatcherImpl::m_watches` unlocked, handle cancelled
completions cleanly, and keep the IOCP worker running when a single watched
directory is deleted. Upstream
[issue 15995](https://github.com/wxWidgets/wxWidgets/issues/15995) explains why
these retired watches need their own lifetime container; its earlier vector
change does not synchronize concurrent mutations.

Select this overlay for local dependency builds with
`--overlay-ports=<repository>/.github/ports`. CI already selects that directory
and includes it in the dependency cache key. Existing installed SDKs change
only when rebuilt with the overlay.
