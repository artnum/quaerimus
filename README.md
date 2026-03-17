# Quaerimus

> [!CAUTION]
> This is a work in progress. The API is not stable, anything can change at
> any time.

Abstraction for libmariadb to allow named parameters, as in [PHP
PDO](https://www.php.net/manual/fr/pdo.prepare.php).

The underlying code use [prepared
statement](https://dev.mysql.com/doc/refman/8.4/en/sql-prepared-statements.html).

In PHP, when you prepare a statement with a named parameter, you bind with the
colon in the name. In quaerimus, the colon is not needed (and not wanted) :

```php
$stmt = $dbh->prepare("SELECT * FROM table WHERE id = :id");
$stmt->bindParam(":id", 10, PDO::PARAM_INT);
```

In quaerimus :

```c
qury_stmt_t *stmt = qury_new(qury_conn, NULL);
qury_prepare(stmt, "SELECT * FROM table WHERE id = :id", 0);
qury_bind_bind_int(stmt, "id", 10);
```

## Custom allocator

A custom allocator can be used, still a work in progress on how I want it to
be.

## Basic operation

A basic select with quaerimus would be like :

```c
qury_conn_t conn;
qury_conn_init(&conn);

mysql_library_init(0, NULL, NULL);

if (!mysql_real_connect(conn.mysql, "localhost", "user", "password", NULL,
                        0, NULL, 0)) {
    exit(EXIT_FAILURE);
}

/* Not using a custom allocator */
qury_stmt_t *stmt = qury_new(&conn, NULL);
if (!stmt
    || !qury_prepare(stmt, "SELECT * FROM table WHERE size > :size", 0)
    || !qury_select_db(stmt, "my_db")
    || !qury_stmt_bind_int(stmt, "size", 10)
    || !qury_execute(stmt)) {
    qury_free(stmt);
    exit(EXIT_FAILURE);
}

while(qury_fetch(stmt)) {
    qury_bind_t *value = NULL;
    
    if(qury_get_value(stmt, "name", &)) {
        char *name = qury_get_cstr(v);
        if (name) {
            printf("NAME %s\n", name);
        }
    }
}

qury_free(stmt);
mysql_close(conn.mysql);
mysql_libaray_end();
```



