# Native printing

File → Page Setup controls the paper and orientation, margins, header, footer,
color mode and print zoom. The print-related INI keys use the original numbers.
Margins are in hundredths of a millimeter in metric locales and thousandths of an
inch in US measurement locales. The first setup or confirmed print supplies the
original default margins if any margin is missing. Settings are written through
Save Settings Now or Save Settings On Exit. Printer and driver choices stay in
memory.

File → Print or Ctrl+P sends the current editor buffer to the native print system.
It does not save the document. With a live selection, the original prints the
intersecting lines, including text before and after partial-line endpoints. This
also applies when the native dialog shows **All pages**. Clear the selection to
print the whole buffer. A requested page range keeps the page numbers from the
pagination of the chosen buffer or selection.

The Windows dialog uses the same common Print dialog as Notepad4. Printing uses
its GDI device context regardless of the editor's GDI or DirectWrite setting. On
Linux, GTK provides the print dialog and Cairo/Pango printer context. wxnotepad
uses the pinned Scintilla print layout, with the current syntax styles and the
printer's metrics. Titles and footers use the global text font at the original
printer cell heights, independently of body print zoom, with one-device-pixel
black rules.

Long physical lines continue across pages without counting already printed rows
again. The pinned original printed only 456 of 1,000 ordered markers in the
14,000-byte fixture; wxnotepad prints all 1,000. This is an intentional repair of
the original printing defect and can add pages previously lost to that defect.

The bundled static GTK SDK builds the configured print backends into GTK itself
so the dialog and backends use a single GObject registry.

`original_printing` is the native CTest case. On GTK it drives the native dialog
and uses only the virtual File backend. Set `WXNOTEPAD_PRINT_TEST_OUTPUT_DIR` to an
existing directory for a run to retain `whole.pdf`, `page2.pdf`,
`all-with-selection.pdf`, `styled-wrap.pdf` (125% color), and
`monochrome-wrap.pdf` (150%, Black on white). Verify the text, color and size with
Poppler’s `pdftotext` and `pdftohtml`:

```text
python -X utf8 tests/reference_capture/verify_print_pdfs.py whole.pdf all-with-selection.pdf page2.pdf --wrapped styled-wrap.pdf --monochrome monochrome-wrap.pdf
```

The verifier compares the ordered markers and footer of the requested page with
the corresponding whole-document page. On Windows, use the scoped
`tests/reference_capture/capture_wx_printing.py` test fixture with the
`printing.exe` test target and a visible, selected Microsoft Print to PDF device.
Use the documented `ci.py probe` or `ci.py test` entry points. The counterpart
tests never send jobs to physical printers.
