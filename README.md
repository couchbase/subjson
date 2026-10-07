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

## Input Requirements

The document passed to the library (`Operation::set_doc()`) must be valid
JSON. As described above the document is only parsed as far as needed to
locate the path, so the library doesn't validate the entire document, and
operations on an invalid (for instance truncated) document may fail with
`Error::DOC_NOTJSON`, but they may also succeed with a partial or incorrect
result. Callers must validate the document before passing it to the library
if it isn't already known to be valid JSON (`Subdoc::Validator` may be used
for that).

The values passed with an operation (`Operation::set_value()`) are validated
by the library.

