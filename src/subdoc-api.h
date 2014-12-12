#ifndef LCB_SUBDOCAPI_H
#define LCB_SUBDOCAPI_H

#ifdef __cplusplus
extern "C" {
#endif

/**
 * The following error codes are returned when a certain sub-document
 * manipulation function could not be executed. Traditional memcached
 * errors may also be returned.
 */

typedef enum {
    SUBDOC_SUCCESS = 0x00, /* PROTOCOL_BINARY_RESPONSE_SUCCESS*/
    /** Document exists, but the given path was not found in the document */
    SUBDOC_PATH_ENOENT = 0x501,
    /** There was a conflict between the data and the path */
    SUBDOC_PATH_MISMATCH = 0x502,
    /** The path is not a valid path (i.e. does not parse correctly) */
    SUBDOC_PATH_EINVAL = 0x503,
    /** The document reference exists, but is not JSON */
    SUBDOC_DOC_NOTJSON = 0x504,
    /**The requested operation required the value did not already exist, but it exists */
    SUBDOC_DOC_EEXISTS = 0x505,
    /**The path requested is too long/deep to traverse */
    SUBDOC_PATH_E2BIG = 0x506,
    /**The number to increment was too big (could not fit in an int64_t) */
    SUBDOC_NUM_E2BIG = 0x507,
    /**Delta is too big (bigger than INT64_MAX) */
    SUBDOC_DELTA_E2BIG = 0x508,
    /**Invalid value for insertion. Inserting this value would invalidate
     * the JSON document */
    SUBDOC_VALUE_CANTINSERT = 0x509,

    /* MEMCACHED ERROR CODES */
    SUBDOC_GLOBAL_ENOMEM = 0x82,
    SUBDOC_GLOBAL_ENOSUPPORT = 0x83,
    SUBDOC_GLOBAL_EINVAL = 0x04,
} subdoc_ERRORS;

#if 0

/**
 * Command structure for sub-document manipulation. This is a dummy structure
 * as this does not yet exist on the server
 *
 * [ HEADER ]
 * [ KEY ]
 * [ PATH ]
 * [ ?VALUE ]
 */
typedef union {
    struct {
        protocol_binary_request_header header;
        struct {
            /* The length of the path to operate on */
            uint16_t pathlen;
            /* The specific subcommand */
            uint8_t subcmd;
        } body;
    } message;
    uint8_t bytes[24 + 2 + 1]; // i.e. 27 bytes base
} protocol_binary_request_subdoc;

/**
 * TODO: We might want to allow multiple commands in a single frame. This will
 * not make directly make execution any quicker on the server side (each command
 * is executed individually) but may help save on CAS validation, locking,
 * and network times. For example:
 *
 * 1. Insert a field
 * 2. Update a certain "Last-Update"
 */
#endif

/**@name Paths
 * A Sub-Document _PATH_ is a path to the container of the item you want to
 * access. Every JSON primitive is stored either as an array element or a
 * dictionary value. In the case of an array element, the _path_ is the path
 * to the numeric index of the array; in the case of a dictionary value, the
 * _path_ is the path to the string key for the value to be accessed (or
 * modified).
 *
 * Path components are separated by a period (`.`). To escape literal `.`
 * characters, encapsulate the given component in backticks.
 *
 * Any other tokens in the path must match _exactly_ the way they might be
 * represented in the document and must be valid JSON. For example, if an
 * element in the path contains a literal quote, the quote must be escaped
 * like so:
 *
 * @code
 * foo.qu\"oted.path
 * @endcode
 */

/**
 * @name Commands
 *
 * Each of these commands operates on a subdoc_PATH. The actual semantics
 * of the path depends on the operation. However, in general:
 *
 * _Dictionary Paths_ are expected to point to a specific dictionary _key_. For
 * update operations, the existing value is replaced. For removal operations
 * the key and value are removed. For
 *
 *
 * @{
 */
typedef enum {
    /* These operations are common because they operate on the _value_ only: */
    /** Get the value located in the path */
    SUBDOC_CMD_GET = 0,
    /** Simply check the path exists */
    SUBDOC_CMD_EXISTS,
    /** Replace the value, if the path exists */
    SUBDOC_CMD_REPLACE,
    /** Remove the value, if the path exists */
    SUBDOC_CMD_DELETE,

    /* Dictionary operations. Only valid if PATH points to a dictionary.
     * The _P variants are similar to `mkdir -p` and will create intermediate
     * path entries if they do not exist. For example, consider an empty
     * document, an `ADD` command for `foo.bar.baz` will fail (since `foo.bar`
     * does not exist), however an `ADD_P` command will succeed, creating
     * `foo` and `bar` as dictionaries, resulting in a document that looks
     * like this:
     * {"foo":{"bar":{"baz":VALUE}}}
     */

    /** Add or replace a value for the given path */
    SUBDOC_CMD_DICT_UPSERT,
    /** Add a value for the given path. Fail if the value already exists */
    SUBDOC_CMD_DICT_ADD,
    SUBDOC_CMD_DICT_UPSERT_P,
    SUBDOC_CMD_DICT_ADD_P,

    /* Array operations. Only valid if PATH points to an array */

    /* Note, there is no insert/upsert for an array, since this would require
     * padding surrounding elements with something else, which is probably not
     * what a user wants */

    /* The _P variants will create intermediate path elements, if they do
     * not exist */
    SUBDOC_CMD_ARRAY_PREPEND,
    SUBDOC_CMD_ARRAY_APPEND,
    SUBDOC_CMD_ARRAY_PREPEND_P,
    SUBDOC_CMD_ARRAY_APPEND_P,
    SUBDOC_CMD_ARRAY_POP_FIRST,
    SUBDOC_CMD_ARRAY_POP_LAST,

    /**Adds a value to a list, ensuring that the value does not already exist.
     * Values added can only be primitives, and the list itself must already
     * only contain primitives. If any of these is violated, the error
     * SUBDOC_PATH_MISMATCH is returned. */
    SUBDOC_CMD_ARRAY_ADD_UNIQUE,
    SUBDOC_CMD_ARRAY_ADD_UNIQUE_P,

    /* Use explicit path. In the protocol this should contain a 64-bit integer
     * as the delta. The _P variants will create intermediate paths, if they
     * do not exist.
     *
     * If the number itself does not fit into a uint64_t (if unsigned) or an
     * int64_t (if signed), a SUBDOC_NUM_E2BIG error is returned.
     *
     * If the resulting item does exist, but is not a signed or unsigned integer,
     * then a SUBDOC_PATH_MISMATCH error is returned. This is the case for
     * 'floats' and 'exponents' as well. Only whole integers are supported.
     */
    SUBDOC_CMD_INCREMENT,
    SUBDOC_CMD_DECREMENT,
    SUBDOC_CMD_INCREMENT_P,
    SUBDOC_CMD_DECREMENT_P
} subdoc_OPTYPE;

#define SUBDOC_CMD_ARRAY_POP SUBDOC_CMD_ARRAY_POP_LAST
#define SUBDOC_CMD_ARRAY_SHIFT SUBDOC_CMD_ARRAY_POP_FIRST

/**@}*/

struct subdoc_PATH_st;

#ifdef __cplusplus
}
#endif
#endif
