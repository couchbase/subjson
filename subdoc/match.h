#ifndef SUBDOC_MATCH_H
#define SUBDOC_MATCH_H

#include "loc.h"
#include "path.h"

#ifdef __cplusplus
extern "C" {
#endif

/** Structure describing a match for an item */
typedef struct {
    /**The JSON type for the result (i.e. jsonsl_type_t). If the match itself
     * is not found, this will contain the innermost _parent_ type. */
    unsigned type : 32;

    /**Error status (jsonsl_error_t) */
    unsigned status : 16;

    /** result of the match. jsonsl_jpr_match_t */
    int16_t matchres;

    /** Flags, if #type is JSONSL_TYPE_SPECIAL */
    uint16_t sflags;

    /**
     * The deepest level at which a possible match was found.
     * In jsonsl, the imaginary root level is 0, the top level container
     * is 1, etc.
     */
    uint16_t match_level;

    /* Value of 'nelem' */
    uint64_t numval;

    /**
     * The current position of the match. This value is 0-based and works
     * in conjunction with #num_siblings to determine how to handle
     * surrounding items for various modification items.
     */
    unsigned position;

    /**The number of children in the last parent. Note this may not necessarily
     * be the immediate parent; but rather indicates whether any kind of special
     * comma handling is required.
     *
     * This is used by insertion/deletion operations to determine if any
     * trailing tokens from surrounding items should be stripped.
     */
    unsigned num_siblings;

    /** Match is a dictionary value. #loc_key contains the key */
    unsigned char has_key;

    /**
     * Response flag indicating that the match's immediate parent was found,
     * and its location is in #loc_parent.
     *
     * This flag is implied to be true if #matchres is JSONSL_MATCH_COMPLETE
     */
    unsigned char immediate_parent_found;

    /**Request flag; indicating whether the last child position should be
     * returned inside the `loc_key` member. Note that the position will
     * be indicated in the `loc_key`'s _length_ field, and its `at` field
     * will be set to NULL
     *
     * This requires that the last child be an array element, and thus the
     * parent match object be an array.
     *
     * This also changes some of the match semantics. Here most of the
     * information will be adapted to suit the last child; this includes
     * things like the value type and such.
     */
    unsigned char get_last_child_pos;

    /**If 'ensure_unique' is true, set to true if the value of #ensure_unique
     * already exists */
    unsigned char unique_item_found;

    /** Location describing the matched item, if the match is found */
    subdoc_LOC loc_match;

    /**Location desribing the key for the item. Valid only if #has_key is true*/
    subdoc_LOC loc_key;

    /**Location describing the deepest parent match. If #immediate_parent_found
     * is true then this is the direct parent of the match.*/
    subdoc_LOC loc_parent;

    /**If set to true, will also descend each child element to ensure that
     * the contents here are unique. Will set an error code accordingly, if
     * types are mismatched. */
    subdoc_LOC ensure_unique;
} subdoc_MATCH;


int
subdoc_match_exec(const char *value, size_t nvalue,
    const subdoc_PATH *nj, jsonsl_t jsn, subdoc_MATCH *result);

jsonsl_t
subdoc_jsn_alloc(void);

void
subdoc_jsn_free(jsonsl_t);

/* Treats the value as a top-level object */
#define SUBDOC_VALIDATE_PARENT_NONE 0x01

/* Treats the value as one or more array elements */
#define SUBDOC_VALIDATE_PARENT_ARRAY 0x02

/* Treats the value as a dictionary value */
#define SUBDOC_VALIDATE_PARENT_DICT 0x03

/* New value must be a single element only */
#define SUBDOC_VALIDATE_F_SINGLE 0x100

/* New value must be a JSON primitive */
#define SUBDOC_VALIDATE_F_PRIMITIVE 0x200

#define SUBDOC_VALIDATE_MODEMASK 0xFF
#define SUBDOC_VALIDATE_FLAGMASK 0xFF00

/* Error codes */
typedef enum {
    /* Requested a primitive, but item is not a primitive */
    SUBDOC_VALIDATE_ENOTPRIMITIVE = JSONSL_ERROR_GENERIC + 1,
    /* Requested only a single item, but multiple found. Also returned if
     * PARENT_NONE is specified, and multiple items are found! */
    SUBDOC_VALIDATE_EMULTIELEM,
    /* No parse error, but a full JSON value could not be parsed */
    SUBDOC_VALIDATE_EPARTIAL
} subdoc_VALIDSTATUS;

/**
 * Convenience function to scan an item and see if it's json.
 *
 * @param s Buffer to check
 * @param n Size of buffer
 * @param jsn Parser. If NULL, one will be allocated and freed internally
 * @param mode The context in which the value should be checked. This is one of
 * the `SUBDOC_VALIDATE_PARENT_*` constants. The mode may also be combined
 * with one of the flags to add additional constraints on the added value.
 *
 * @return JSONSL_ERROR_SUCCESS if JSON, error code otherwise.
 */
jsonsl_error_t
subdoc_validate(const char *s, size_t n, jsonsl_t jsn, int mode);

#ifdef __cplusplus
}
#endif

#endif /* SUBDOC_MATCH_H */
