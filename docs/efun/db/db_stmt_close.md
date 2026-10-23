---
title: db / db_stmt_close
---
# db_stmt_close

### NAME

    db_stmt_close() - closes a prepared statement

### SYNOPSIS

    int db_stmt_close(int stmt_handle);

### DESCRIPTION

    Closes the prepared statement identified by `stmt_handle` and releases
    its statement handle. Closing a database connection also closes its
    prepared statements. The statement's current cached result is not
    cleared by this call; use db_stmt_free_result() or db_free_result() to
    release that result explicitly.

    Returns 1 on success, 0 if MySQL reports a close failure. An invalid
    statement handle raises an error. Prepared statements are currently
    supported only for MySQL connections.

### SEE ALSO

    db_stmt_prepare(3), db_stmt_exec(3), db_stmt_free_result(3),
    db_close(3)