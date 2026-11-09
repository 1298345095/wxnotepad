# File times

File → Save with Original Time is available for modified documents. It uses
the ordinary Save flow, including first-save and read-only Save As decisions,
encoding-loss confirmation and configured save cleanup. A successful save
updates the document identity, language and save point normally while restoring
the destination's original writable times. A new destination keeps its initial
creation-time values captured before content is written.

On Windows, the preserved fields are creation, access, modification and change
time. On Linux, they are access and modification time; ctime changes according
to filesystem rules. Timestamp precision follows the destination filesystem.
The existing safe temporary-file replacement and recoverable write failures
apply. As in Notepad4, failure to query or restore times does not fail an
otherwise successful content save.

File → Update File Time is available for named documents and sets the backing
file's modification time to the current time. It retains the other writable
times, backing bytes, live text, dirty state, save point, undo/redo history,
selection, encoding and document identity. It skips save cleanup and encoding
conversion. Like the original, it creates an empty backing file when the named
file has disappeared and its parent directory remains writable; failures are
silent. Successful updates refresh the active file-watching baseline.

On Linux, a recreated backing file receives owner read/write permissions,
subject to the process umask. Existing files retain their access permissions.
