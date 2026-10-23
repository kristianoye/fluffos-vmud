---
title: db / db_stmt_free_result
---
# db_stmt_free_result

### NAME

    db_stmt_free_result() - releases a prepared statement's result

### SYNOPSIS

    int db_stmt_free_result(int stmt_handle);

### DESCRIPTION

    Releases the current result for the prepared statement identified by
    `stmt_handle` without closing the statement. The statement can still be
    executed again with db_stmt_exec(). Since results are cached on the
    owning connection, this also clears that connection's current result
    for db_fetch().

    Returns 1 when the statement result is released. An invalid statement
    handle raises an error. Prepared statements are currently supported
    only for MySQL connections.

### SEE ALSO

    db_stmt_exec(3), db_stmt_close(3), db_free_result(3), db_fetch(3)