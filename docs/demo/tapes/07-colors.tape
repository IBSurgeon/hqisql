# @balloon colors_builtin
Sleep 500ms
Type "select first_name, last_name, salary" Enter
Type "from employee  -- sales" Enter
Type "where dept_no = '600' and salary > 50000;" Enter
Sleep 2.5s
Ctrl+L
Sleep 500ms
Hide
Sleep 500ms
Ctrl+D
Sleep 1s
Type@1ms "HQISQL_COLORS=colors-demo.txt fb/bin/hqisql -user SYSDBA db/employee.fdb" Enter
Sleep 2.5s
Ctrl+L
Sleep 500ms
Show
# @balloon colors_own
Sleep 500ms
Up
Sleep 1.5s
Enter
Sleep 3s
