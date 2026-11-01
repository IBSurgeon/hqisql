-- Run on a copy of employee.fdb before each scene.
create table orders (
  id integer,
  customer varchar(30),
  amount numeric(12,2),
  order_date date
);
commit;
