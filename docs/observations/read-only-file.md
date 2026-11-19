# Disk file protection

File → File Mode → Read Only File changes the named file's protection. Read Only
Mode (F10) remains editor protection. Untitled documents have no disk toggle.

The pinned original `IDM_FILE_READONLY_FILE` changes only `FILE_ATTRIBUTE_READONLY`,
re-reads the result and clears F10 when the file becomes writable. Setting disk
protection preserves the existing F10 state. The port uses the same document
session, title and Save / Save As behavior.

On Linux the command changes only `S_IWUSR` and is enabled only when `st_uid`
matches the effective UID. In particular, 0664 becomes 0464 and returns to 0664;
group/other permissions and special mode bits survive. This affects the owner's
other programs too. The checkbox observes the owner-write bit, independently of
whether group membership or an ACL grants effective write access. For non-owners,
the command is disabled with owner-only help, while existing title and Save
protection retain their effective-access policy. Actual write failures use the
ordinary Save error flow. No ACL or elevation mechanism is introduced.

`tests/read_only_file.cpp` drives actual File Mode commands, independently reads
NTFS attributes or POSIX metadata and bytes, and covers selection/history/format,
Recent Files, the original disk/F10 sequence, Save As/reopening, external changes,
removed files and non-owners. Linux follows an owned temporary symlink to
`/proc/self/status` after Open to exercise a real chmod failure without privileged
setup or persisting proc content. Windows also denies `FILE_WRITE_ATTRIBUTES` on an
owned temporary fixture, checks native failure feedback and restores its original
DACL. Existing `original_read_only_mode` covers ordinary protected Save choices.

Original sequence evidence and native Windows/X11/Wayland runs for #241 are under
`F:/wxnotepad-evidence/next-native-file-tools-2026-11-18/241/`. The isolated original
executable is revision `89b8b36e0ee2c23f73be922315f17261ccfbbe1a`, SHA-256
`c4ae363348fe833b39c9d9b77f1f0b85565f140f7c5e52c51a0b91cf896227d9`.
`original-03/observations.json` contains the successful disk/F10 sequence;
earlier startup-race failures remain preserved separately.
