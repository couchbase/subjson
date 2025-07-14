/*
 *     Copyright 2015-Present Couchbase, Inc.
 *
 *   Use of this software is governed by the Business Source License included
 *   in the file licenses/BSL-Couchbase.txt.  As of the Change Date specified
 *   in that file, in accordance with the Business Source License, use of this
 *   software will be governed by the Apache License, Version 2.0, included in
 *   the file licenses/APL2.txt.
 */

#pragma once

#include <string>

namespace Subdoc {

class UescapeConverter {
public:
    class Status {
    public:
        enum Code {
            SUCCESS,
            INCOMPLETE_SURROGATE, // End of string encountered with incomplete surrogate
            INVALID_SURROGATE, // Invalid surrogate pair
            EMBEDDED_NUL, // found embedded 0x00 pair
            INVALID_HEXCHARS,
            INVALID_CODEPOINT
        };

        operator bool() const {
            return m_code == SUCCESS;
        }

        Code code() const {
            return m_code;
        }

        Status(Code code) : m_code(code) {}

    private:
        Code m_code;
    };

    UescapeConverter(const std::string& in, std::string& out)
    : m_inbuf(in.c_str()), m_inlen(in.size()), m_out(out) {
    }

    UescapeConverter(const char *s, size_t n, std::string& out)
    : m_inbuf(s), m_inlen(n), m_out(out) {
    }

    inline Status convert();

    static Status convert(const char *s, size_t n, std::string& out) {
        UescapeConverter conv(s, n, out);
        return conv.convert();
    }

    static Status convert(const std::string& in, std::string &out) {
        UescapeConverter conv(in, out);
        return conv.convert();
    }


private:
    inline bool is_uescape(size_t pos);
    inline void append_utf8(char32_t pt);
    inline Status handle_uescape(size_t pos);

    const char *m_inbuf;
    size_t m_inlen;
    std::string& m_out;
    char16_t last_codepoint = 0;
};

UescapeConverter::Status
UescapeConverter::convert()
{
    for (size_t ii = 0; ii < m_inlen; ii++) {
        if (is_uescape(ii)) {
            Status st = handle_uescape(ii);
            if (!st) {
                return st;
            }

            // Skip over the 6-1 characters of {\,u,x,x,x,x}
            ii += 5;
        } else {
            m_out += m_inbuf[ii];
        }
    }
    if (last_codepoint) {
        return Status::INCOMPLETE_SURROGATE;
    }
    return Status::SUCCESS;
}

bool
UescapeConverter::is_uescape(size_t pos)
{
    if (m_inbuf[pos] != '\\') {
        return false;
    }
    if (pos == m_inlen - 1) {
        return false;
    }
    if (m_inbuf[pos+1] == 'u') {
        return true;
    }
    return false;
}

void
UescapeConverter::append_utf8(char32_t pt)
{
    if (pt < 0x80) {
        m_out += static_cast<char>(pt);
    } else if (pt < 0x800) {
        // 110xxxxxx 10xxxxxx
        m_out += static_cast<char>((pt >> 6) | 0xC0);
        // Write the remaining 6 bytes, and set higher order 11
        m_out += static_cast<char>((pt & 0x3F) | 0x80);
    } else if (pt < 0x10000) {
        // 1110xxxx 10xxxxxx 10xxxxxx
        m_out += static_cast<char>((pt >> 12) | 0xE0);
        m_out += static_cast<char>(((pt >> 6) & 0x3F) | 0x80);
        m_out += static_cast<char>((pt & 0x3F) | 0x80);
    } else {
        // 11110xxx 10xxxxxx 10xxxxxx 10xxxxxx
        m_out += static_cast<char>((pt >> 18) | 0xF0);
        m_out += static_cast<char>(((pt >> 12) & 0x3F) | 0x80);
        m_out += static_cast<char>(((pt >> 6) & 0x3F) | 0x80);
        m_out += static_cast<char>((pt & 0x3F) | 0x80);
    }
}

UescapeConverter::Status
UescapeConverter::handle_uescape(size_t pos)
{
    pos += 2; // Swallow '\u'
    if (m_inlen - pos < 4) {
        return Status::INVALID_HEXCHARS; // too small
    }

    char16_t res = 0;

    for (size_t ii = pos; ii < pos+4; ii++) {
        char numbuf[2] = { m_inbuf[ii], 0 };
        char* endptr = nullptr;

        long rv = strtol(numbuf, &endptr, 16);
        if (endptr && *endptr != '\0') {
            return Status::INVALID_HEXCHARS;
        }
        if (rv == 0 && res == 0) {
            continue;
        }
        res <<= 4;
        res |= rv;
    }

    // From RFC 2781:
    // 2.2 Decoding UTF-16
    //    1) If W1 < 0xD800 or W1 > 0xDFFF, the character value U is the value
    //       of W1. Terminate.
    //
    //    2) Determine if W1 is between 0xD800 and 0xDBFF. If not, the sequence
    //       is in error and no valid character can be obtained using W1.
    //       Terminate.
    //
    //    3) If there is no W2 (that is, the sequence ends with W1), or if W2
    //       is not between 0xDC00 and 0xDFFF, the sequence is in error.
    //       Terminate.
    //
    //    4) Construct a 20-bit unsigned integer U', taking the 10 low-order
    //       bits of W1 as its 10 high-order bits and the 10 low-order bits of
    //       W2 as its 10 low-order bits.
    //    5) Add 0x10000 to U' to obtain the character value U. Terminate.
    if (res == 0x00) {
        return Status::EMBEDDED_NUL;
    }
    if (last_codepoint) {
        if (res < 0xDC00 || res > 0xDFFF) {
            return Status::INVALID_SURROGATE; // error
        }

        char16_t w1 = last_codepoint;
        char16_t w2 = res;

        // 10 low bits of w1 as its 10 high bits
        char32_t cp;
        cp = (w1 & 0x3FF) << 10;
        // 10 low bits of w2 as its 20 low bits
        cp |= (w2 & 0x3FF);

        // Add 0x10000
        cp += 0x10000;
        append_utf8(cp);
        last_codepoint = 0;

    } else if (res < 0xD800 || res > 0xDFFF) {
        append_utf8(res);
    } else if (res > 0xD7FF && res < 0xDC00) {
        last_codepoint = res;
    } else {
        return Status::INVALID_CODEPOINT;
    }

    return Status::SUCCESS;
}

}
