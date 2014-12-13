# subjson - quickly manipulate JSON subfields in pure C

This is high performance string library which can manipulate JSON documents.
It does so by performing simple string substitutions on _regions_ of the
document.

This library uses the fast [jsonsl](https://github.com/mnunberg/jsonsl) parser
to obtain regions of the document which should be replaced, and outputs a small,
fixed array of `iovec` like structures (buffer-length regions) which consist
of the new document.

Because the library does not actually build a JSON tree, the memory usage and
CPU consumption is constant, regardless of the size of the actual JSON object
being operated upon, and thus the only variable performance factor is the
amount of actual time the library can seek to the location in the document to
be modified.

## Building

    $ git submodule init
    $ git submodule update
    $ mkdir build
    $ cd build
    $ cmake .. -DCMAKE_BUILD_TYPE=RELEASE
    $ make
    $ make test


