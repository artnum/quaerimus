# qury_search

The idea is to have the ldap syntax for a sql search. Why is that ? Because 
ldap filter is a nice way to express search request as http parameter.

## example 1

```
GET https://example.com/endpoint?search=(&(mail=*)(active=*)) id,firstname,lastname
```

Translate to 

```sql
SELECT id,firstname,lastname 
FROM   endpointTable 
WHERE  mail IS NOT NULL 
  AND  active IS NOT NULL;
```

## example 2

```
GET https://example.com/endpoint?search=(&(mail=*)(active=*))
```

Translate to 

```sql
SELECT *
FROM   endpointTable 
WHERE  mail IS NOT NULL 
  AND  active IS NOT NULL;
```

## example 3

```
GET https://example.com/endpoint?search=(&(age>21)(name~=paul))
```

Translate to
```
SELECT *
FROM endpointTable
WHERE age  >    21
  AND name LIKE paul
```

## qury_search signature

bool qury_search(qury_stmt_t *stmt, const char *table, size_t table_len
                 const char *filter, size_t filter_len);


## Named placeholder

Of course the idea is to have placeholdre :

```c

if (!qury_search(stmt, "my_table", 0, "(&(name~=:name)(age>:age)) id,name,lastname", 0)) {
    /* error */
}

qury_stmt_bind(stmt, "name", ...);
qury_stmt_bind(stmt, "age", ...);
```


