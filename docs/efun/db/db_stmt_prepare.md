---
title: db / db_stmt_prepare
---
# db_stmt_prepare

### NAME

    db_stmt_prepare() - prepares a parameterized SQL statement

### SYNOPSIS

    mixed db_stmt_prepare(int handle, string sql);

### DESCRIPTION

    Prepares `sql` on the database connection identified by `handle` and
    returns a statement handle. Use `?` placeholders for values that will
    be supplied to db_stmt_exec(). Prepared statements are currently
    supported only for MySQL connections.

    Returns a statement handle on success or an error string if MySQL
    cannot prepare the statement. An invalid connection handle raises an
    error.

### SEE ALSO

    db_stmt_exec(3), db_stmt_close(3), db_connect(3), db_close(3)