# @balloon prepare
Sleep 500ms
Type "prepare ins as insert into orders (id, customer, amount)" Enter
Type "  values (?, upper(?), ?);" Enter
Sleep 800ms
Type "exec sql ins using (1, 'acme', 99.50);" Enter
Sleep 800ms
Type "select id, customer, amount from orders;" Enter
Sleep 2s
# @balloon execfrom
Type "exec sql ins from csv 'data/bad.csv' options (header, errors = 10);" Enter
Sleep 4.5s
