---
title: db / db_stmt_exec
---
# db_stmt_exec

### NAME

    db_stmt_exec() - binds parameters and executes a prepared statement

### SYNOPSIS

    mixed db_stmt_exec(int stmt_handle, mixed arg, ...);

### DESCRIPTION

    Binds each trailing argument, in order, to a `?` placeholder in the
    prepared statement identified by `stmt_handle`, then executes it.
    The number of arguments must match the statement's parameter count.
    Supported values are integers, floats, strings, and buffers. An
    undefined value (for example, an unassigned `mixed` local) binds SQL
    NULL; integer 0 binds the numeric value zero.

    Returns the number of rows in a result set, or the affected-row count
    for a statement without a result set. On success, retrieve result
    columns and rows using db_fetch() with the statement's owning database
    connection handle. Returns an error string for a parameter-count or
    MySQL execution error. Invalid statement handles and unsupported
    argument types raise an error.

    Prepared-statement execution is currently supported only for MySQL.

### SEE ALSO

    db_stmt_prepare(3), db_stmt_next_result(3), db_stmt_free_result(3),
    db_stmt_close(3), db_fetch(3), db_exec(3)