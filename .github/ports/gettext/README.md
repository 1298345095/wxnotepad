# GNU gettext host tools

This overlay updates the vcpkg baseline's gettext tools from 0.22.5 to 0.23.2,
matching Ubuntu 26.04. It retains the upstream vcpkg tool-only packaging recipe
and its MIT-licensed helper files from baseline
`d186940b2f0c4e8e5b4ff804af57d2a26fbfa091`.

`subdirs.patch` and `parallel-gettext-tools.patch` follow the 0.23.2 generated
Makefiles. The old directory-reading MSVC patch and gnulib `localtime.c` patch
are already present in 0.23.2 and are omitted. The remaining baseline patches
retain their original purpose.

This port supplies build tools and Autotools data, not the application runtime
translation library. The separate `gettext-libintl` dependency retains its
baseline recipe. CI verifies all five localization tools and exercises extraction,
catalog validation and compilation before building the application.
