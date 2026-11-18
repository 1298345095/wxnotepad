# GTK3 overlay

This overlay copies GTK3 3.24.52#2 from the project's pinned vcpkg baseline
`d186940b2f0c4e8e5b4ff804af57d2a26fbfa091`. Local port version 3 adds
`gtk3-pathbar-button-cancellation.patch`; the upstream archive, integrity value,
features, dependencies and existing build patches are preserved.

Local port version 4 compiles the configured GTK print backends into the static
Linux GTK library. Building separate modules against static GTK and GLib gives the
print dialog and the modules independent GType registries. The printer list then
stays empty even with the packaged File module present. Built-in backends reuse the
same registry and no longer depend on an SDK module path. Shared GTK builds keep
upstream module loading and the configured backends and their dependencies are
unchanged.

An asynchronous symbolic-icon query can outlive an individual pathbar button in
the native file chooser. The patch cancels that request at button destruction,
before child images are destroyed, and retains the cancellation token until its
button-side cleanup. Normal completion releases that extra reference. Existing
parent cancellation order and lifetime semantics are preserved.

The CI dependency install explicitly selects `.github/ports`; its binary-cache
inputs include this overlay. A local dependency build can select the same patch
with `--overlay-ports=<repository>/.github/ports`. Existing installed SDKs remain
unchanged until rebuilt with that option.

The captured failure is the GTK `set_button_image_get_info_cb` path. Related
upstream history: <https://github.com/GNOME/gtk/commit/559d32420c21a5888d0cc37169157e91780b6c9d5f>.
Remove the overlay after adopting an upstream GTK port that includes equivalent
per-button cancellation and token ownership.
