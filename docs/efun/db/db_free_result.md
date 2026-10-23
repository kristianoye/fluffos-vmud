---
title: db / db_free_result
---
# db_free_result

### NAME

    db_free_result() - releases the current result set

### SYNOPSIS

    int db_free_result(int handle);

### DESCRIPTION

    Releases the current result set for the database connection identified
    by `handle`. Use this when you no longer need to fetch rows, or before
    issuing another statement when the current result has not been fully
    consumed. This can help avoid MySQL's "Commands out of sync" error.

    Returns 1 when the database backend supports result cleanup, or 0
    otherwise. An invalid connection handle raises an error.

### SEE ALSO

    db_close(3), db_exec(3), db_fetch(3), db_stmt_free_result(3)