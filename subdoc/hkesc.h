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

// Template class for dealing with escaped hash keys (specifically, those
// which contain 'u-escapes'.
// This is required for comparison: All JSON characters have a single form
// of representation, except for those which must be escaped.
// Implemented as a template because we might 'customize' the ABI for jsonsl
// depending on the parsing mode.
#include "uescape.h"
#include "loc.h"

namespace Subdoc {
class HashKey {
public:
    template <typename StateType>
    void set_hk_begin(const StateType *, const char *at) {
        m_strvalid = false;
        m_hkbuf = at + 1;
    }

    template <typename StateType>
    void set_hk_end(const StateType *state) {
        m_hklen = state->pos_cur - (state->pos_begin + 1);
        if (!state->nescapes) {
            m_hkesc = false;
        } else {
            m_hkesc = true;
        }
    }

    const char *get_hk(size_t &nkey) {
        if (!m_hkesc) {
            nkey = m_hklen;
            return m_hkbuf;
        }

        if (m_strvalid) {
            nkey = m_hkstr.size();
            return m_hkstr.c_str();
        }

        m_hkstr.clear();

        // TODO: use jsonsl_util_unescape_ex() instead. However this requires
        // a dedicated character table
        UescapeConverter::convert(m_hkbuf, m_hklen, m_hkstr);
        m_strvalid = true;
        return get_hk(nkey);
    }

    void hk_rawloc(Loc &loc) const {
        loc.at = m_hkbuf - 1;
        loc.length = m_hklen + 2;
    }

private:
    const char *m_hkbuf = NULL;
    size_t m_hklen = 0;
    bool m_hkesc = false;
    bool m_strvalid = false;
    std::string m_hkstr;
};
}
