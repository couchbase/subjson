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
 * Micro benchmarks for parsing complete documents with jsonsl, for the
 * different kinds of content found in JSON documents.
 */

#define INCLUDE_JSONSL_SRC
#include "subdoc/jsonsl_header.h"
#include <benchmark/benchmark.h>
#include <string>
#include <utility>
#include <vector>

namespace {

/** The kind of content in the generated document */
enum class Content {
    /** Compact records with a mix of value types */
    Records,
    /** The same records, pretty printed */
    PrettyRecords,
    /** Strings of varying length with an occasional escape sequence */
    Strings,
    /** Integers and floating point numbers */
    Numbers
};

std::string makeRecord(size_t idx, bool pretty) {
    const std::string id = std::to_string(idx);
    const std::vector<std::pair<std::string, std::string>> members = {
            {"id", id},
            {"name", "\"user-" + id + "\""},
            {"email", "\"user." + id + "@example.com\""},
            {"score", std::to_string(idx % 1000) + ".25"},
            {"active", idx % 2 ? "true" : "false"},
            {"manager", "null"},
            {"tags", pretty ? R"(["a", "bb", "ccc"])" : R"(["a","bb","ccc"])"}};
    std::string record = "{";
    for (const auto& [key, value] : members) {
        if (record.size() > 1) {
            record.push_back(',');
        }
        record.append(pretty ? "\n      \"" : "\"");
        record.append(key);
        record.append(pretty ? "\": " : "\":");
        record.append(value);
    }
    record.append(pretty ? "\n    }" : "}");
    return record;
}

std::string makeElement(Content content, size_t idx) {
    switch (content) {
    case Content::Records:
        return makeRecord(idx, false);
    case Content::PrettyRecords:
        return makeRecord(idx, true);
    case Content::Strings: {
        std::string str = "\"";
        const size_t words = 2 + (idx % 30);
        for (size_t ii = 0; ii < words; ++ii) {
            str.append(ii % 7 == 6 ? R"(\"quoted\" )" : "lorem ipsum ");
        }
        str.push_back('"');
        return str;
    }
    case Content::Numbers:
        switch (idx % 4) {
        case 0:
            return std::to_string(idx * 7919);
        case 1:
            return "-" + std::to_string(idx);
        case 2:
            return std::to_string(idx) + ".125";
        default:
            return "1.5e-" + std::to_string(idx % 300);
        }
    }
    return {};
}

/**
 * Build a document with an array of (at least) the requested size in
 * bytes, containing the requested kind of elements.
 */
std::string makeDocument(size_t bytes, Content content) {
    const bool pretty = content == Content::PrettyRecords;
    std::string doc = pretty ? "{\n  \"items\": [\n    " : R"({"items":[)";
    size_t idx = 0;
    while (doc.size() < bytes) {
        if (idx) {
            doc.append(pretty ? ",\n    " : ",");
        }
        doc.append(makeElement(content, idx++));
    }
    doc.append(pretty ? "\n  ]\n}" : "]}");
    return doc;
}

struct ParseCounters {
    size_t pushes = 0;
    jsonsl_error_t error = JSONSL_ERROR_SUCCESS;
};

void pushCallback(jsonsl_t jsn,
                  jsonsl_action_t,
                  jsonsl_state_st*,
                  const jsonsl_char_t*) {
    ++static_cast<ParseCounters*>(jsn->data)->pushes;
}

int errorCallback(jsonsl_t jsn,
                  jsonsl_error_t error,
                  jsonsl_state_st*,
                  jsonsl_char_t*) {
    static_cast<ParseCounters*>(jsn->data)->error = error;
    return 0;
}

/**
 * Parse the document with jsonsl for each iteration of the benchmark.
 *
 * @param callbackLevels Invoke the callbacks for values up to this level
 *                       (2: the document and its members only, as when
 *                       validating)
 */
void runParse(benchmark::State& state, Content content, size_t callbackLevels) {
    const auto doc = makeDocument(state.range(0), content);
    jsonsl_t jsn = jsonsl_new(64);
    ParseCounters counters;
    while (state.KeepRunning()) {
        counters = {};
        jsonsl_reset(jsn);
        jsonsl_enable_all_callbacks(jsn);
        jsn->action_callback_PUSH = pushCallback;
        jsn->error_callback = errorCallback;
        jsn->max_callback_level = callbackLevels;
        jsn->data = &counters;
        jsonsl_feed(jsn, doc.data(), doc.size());
        if (counters.error != JSONSL_ERROR_SUCCESS) {
            state.SkipWithError(std::string("jsonsl failed: ") +
                                jsonsl_strerror(counters.error));
            break;
        }
        benchmark::DoNotOptimize(counters.pushes);
    }
    jsonsl_destroy(jsn);
    state.SetBytesProcessed(static_cast<int64_t>(state.iterations()) *
                            static_cast<int64_t>(doc.size()));
}

void JsonslParseRecords(benchmark::State& state) {
    runParse(state, Content::Records, 2);
}

void JsonslParsePrettyRecords(benchmark::State& state) {
    runParse(state, Content::PrettyRecords, 2);
}

void JsonslParseStrings(benchmark::State& state) {
    runParse(state, Content::Strings, 2);
}

void JsonslParseNumbers(benchmark::State& state) {
    runParse(state, Content::Numbers, 2);
}

/** A callback for every value in the document */
void JsonslParseRecordsAllCallbacks(benchmark::State& state) {
    runParse(state, Content::Records, 64);
}

} // namespace

#define JSONSL_BENCHMARK(name) \
    BENCHMARK(name)->RangeMultiplier(16)->Range(4 << 10, 1 << 20)

JSONSL_BENCHMARK(JsonslParseRecords);
JSONSL_BENCHMARK(JsonslParsePrettyRecords);
JSONSL_BENCHMARK(JsonslParseStrings);
JSONSL_BENCHMARK(JsonslParseNumbers);
JSONSL_BENCHMARK(JsonslParseRecordsAllCallbacks);
