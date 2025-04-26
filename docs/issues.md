
# PostgreSQL Snippets



## If occurs the issue:
```sql
[postgres@ppqwqqq ~]$ createuser --interactive --pwprompt
输入要增加的角色名称: ppqwqqq
为新角色输入的口令: 
再输入一遍: 
新的角色是否是超级用户? (y/n) y
createuser: 错误: 连接到套接字"/run/postgresql/.s.PGSQL.5432"上的服务器失败:没有那个文件或目录
	Is the server running locally and accepting connections on that socket?
```

[Solution Link](https://unix.stackexchange.com/questions/294926/unable-to-start-posgtresql-the-reason-isnt-clear)

Follow these steps to fix it: 
1. Create the data directory 
   `sudo mkdir /var/lib/postgres/data`
2. set `/var/lib/postgres/data` ownership to user `postgres`
   `chown postgres /var/lib/postgres/data`
3. As user `postgres` start the database
   `sudo -i -u postgres`
   `initdb -D '/var/lib/postgres/data'`

## If occurs the issue:
```sql
WARNING:  database "postgres" has a collation version mismatch
描述:  The database was created using collation version 2.40, but the operating system provides version 2.41.
提示:  Rebuild all objects in this database that use the default collation and run ALTER DATABASE postgres REFRESH COLLATION VERSION, or build PostgreSQL with the right library version.
```

Follow the step to fix it:
1. Execute the sql: 
```sql
ALTER DATABASE postgres REFRESH COLLATION VERSION;
```
