# Original search-wrap confirmation and feedback

Compatibility source: Notepad4 `89b8b36e0ee2c23f73be922315f17261ccfbbe1a`,
`src/Edit.cpp` (`EditFindNext`, `EditFindPrev`, `EditReplace`),
`src/Dialogs.cpp` (`InfoBox`, `InfoBoxDlgProc`), and `src/Notepad4.rc`.

Find Next starts at the selection end; Previous starts at its beginning. A failed
search offers OK/Cancel before searching the whole document again only when
wrapping is enabled and the initial position is beyond the beginning (Next) or
before the end (Previous). Cancel preserves the selection and view and suppresses
the subsequent not-found warning. One match can be selected again after wrapping;
the original has no separate "only match" message.

Select to Next/Previous use the same matcher but never wrap. Their selection
anchors are the smaller/larger original selection endpoint respectively. The
original shortcuts are Ctrl+Alt+F2 and Ctrl+Alt+Shift+F2.

Replace initially searches from selection start to document end. A candidate
whose endpoints differ from the selection is selected first. An exact match is
replaced, followed by another forward search from the replacement end. Both
searches use the forward wrap confirmation. Cancelling the subsequent search
retains the replacement and collapses the caret at its end.

The original `FindReplaceOption` default is 2, with No Wrap stored in bit 64.
Search feedback uses `[Suppressed Messages]` fields `MsgFindWrap1`, `MsgFindWrap2`
and `MsgNotFound`. Missing fields default to 0 (show the dialog); 1 suppresses it
and returns OK; 2 shows it with the suppression checkbox disabled. Checking
"Don't show this message again" writes 1 immediately, including when Cancel is
chosen or Save Settings On Exit is disabled. The three fields are independent.

`original_search_wrap_feedback` drives owned native controls and actual modal
dialogs. `original_search_message_settings` checks portable configuration with
independent processes and independently reads the resulting INI. Existing search
fixtures continue to verify their document results while accepting feedback;
these two fixtures verify the feedback choices themselves.
