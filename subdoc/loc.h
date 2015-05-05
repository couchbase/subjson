#if !defined(SUBDOC_LOC_H) && defined(__cplusplus)
#define SUBDOC_LOC_H

#include <stddef.h>
#include <string>

namespace Subdoc {
/** Structure describing a position and length of a buffer (e.g. IOV) */
class Loc {
public:
    const char *at;
    size_t length;

    enum OverlapMode {
        NO_OVERLAP = 0,
        OVERLAP = 1
    };

    /**
     * Modifies the object so that it ends where `until` begins.
     *
     * The object will have a starting position of the base buffer, and will
     * span until the `until` buffer.
     *
     * Example:
     * @code
     * BASE     = ABCDEFGHIJ
     * UNTIL    =      FGH
     * THIS     = ABCDE
     * @endcode
     *
     * @param base The common buffer
     * @param until position at where this buffer should end
     * @param overlap Whether the end should overlap with the first byte of `until`
     */
    void end_at_begin(const Loc& base, const Loc& until, OverlapMode overlap)
    {
        at = base.at;
        length = until.at - base.at;
        if (overlap == OVERLAP) {
            length++;
        }
    }

    /**
     * Modifies the object so that it begins where `until` ends.
     *
     * The buffer will have an end position matching the end of the base buffer,
     * and will end where `from` ends.
     *
     * Example:
     * @code
     * BASE     = ABCDEFGHIJ
     * FROM     =   CDE
     * THIS     =      FGHIJ
     * @endcode
     *
     * @param base The common buffer
     * @param from A buffer whose end should be used as the beginning of the
     *        current buffer
     * @param overlap Whether the current buffer should overlap `until`'s last
     *        byte
     */
    void begin_at_end(const Loc& base, const Loc& from, OverlapMode overlap)
    {
        at = from.at + from.length;
        length = base.length - (at - base.at);
        if (overlap == OVERLAP) {
            at--;
            length++;
        }
    }

    /**
     * Modifies the object so that it begins where `from` begins.
     *
     * The buffer will have an end position matching the end of the base buffer
     * and will begin where `from` begins
     *
     * Example:
     * @code
     * BASE     = ABCDEFGHIJ
     * FROM     =    DEF
     * THIS     =    DEFGHIJ
     * @endcode
     *
     * @param base Common buffer
     * @param from The begin position
     */
    void begin_at_begin(const Loc& base, const Loc& from)
    {
        at = from.at;
        length = base.length - (from.at - base.at);
    }

    /**
     * Modifies the object so that it ends where `until` ends.
     *
     * The buffer will have a start position of `base` and an end position of
     * `until.
     *
     * Example
     * @code
     * BASE     = ABCDEFGHIJ
     * UNTIL    =     EFG
     * THIS     = ABCDEFG
     * @endcode
     *
     * @param base
     * @param until
     * @param overlap
     */
    void
    end_at_end(const Loc& base, const Loc& until, OverlapMode overlap)
    {
        at = base.at;
        length = (until.at + until.length) - base.at;
        if (overlap == NO_OVERLAP) {
            length--;
        }
    }

    bool empty() const { return length == 0; }

    std::string to_string() const {
        if (!empty()) {
            return std::string(at, length);
        } else {
            return std::string();
        }
    }
};

} // namespace Subdoc
#endif
