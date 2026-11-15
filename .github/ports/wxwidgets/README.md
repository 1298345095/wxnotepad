# Editor image dependencies

This overlay copies wxWidgets 3.3.3#1 from the pinned vcpkg baseline
`d186940b2f0c4e8e5b4ff804af57d2a26fbfa091`. Local port version 2 disables
JPEG, TIFF and WebP on Windows and removes their Windows package dependencies.
The application uses native bitmaps and PNG/SVG art; those facilities remain
enabled. Existing upstream sources, integrity values and patches are preserved.

Linux keeps the original codec options and dependency declarations. Its GTK
overlay and static linkage are unchanged.

Select this overlay for local dependency builds with
`--overlay-ports=<repository>/.github/ports`. CI already selects that directory
and includes it in the dependency cache key. Existing installed SDKs change
only when rebuilt with the overlay.
