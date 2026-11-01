# @balloon tab
Sleep 300ms
Type "select * from emp"
Sleep 500ms
Tab
Sleep 800ms
# @balloon newline
Ctrl+J
Type "where e."
Sleep 800ms
Up
Sleep 300ms
Ctrl+E
Sleep 300ms
Type " e"
Sleep 600ms
Down
Ctrl+E
# @balloon alias
Type "fu"
Sleep 500ms
Tab
Sleep 800ms
Type " like 'B%';"
Sleep 500ms
Enter
Sleep 2.5s
# @balloon history_ml
Up
Sleep 2s
# @balloon edit_any
Up
Sleep 300ms
Ctrl+A
Right 8
Sleep 300ms
Backspace
Type "e.fu"
Sleep 400ms
Tab
Sleep 1s
Enter
Sleep 3s
