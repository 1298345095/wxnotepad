# Dependency notices

- `Notepad4.txt`: original pinned repository's complete notices, including the
  Notepad4 BSD 3-Clause and Boost Software License 1.0 text.
- `Scintilla.txt`: original pinned Scintilla license.
- `wxWidgets.txt`: copyright/license file from the vcpkg wxWidgets 3.3.4 overlay.
  wxWidgets uses the wxWindows Library Licence exception to LGPL. wxSTC is
  disabled; the separately built original core retains `Scintilla.txt` above.

The original archive also retains all source notices. Other transitive vcpkg
dependency notices remain under the chosen installation's `share/<port>/copyright`.
The application package also retains the dependency notices.

Linux Calculate Expression statically links QuickJS-ng 0.16.2 overlay revision 1
(MIT and Unicode-3.0), retaining the pinned upstream archive plus the original
numeric NaN compatibility patch. Its complete notices and package/SPDX records
are in the selected SDK's `share/quickjs-ng/`; the application bundler retains the
copyright notice under `licenses/vcpkg/quickjs-ng/`. The engine's optional libc
I/O modules and command-line programs are not application runtime dependencies.
Windows uses the original system ActiveScript/propsys components.
