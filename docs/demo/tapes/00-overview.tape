# @balloon editor_hl
Sleep 300ms
Type "select first_name, salary" Enter
# @balloon tab
Type "from emp"
Sleep 500ms
Tab
Sleep 800ms
Enter
Type "where salary > 100000;"
Sleep 400ms
Enter
Sleep 2.5s
Ctrl+L
# @balloon preview
Type "set preview 8;" Enter
Type "select * from employee;" Enter
Sleep 3.5s
Ctrl+L
# @balloon csv
Type "import into orders from csv 'data/orders.csv' options (header);" Enter
Sleep 2.5s
Type "select count(*) from orders;" Enter
Sleep 2.5s
