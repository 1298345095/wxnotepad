# Delivered native menu map

The relative order follows Notepad4 `89b8b36e0ee2c23f73be922315f17261ccfbbe1a`,
`src/Notepad4.rc`, restricted to delivered commands. Separators divide the
original command groups. Shortcut labels describe the application's existing
bindings; relocating an item retains its handler and update-UI state.

```text
File
  New; New Window; Open; Open As GBK; Open As UTF-16LE
  Save; Save As; Save Copy; Save with Original Time
  File Mode > Read Only Mode
  Revert
  Reload > With Encoding; As UTF-8
  Line Endings > Windows (CR+LF); Unix/macOS (LF); Classic Mac OS (CR); Default
  Update File Time
Edit
  Undo; Redo
  Cut; Copy; Paste; Swap
  Clear Clipboard
  Copy to Clipboard > File Name; File Name and Ext.; Full Path Name; Copy All; Copy Add
  Selection
    Duplicate
    Toggle Line Comment; Toggle Block Comment; Indent; Unindent
    Strip Trailing Blanks; Strip First Character; Strip Last Character; Strip Leading Blanks
    Merge Blank Lines; Remove Blank Lines; Merge Duplicate Lines; Remove Duplicate Lines
    Pad With Spaces; Compress Whitespace
  Enclose Selection > With; HTML/XML Tag; Triple Single Quotes; Triple Double Quotes; Triple Backticks
  Lines
    Move Up; Move Down; Transpose
    Sort Lines; Modify Lines; Align Lines
    Duplicate Line; Cut Line; Copy Line; Delete Line
    Join Lines; Split Lines; Column Wrap; Join Paragraphs
  Convert
    Upper Case; Lower Case; Invert Case; Title Case; Sentence Case
    Tabify Selection; Untabify Selection; Tabify Indent; Untabify Indent
    To Hexadecimal; To Decimal; To Binary; To Octet
    Escape HTML/XML Chars; Unescape HTML/XML Chars
    URL Encode; URL Component Encode; URL Decode
  Insert
    HTML/XML Tag; New GUID; Unicode Control Character
    File Name; File Name and Extension; Full Path Name
    Current Date; Current Date Time; UTC Date Time; Unix Timestamp
    Other Timestamps > Millisecond; Microsecond; Nanosecond
    Time/Date (Short Form); Time/Date (Long Form)
    Encoding Identifier; Script Shebang Line
  Special > Character to Hex; Hex to Character; Escape C Chars; Unescape C Chars
Search
  Find; Find Next; Find Previous; Replace
  Find Matching Brace; Select to Matching Brace; Select Lines in Current Block
  Select to Next; Select to Previous
  Bookmarks > Toggle; Goto Next; Goto Previous; Select All; Clear All
  Goto
    Goto Line
    Goto Block Start; Goto Block End; Goto Previous Block; Goto Next Block
    Goto Previous Sibling Block; Goto Next Sibling Block
    Goto Selection Start; Goto Selection End
View
  Caret Style
  Word Wrap; Long Line Marker; Indentation Guides
  Show Whitespace; Show Line Endings; Show Wrap Symbols
  Visual Brace Matching; Highlight Current Block; Highlight Current Line; Mark Occurrences
  Line Numbers; Bookmark Margin; Change History Marker
  Show Code Folding; Toggle Folds > Collapse All; Expand All
  Zoom > Zoom In; Zoom Out; Zoom Level; Reset Zoom
Scheme
  Automatic; delivered syntax schemes
Settings
  Insert Tabs as Spaces; Tab Settings; Long Line Settings; Auto Indent
  Select and Edit Options
    Copy Selection As Find Text; Copy Paste Buffer As Find Text
    Line Selection Mode > None; Visual Studio; Old Visual Studio; Normal
  Appearance > Auto-Shrink Horizontal Scrollbar
  File Change Notification (Alt+F5)
  Other Settings > Remember Search Options
  Save Settings On Exit; Save Settings Now
```

The settings persistence entries and find-text preferences appear when the frame
has an application settings store. Opening the menu does not save preferences.

wxnotepad retains these specific placements:

- Explicit Open As GBK and UTF-16LE stay next to Open in File.
- The delivered HTML/XML escapes and URL conversions originate from original
  context actions. They remain in Convert, after case/tab/radix conversion.
- HTML/XML Tag intentionally appears in both Enclose Selection and Insert.
- Automatic and the currently delivered syntax choices occupy Scheme directly.
- Auto Indent stays next to tab and long-line preferences in Settings.
- Auto-Shrink Horizontal Scrollbar occupies Settings > Appearance.
- The delivered Collapse All and Expand All actions occupy View > Toggle Folds.
- Goto Selection Start/End now have visible entries at the original Goto positions,
  retaining Ctrl+Shift+Comma and Ctrl+Shift+Period.
