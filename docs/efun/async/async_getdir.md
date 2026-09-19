---
title: async / async_getdir
---
# async_getdir

### NAME

    async_getdir() - returns information pertaining to a filesystem directory

### SYNOPSIS

    void async_getdir( string dir, function callback, int stat );
    promise async_getdir( string dir );

### DESCRIPTION

    If  'dir' is a filename ('*' and '?' wildcards are supported), n array
    of strings is returned to the callback containing all filenames that match
    the specification. If 'dir' is a directory name (ending with a slash--ie:
    "/u/", "/adm/", etc), all filenames in that directory are returned.

    Unlike the get_dir routine, this efun does not take an integer second
    argument to specify more information (filename, filesize, last touched).

    The callback should follow this format:

        function(mixed res) {
            // 0 when directory doesn't exist
            // empty array when no matching files exist
            // array of matching filenames
        }

    With the callback OMITTED, returns a promise fulfilled with the sorted
    array of names -- `string *names = await async_getdir(dir);`.

    With the third, optional parameter, set to non-zero, async_getdir will
    return the stat() info for each directory entry like get_dir():

          (\{ filename, size_of_file, last_time_file_touched \})

### NOTE

    When the 'this_player in call_out' driver setting is enabled,
    this_player() inside the callback is preserved from the time the
    request was made, like call_out().

### SEE ALSO

    file_size(3), get_dir(3), stat(3), time(3)

