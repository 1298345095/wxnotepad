#pragma once

namespace NativeDialogs {
// Controls only GIO completion order in the dedicated native regression binary.
void EnableDirectoryLoadOrder();
bool DirectoryLoadOrderCompleted();
} // namespace NativeDialogs
