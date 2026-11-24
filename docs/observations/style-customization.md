# Customize Schemes transactions

The reference is Notepad4 `89b8b36e0ee2c23f73be922315f17261ccfbbe1a`,
`Styles.cpp::Style_ConfigDlg`, `Style_ConfigDlgProc`, `Style_Import`,
`Style_Export` and `Style_ResetAll`. Independent records are retained in
`F:/wxnotepad-evidence/style-workflows-implementation-2026-11-23/264`.
The reference executable runs from an owned copy on a named private desktop;
the observer uses direct messages to its owned controls.

Preview applies the edited style to the actual editor. It does not establish
a new cancellation baseline. Cancel restores the dialog's opening style
strings, filename extensions and custom-color palette, including after Preview
or import. OK retains the edited values. Neither response saves preferences by
itself. Normal Save Settings and dirty theme transitions control persistence.

The original does not back up `UseDefaultCodeStyle`. Import and Reset All can
change this choice even when the dialog is subsequently cancelled. Reset All
sets it to true for Text File and false for other schemes. The adaptation
preserves this distinction when restoring the shared style profile.

In Default, Reset restores source defaults. In Dark, Reset reloads the selected
style from the saved Dark profile, falling back to its source default when
absent. Reset All reloads Dark styles/custom colors, restores source filename
extensions, and resets inheritance choices. Empty extension edits restore the
source extension list; empty style strings are valid explicit values.

Original scheme imports merge present style keys and inheritance choices and
ignore empty extension entries. Exports contain `[File Extensions]` and style
sections named by stable original identities. The approved wxnotepad dialog
also imports/exports the original `[Custom Colors]` format (`01=#RRGGBB`
through `16=#RRGGBB`). This extends the original scheme-file IO, which omits
that section; original Notepad4 still reads the scheme sections and ignores
the additional palette section during scheme import.

Style edits use the same `FontSettings`, stable style metadata and
`ApplySyntaxStyles` path as theme switching. Preview and cancellation preserve
document bytes, encoding, selection, folds and native Undo/Redo/save points.
Filename extensions use the shared syntax catalog for file detection and
native Open/Save filter construction; dialect, favorite and default identities
remain distinct.
