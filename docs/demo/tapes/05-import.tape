# @balloon csv
Sleep 500ms
Type "import into orders from csv 'data/orders.csv'" Enter
Type "  options (header);" Enter
Sleep 3s
# @balloon parquet
Type "import into orders from parquet 'data/sales.parquet';" Enter
Sleep 3s
Type "select count(*), sum(amount) from orders;" Enter
Sleep 3s
