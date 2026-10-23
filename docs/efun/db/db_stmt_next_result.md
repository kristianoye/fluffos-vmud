---
title: db / db_stmt_next_result
---
# db_stmt_next_result

### NAME

    db_stmt_next_result() - advances to the next prepared-statement result

### SYNOPSIS

    mixed db_stmt_next_result(int stmt_handle);

### DESCRIPTION

    Advances the prepared statement identified by `stmt_handle` to its
    next result set, such as a result returned by a stored procedure call.
    If another result set is available, it becomes the current result for
    the statement's owning connection and can be read with db_fetch().

    Returns the number of rows in the next result set, 0 when there are no
    more results, or an error string if MySQL reports an error. An invalid
    statement handle raises an error. Prepared statements are currently
    supported only for MySQL connections.

### SEE ALSO

    db_stmt_exec(3), db_fetch(3), db_stmt_free_result(3), db_stmt_close(3)