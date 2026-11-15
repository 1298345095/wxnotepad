# Editor dependency profile

This overlay copies wxWidgets 3.3.3#1 from the pinned vcpkg baseline
`d186940b2f0c4e8e5b4ff804af57d2a26fbfa091`. Local port version 4 disables
JPEG, TIFF, WebP and wxRegEx on Windows and removes their package dependencies.
The application's regular-expression commands use the pinned Notepad4 core,
including its standalone Boost.Regex engine, rather than wxRegEx.

Windows also disables the unused wxGrid, wxDataViewCtrl/wxTreeListCtrl and
Doc/View/MDI frameworks. The application owns its document model, uses one
document per top-level window, and does not use those controls. wxGridSizer is
a layout class, independent of the disabled wxGrid table control.

Printing and page setup remain enabled for the original Notepad4 functionality.
Native ListCtrl/TreeCtrl, book controls, standard dialogs, PNG/SVG art, zlib,
accessibility, IME, clipboard, drag/drop and both rendering technologies remain
available. The wxWidgets archive reference, integrity value and upstream patches
are preserved.

Both Windows and Linux disable wxSTC. The overlay no longer fetches or copies
wxWidgets' Scintilla/Lexilla submodules, and its SDK license collection covers
wxWidgets itself. The application continues to build the separately pinned
Notepad4 core and retains its Scintilla/Notepad4 notices in `licenses/`.
wxSTC was not linked into the application or included in its distribution;
this change reduces SDK downloads, compilation and installed SDK size.

Linux otherwise keeps the original options and dependency declarations. Its
GTK overlay and static linkage are unchanged.

Select this overlay for local dependency builds with
`--overlay-ports=<repository>/.github/ports`. CI already selects that directory
and includes it in the dependency cache key. Existing installed SDKs change
only when rebuilt with the overlay.
