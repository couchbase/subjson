# subjson - quickly manipulate JSON subfields

This is high performance string library which can manipulate JSON documents.
It does so by performing simple string substitutions on _regions_ of the
document.

This library uses the fast [jsonsl](https://github.com/mnunberg/jsonsl) parser
to obtain regions of the document which should be replaced, and outputs a small,
fixed array of `iovec` like structures (buffer-length regions) which consist
of the new document.

## Performance Characteristics

Because the library does not actually build a JSON tree, the memory usage and
CPU consumption is constant, regardless of the size of the actual JSON object
being operated upon, and thus the only variable performance factor is the
amount of actual time the library can seek to the location in the document to
be modified.

On a single Xeon E5520 core, this library can process about 150MB/s-300MB/s
of JSON. This processing includes the search logic as well as any replacement
logic.

The above speed is rather misleading, as this is often quicker, since the
document is only parsed until the relevant match sections have been found.
This means that even for large inputs, only _n_ bytes of the data is actually
parsed, where _n_ is the position in the file where the match itself ends.

Performance may also depend on how deep and/or long the path is (since string
comparison must be done occasionally on the relevant path components).

## Building

    $ mkdir build
    $ cd build
    $ cmake .. -DCMAKE_BUILD_TYPE=RELEASE -DSUBJSON_GTEST=/path/to/gtest
    $ make
    $ make test
    $ ./bin/bench --help

Note that to run the tests you will need to have a copy of gtest. A minified
version may be found [here](https://github.com/couchbasedeps/gtest).

## Testing commands

The build will produce a `bench` program in the `$build/bin` directory,
where `$build` is the directory from which CMake was run.

The basic syntax of `bench` is:

    ./bin/bench -c <COMMAND> -f <JSON FILE> -p <PATH> [ -v <VALUE> ]

You can use `./bin/bench -c help` to show a list of commands.

For commands which perform mutations, the `-v` argument is required, and
must contain a string which will evaluate as valid JSON within the context
of the operation. In most cases this is just a simple JSON value; in the case
of list operations this may also be a series of JSON values separated by
commas.

Note that if inserting a string, the string must be specified with surrounding
quotes. For example


    ./bin/bench -f ../jsondata/brewery_5k.json -v '"CENSORED DUE TO PROHIBITION"' -p description -c replace
