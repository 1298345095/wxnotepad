# Editor dependency profile

This overlay copies wxWidgets 3.3.3#1 from the pinned vcpkg baseline
`d186940b2f0c4e8e5b4ff804af57d2a26fbfa091`. Local port version 3 disables
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
available. Existing upstream sources, integrity values and patches are preserved.

Linux keeps the original options and dependency declarations. Its GTK
overlay and static linkage are unchanged.

Select this overlay for local dependency builds with
`--overlay-ports=<repository>/.github/ports`. CI already selects that directory
and includes it in the dependency cache key. Existing installed SDKs change
only when rebuilt with the overlay.
