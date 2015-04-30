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
    SUBDOC_STATUS_SUCCESS = 0x00, /* PROTOCOL_BINARY_RESPONSE_SUCCESS*/
    /** Document exists, but the given path was not found in the document */
    SUBDOC_STATUS_PATH_ENOENT = 0x501,
    /** There was a conflict between the data and the path */
    SUBDOC_STATUS_PATH_MISMATCH = 0x502,
    /** The path is not a valid path (i.e. does not parse correctly) */
    SUBDOC_STATUS_PATH_EINVAL = 0x503,
    /** The document reference exists, but is not JSON */
    SUBDOC_STATUS_DOC_NOTJSON = 0x504,
    /**The requested operation required the value did not already exist, but it exists */
    SUBDOC_STATUS_DOC_EEXISTS = 0x505,
    /**The path requested is too long/deep to traverse */
    SUBDOC_STATUS_PATH_E2BIG = 0x506,
    /**The number to increment was too big (could not fit in an int64_t) */
    SUBDOC_STATUS_NUM_E2BIG = 0x507,
    /**Delta is too big (bigger than INT64_MAX) */
    SUBDOC_STATUS_DELTA_E2BIG = 0x508,
    /**Invalid value for insertion. Inserting this value would invalidate
     * the JSON document */
    SUBDOC_STATUS_VALUE_CANTINSERT = 0x509,
    /** Document too deep to parse */
    SUBDOC_STATUS_DOC_ETOODEEP = 0x50A,

    /* MEMCACHED ERROR CODES */
    SUBDOC_STATUS_GLOBAL_UNKNOWN_COMMAND = 0x81,
    SUBDOC_STATUS_GLOBAL_ENOMEM = 0x82,
    SUBDOC_STATUS_GLOBAL_ENOSUPPORT = 0x83,
    SUBDOC_STATUS_GLOBAL_EINVAL = 0x04,
} subdoc_ERRORS;

#ifdef __cplusplus
namespace Subdoc { typedef subdoc_ERRORS Error; }
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
    SUBDOC_CMD_GET = 0x00,

    /** Simply check the path exists */
    SUBDOC_CMD_EXISTS = 0x01,

    /** Replace the value, if the path exists */
    SUBDOC_CMD_REPLACE = 0x02,

    /** Remove the value, if the path exists */
    SUBDOC_CMD_DELETE = 0x03,

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
    SUBDOC_CMD_DICT_UPSERT = 0x04,
    SUBDOC_CMD_DICT_UPSERT_P = 0x84,

    /** Add a value for the given path. Fail if the value already exists */
    SUBDOC_CMD_DICT_ADD = 0x05,
    SUBDOC_CMD_DICT_ADD_P = 0x85,

    /* Array operations. Only valid if PATH points to an array */

    /* Note, there is no insert/upsert for an array, since this would require
     * padding surrounding elements with something else, which is probably not
     * what a user wants */

    /* The _P variants will create intermediate path elements, if they do
     * not exist */
    SUBDOC_CMD_ARRAY_PREPEND = 0x06,
    SUBDOC_CMD_ARRAY_PREPEND_P = 0x86,

    SUBDOC_CMD_ARRAY_APPEND = 0x07,
    SUBDOC_CMD_ARRAY_APPEND_P = 0x87,

    /**Adds a value to a list, ensuring that the value does not already exist.
     * Values added can only be primitives, and the list itself must already
     * only contain primitives. If any of these is violated, the error
     * SUBDOC_PATH_MISMATCH is returned. */
    SUBDOC_CMD_ARRAY_ADD_UNIQUE = 0x08,
    SUBDOC_CMD_ARRAY_ADD_UNIQUE_P = 0x88,


    /* In the protocol this should contain a 64-bit integer
     *
     * If the number itself does not fit into a uint64_t (if unsigned) or an
     * int64_t (if signed), a SUBDOC_NUM_E2BIG error is returned.
     *
     * If the resulting item does exist, but is not a signed or unsigned integer,
     * then a SUBDOC_PATH_MISMATCH error is returned. This is the case for
     * 'floats' and 'exponents' as well. Only whole integers are supported.
     */
    SUBDOC_CMD_INCREMENT = 0x09,
    SUBDOC_CMD_INCREMENT_P = 0x89,
    SUBDOC_CMD_DECREMENT = 0x0A,
    SUBDOC_CMD_DECREMENT_P = 0x8A
} subdoc_OPTYPE;

static const int SUBDOC_CMD_FLAG_MKDIR_P = 0x80;

/**@}*/

struct subdoc_PATH_st;

#ifdef __cplusplus
}
#endif
#endif
