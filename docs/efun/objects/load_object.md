---
title: objects / load_object
---
# load_object

### NAME

    load_object() - find or load an object by file name

### SYNOPSIS

    object load_object( string str, mixed args... );

### DESCRIPTION

    Find  the  object with the file name 'str'.  If the file exists and the
    object hasn't been loaded yet, it is loaded and returned (if possible).

    Any trailing 'args' are passed on to create() -- the same convention
    new()/clone_object() use -- but only when this call is the one that
    actually loads the object. If the object was already loaded, create()
    is not called again and any 'args' passed are silently ignored. Virtual
    objects (loaded via master::compile_object()) initialize via
    virtual_start() rather than create(), so 'args' are also ignored for
    them.

### RETURN VALUES

    load_object() returns the loaded object if it can be loaded (or already
    loaded), otherwise the value 0 will be returned.

### SEE ALSO

    file_name(3), find_object(3), clone_object(3), new(3), stat(3)


