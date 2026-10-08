/*
 *     Copyright 2026-Present Couchbase, Inc.
 *
 *   Use of this software is governed by the Business Source License included
 *   in the file licenses/BSL-Couchbase.txt.  As of the Change Date specified
 *   in that file, in accordance with the Business Source License, use of this
 *   software will be governed by the Apache License, Version 2.0, included in
 *   the file licenses/APL2.txt.
 */

/**
 * Property based tests which run (a lot of) operations on randomly generated
 * documents, and verify properties which must hold for every one of them.
 * They complement the hand written tests (which verify the exact result of
 * a given operation) by covering combinations of commands, paths and
 * documents nobody thought of writing a test for.
 */

#include "subdoc/match.h"
#include "subdoc/operations.h"
#include "subdoc/validate.h"
#include <gtest/gtest.h>
#include <random>
#include <string>
#include <string_view>
#include <vector>

using Subdoc::Command;
using Subdoc::Error;
using Subdoc::Match;
using Subdoc::Operation;
using Subdoc::Result;
using Subdoc::Validator;

namespace {

/** Generate a random JSON value (biased towards tricky values) */
void randomValue(std::mt19937& gen, std::string& out, int depth) {
    static const std::vector<std::string_view> primitives = {
            "0",          "-0",      "1",         "-1",
            "12345",      "-987",    "1.5",       "1e5",
            "2E-3",       "true",    "false",     "null",
            R"("")",      R"("s")",  R"("a\"b")", R"("\\")",
            R"("]},[{")", R"("23")", "1234",      "9223372036854775807"};
    const char* ws = gen() % 5 == 0 ? " " : "";
    switch (gen() % (depth > 2 ? 3 : 6)) {
    case 0:
    case 1:
    case 2:
        out.append(primitives[gen() % primitives.size()]);
        return;
    case 3:
    case 4: {
        out.push_back('[');
        out.append(ws);
        const int count = int(gen() % 5);
        for (int ii = 0; ii < count; ++ii) {
            if (ii) {
                out.append(ws).append(",").append(ws);
            }
            randomValue(gen, out, depth + 1);
        }
        out.append(ws);
        out.push_back(']');
        return;
    }
    default: {
        out.push_back('{');
        const int count = int(gen() % 4);
        for (int ii = 0; ii < count; ++ii) {
            if (ii) {
                out.push_back(',');
            }
            out.append("\"k" + std::to_string(ii) + "\":");
            randomValue(gen, out, depth + 1);
        }
        out.push_back('}');
        return;
    }
    }
}

/**
 * Generate a random document with an array "a" and an object "o" (which
 * the paths used by the tests refer to) surrounded by other members
 */
std::string randomDocument(std::mt19937& gen) {
    std::string doc = R"({"pre":)";
    randomValue(gen, doc, 1);
    doc.append(R"(,"a":[)");
    const int count = int(gen() % 6);
    for (int ii = 0; ii < count; ++ii) {
        if (ii) {
            doc.append(gen() % 4 ? "," : " , ");
        }
        randomValue(gen, doc, 1);
    }
    doc.append(R"(],"o":{)");
    const int members = int(gen() % 4);
    for (int ii = 0; ii < members; ++ii) {
        if (ii) {
            doc.push_back(',');
        }
        doc.append("\"k" + std::to_string(ii) + "\":");
        randomValue(gen, doc, 1);
    }
    doc.append(R"(},"post":1})");
    return doc;
}

/** The outcome of an operation */
struct Outcome {
    Error status;
    /** The new document (for successful mutations) */
    std::string newdoc;
    /** The match (for successful operations) */
    std::string match;
};

Outcome execute(const std::string& doc,
                uint8_t command,
                const std::string& path,
                std::string_view value) {
    Operation op;
    Result result;
    op.set_doc(doc);
    if (!value.empty()) {
        op.set_value(value.data(), value.size());
    }
    op.set_code(command);
    op.set_result_buf(&result);
    Outcome outcome{op.op_exec(path), {}, {}};
    if (outcome.status.success()) {
        for (const auto& loc : result.newdoc()) {
            outcome.newdoc.append(loc.at, loc.length);
        }
        if (result.matchloc().at) {
            outcome.match.assign(result.matchloc().at,
                                 result.matchloc().length);
        }
    }
    return outcome;
}

/** Paths into the documents created by randomDocument() */
const std::vector<std::string> paths = {
        "",         "a",        "a[0]",      "a[1]",     "a[2]",
        "a[5]",     "a[-1]",    "a[-1][-1]", "a[-1][0]", "a[0][0]",
        "a[1][-1]", "a[-1].k0", "a[-1].k1",  "a[1].k0",  "a[-1].new",
        "a[1].new", "o",        "o.k0",      "o.k1",     "o.k2",
        "o.new",    "o.k0[-1]", "o.k0[0]",   "o.k1.k0",  "o.k1[-1].k0",
        "pre",      "post",     "new",       "new.deep", "pre[-1]"};

/** A command and the value to use with it */
struct Request {
    uint8_t command;
    std::string_view value;
};

/** Mutations to run on the documents */
const std::vector<Request> mutations = {
        {Command::DICT_ADD, "1"},
        {Command::DICT_ADD_P, R"({"x":[1]})"},
        {Command::DICT_UPSERT, R"("s")"},
        {Command::DICT_UPSERT_P, "[1,2]"},
        {Command::REPLACE, "null"},
        {Command::REPLACE, R"({"x":1})"},
        {Command::REMOVE, {}},
        {Command::ARRAY_APPEND, "1"},
        {Command::ARRAY_APPEND, "1,[2],3"},
        {Command::ARRAY_APPEND_P, R"({"x":1})"},
        {Command::ARRAY_PREPEND, "1"},
        {Command::ARRAY_PREPEND_P, R"("s",2)"},
        {Command::ARRAY_INSERT, "1"},
        {Command::ARRAY_INSERT, "[1],2"},
        {Command::ARRAY_ADD_UNIQUE, "1"},
        {Command::ARRAY_ADD_UNIQUE_P, R"("s")"},
        {Command::COUNTER, "1"},
        {Command::COUNTER_P, "-5"},
};

/** The number of random documents each test runs operations on */
constexpr int NumDocuments = 2000;

} // namespace

/**
 * Every successful mutation must result in a valid JSON document; whatever
 * the document, path and value are.
 */
TEST(InvariantTest, MutationsProduceValidJson) {
    std::mt19937 gen(0x5eed);
    auto* jsn = Match::jsn_alloc();
    size_t succeeded = 0;
    for (int iteration = 0; iteration < NumDocuments; ++iteration) {
        const auto doc = randomDocument(gen);
        ASSERT_EQ(JSONSL_ERROR_SUCCESS, Validator::validate(doc, jsn)) << doc;
        for (const auto& path : paths) {
            for (const auto& request : mutations) {
                const auto outcome =
                        execute(doc, request.command, path, request.value);
                if (!outcome.status.success()) {
                    continue;
                }
                ++succeeded;
                ASSERT_EQ(JSONSL_ERROR_SUCCESS,
                          Validator::validate(outcome.newdoc, jsn))
                        << "Command: " << int(request.command)
                        << " path: " << path << " value: " << request.value
                        << "\ndoc:    " << doc
                        << "\nnewdoc: " << outcome.newdoc;
            }
        }
    }
    Match::jsn_free(jsn);
    // Make sure the test actually exercised a decent number of mutations
    EXPECT_LT(NumDocuments * 50, succeeded);
}
