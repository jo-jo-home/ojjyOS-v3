# Open bugs

Format and rules: `bugs/README.md`. Add one line per bug you find but do
not fix; remove the line in the commit that fixes it.

| ID | Symptom | Steps | Area | Severity | Card |
|---|---|---|---|---|---|
| BUG-1001 | craigslist.org fails the secure handshake (needs TLS 1.2; the browser only speaks TLS 1.3) | Web: open https://craigslist.org/ | NET | med | NET-03 |
| BUG-1002 | Clicking inside TextEdit/Notes does not move the caret (the editor has no click positioning at all) | TextEdit: click anywhere in the text | APP-TEXTEDIT | low | -- |
| BUG-1003 | Documents longer than 64 lines open read-only and are shown cut down | TextEdit: open a 70-line .txt file | APP-TEXTEDIT | low | -- |
