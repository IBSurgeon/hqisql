Hide
Type@1ms "select count(*) from employee;" Enter
Type@1ms "select first_name, last_name, salary" Enter
Type@1ms "from employee" Enter
Type@1ms "where dept_no = '600'" Enter
Type@1ms "  and salary > 50000;" Enter
Sleep 1s
Ctrl+L
Show
# @balloon history
Sleep 500ms
Up
Sleep 2s
Backspace
Type " order by last_name;"
Sleep 500ms
Enter
Sleep 2.5s
# @balloon search
Ctrl+R
Sleep 400ms
Type "count"
Sleep 1.5s
Enter
Sleep 3s
