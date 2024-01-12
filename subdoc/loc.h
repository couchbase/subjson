/* -*- Mode: C++; tab-width: 4; c-basic-offset: 4; indent-tabs-mode: nil -*- */
/*
*     Copyright 2015-Present Couchbase, Inc.
*
*   Use of this software is governed by the Business Source License included
*   in the file licenses/BSL-Couchbase.txt.  As of the Change Date specified
*   in that file, in accordance with the Business Source License, use of this
*   software will be governed by the Apache License, Version 2.0, included in
*   the file licenses/APL2.txt.
*/

#if !defined(SUBDOC_LOC_H) && defined(__cplusplus)
#define SUBDOC_LOC_H

#include <cstddef>
#include <string>

namespace Subdoc {
/** Structure describing a position and length of a buffer (e.g. IOV) */
class Loc {
public:
    const char *at = nullptr;
    size_t length = 0;

    Loc() = default;

    Loc(const char *s, size_t n) {
        assign(s, n);
    }

    enum OverlapMode {
        NO_OVERLAP = 0,
        OVERLAP = 1
    };

    void assign(const char *s, size_t n) { at = s; length = n; }

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

    bool empty() const {
        return length == 0;
    }

    void clear() {
        at = nullptr;
        length = 0;
    }

    [[nodiscard]] std::string_view to_view() const {
        return {at, length};
    }

    [[nodiscard]] std::string to_string() const {
        if (!empty()) {
            return std::string{to_view()};
        } else {
            return {};
        }
    }

    // Move buffer start ahead n bytes
    void ltrim(size_t n)
    {
        at += n;
        length -= n;
    }

    // Move buffer end back n bytes
    void rtrim(size_t n)
    {
        length -= n;
    }
};

// similar to boost::buffer
template <typename T> class Buffer {
public:
    Buffer(const T* buf, size_t n) : m_buf(buf), m_size(n) {
    }

    const T& operator[](size_t ix) const { return m_buf[ix]; }
    size_t size() const { return m_size; }
    typedef const T* const_iterator;
    const_iterator begin() const { return m_buf; }
    const_iterator end() const { return m_buf + m_size; }
protected:
    const T *m_buf;
    size_t m_size;
};

} // namespace Subdoc
#endif
