# Simplified Chinese catalog provenance

`zh_CN.po` is the complete application catalog for issue #205. All translations
were authored or reviewed directly in Simplified Chinese; no external translation
service or script translated the messages. Scripts only extracted source text,
formatted the model-authored text and checked the completed catalog.

Reused wording comes from `locale/zh-Hans/Notepad4.rc` at the repository's pinned
Notepad4 commit `89b8b36e0ee2c23f73be922315f17261ccfbbe1a`, available at
<https://github.com/zufuliu/notepad4/blob/89b8b36e0ee2c23f73be922315f17261ccfbbe1a/locale/zh-Hans/Notepad4.rc>.
The PO header records the pin; entry comments identify the original menu command,
dialog control or string resource where wording was reused or adapted.

The shared menus retain the original command terminology and all source shortcut
suffixes. Mnemonics were reviewed within the actual wxnotepad sibling groups.
Restart uses T and Open As GBK uses B in File; Settings uses F for File Change
Notification and O for
Other Settings; URL Decode uses R in Convert. The Unicode-control submenu uses
unique mnemonics, including 1/2 for directional isolates. These choices avoid
collisions while keeping the command shortcuts intact. Original malformed
mnemonics in the file-monitor and shuffle controls were repaired.

`IDS_UNTITLED` remains “未命名”. `IDS_CHANGE_LANG_RESTART` adapts only the
application name to wxnotepad. The Go To Line title expands the original
“转到” to “跳转到行”. New file-operation errors are translated as complete
sentences while preserving the format arguments for paths and system details.
Syntax/product names, encoding identifiers and file-filter patterns retain their
original spelling; unchanged translations of those labels are intentional.
Chinese uses `nplurals=1; plural=0;`, including all five plural messages.

The original BSD notice below is retained along with `licenses/Notepad4.txt`.

```text
https://www.flos-freeware.ch
https://xhmikosr.github.io/notepad2-mod/

Notepad4 Copyright © 2011-2026 Zufu Liu and All contributors.
matepath Copyright © 2011-2026 Zufu Liu and All contributors.
Notepad2-mod Copyright © 2010-2017 XhmikosR and All contributors.
Notepad2 Copyright © 2004-2012 Florian Balmer.
metapath Copyright © 1996-2012 Florian Balmer.
All rights reserved.

Redistribution and use in source and binary forms, with or without
modification, are permitted provided that the following conditions are
met:

1. Redistributions of source code must retain the above copyright
notice, this list of conditions and the following disclaimer.

2. Redistributions in binary form must reproduce the above copyright
notice, this list of conditions and the following disclaimer in the
documentation and/or other materials provided with the distribution.

3. Neither the name of Florian Balmer nor the names of its contributors
may be used to endorse or promote products derived from this software
without specific prior written permission.

THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS
IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED
TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A
PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
(INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
```
