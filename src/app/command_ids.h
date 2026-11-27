#pragma once

#include <wx/defs.h>

// Commands shared by menus, toolbar buttons and status fields use an identity
// independent of their translated label, accelerator or menu position.
enum class AppCommand : int {
    NewWindow = wxID_HIGHEST + 1,
    SaveCopy,
    WordWrap,
    ZoomIn,
    ZoomOut,
    ZoomLevel,
    SyntaxScheme,
    CustomizeSchemes,
    FoldDefaultLevels,
    ExecuteDocument,
    GoToLine,
    ChooseEncoding,
    EolCrLf,
    EolLf,
    EolCr,
    AlwaysOnTop
};
