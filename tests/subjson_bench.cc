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
 * Micro benchmarks for subdoc operations, measuring how their cost scales
 * with the size of the array (or object) in the document they operate on
 * (or have to skip over).
 */

#include "subdoc/operations.h"
#include <benchmark/benchmark.h>
#include <stdexcept>
#include <string>

using Subdoc::Command;
using Subdoc::Error;
using Subdoc::Operation;
using Subdoc::Result;

namespace {

/** The kind of elements to put in the array */
enum class Elements {
    /** A mix of short strings and numbers */
    Primitives,
    /** Small objects */
    Objects,
    /** Long strings with lots of escape sequences */
    EscapedStrings,
    /** Every other element is the number 1, the rest short strings */
    ManyDuplicates
};

/** A generated document, with information about the array in it */
struct Document {
    std::string json;
    /** The number of elements in "arr" */
    size_t num_elements = 0;
    /** The first element in "arr" */
    std::string first;
    /** The last element in "arr" */
    std::string last;
};

std::string makeElement(Elements elements, size_t idx) {
    switch (elements) {
    case Elements::Primitives:
        if (idx % 2) {
            return std::to_string(idx * 7919);
        }
        return R"("element-number-)" + std::to_string(idx) + "\"";
    case Elements::Objects:
        return R"({"id":)" + std::to_string(idx) + R"(,"name":"user \")" +
               std::to_string(idx) + R"(\"","tags":["a","b"],"active":true})";
    case Elements::EscapedStrings:
        return R"("C:\\Users\\bench\\file-)" + std::to_string(idx) +
               R"(.txt said \"hello, [world]\"\n\ttab\\ \u00e9 {\"x\":1}")";
    case Elements::ManyDuplicates:
        if (idx % 2) {
            return "1";
        }
        return R"("element-number-)" + std::to_string(idx) + "\"";
    }
    throw std::logic_error("makeElement: invalid elements");
}

/**
 * Build a document with a small header, an array "arr" of (at least)
 * the requested size in bytes and trailing members.
 *
 * @param bytes The approximate size of the array
 * @param elements The kind of elements to put in the array
 */
Document makeDocument(size_t bytes, Elements elements) {
    Document doc;
    doc.json = R"({"type":"bench","meta":{"owner":"me","v":1},"arr":[)";
    const auto start = doc.json.size();
    while (doc.json.size() - start < bytes) {
        if (doc.num_elements) {
            doc.json.push_back(',');
        }
        doc.last = makeElement(elements, doc.num_elements);
        if (doc.num_elements == 0) {
            doc.first = doc.last;
        }
        doc.json.append(doc.last);
        ++doc.num_elements;
    }
    doc.json.append(R"(],"tail":true,"count":0})");
    return doc;
}

/**
 * Build a document with a small header, an object "obj" of (at least) the
 * requested size in bytes and a trailing member.
 */
std::string makeObjectDocument(size_t bytes) {
    std::string doc = R"({"type":"bench","meta":{"owner":"me","v":1},"obj":{)";
    const auto start = doc.size();
    size_t idx = 0;
    while (doc.size() - start < bytes) {
        if (idx) {
            doc.push_back(',');
        }
        doc.append(R"("key-number-)" + std::to_string(idx) + R"(":)");
        if (idx % 2) {
            doc.append(std::to_string(idx * 7919));
        } else {
            doc.append(R"("value-number-)" + std::to_string(idx) + "\"");
        }
        ++idx;
    }
    doc.append(R"(},"tail":true})");
    return doc;
}

/**
 * Run the operation on the document for each iteration of the benchmark
 * and verify that it returns the expected status.
 */
void runOp(benchmark::State& state,
           const std::string& doc,
           Command command,
           const std::string& path,
           std::string_view value,
           Error expected = Error::SUCCESS) {
    Operation op;
    Result result;
    while (state.KeepRunning()) {
        op.clear();
        result.clear();
        op.set_doc(doc);
        if (!value.empty()) {
            op.set_value(value.data(), value.size());
        }
        op.set_code(command);
        op.set_result_buf(&result);
        const auto rv = op.op_exec(path);
        if (rv != expected) {
            state.SkipWithError("op_exec on " + path +
                                " returned: " + rv.description() +
                                ", expected: " + expected.description());
            return;
        }
        const char* match = result.matchloc().at;
        benchmark::DoNotOptimize(match);
        if (rv == Error::SUCCESS && result.newdoc().size() > 0) {
            const char* newdoc = result.newdoc()[0].at;
            benchmark::DoNotOptimize(newdoc);
        }
    }
    state.SetBytesProcessed(static_cast<int64_t>(state.iterations()) *
                            static_cast<int64_t>(doc.size()));
}

void runArrayOp(benchmark::State& state,
                Elements elements,
                Command command,
                const std::string& path,
                std::string_view value,
                Error expected = Error::SUCCESS) {
    const auto doc = makeDocument(state.range(0), elements);
    runOp(state, doc.json, command, path, value, expected);
}

// ---------------------------------------------------------------------------
// Array mutations
// ---------------------------------------------------------------------------

void ArrayAppendPrimitives(benchmark::State& state) {
    runArrayOp(state,
               Elements::Primitives,
               Command::ARRAY_APPEND,
               "arr",
               R"("new value")");
}

void ArrayAppendObjects(benchmark::State& state) {
    runArrayOp(state,
               Elements::Objects,
               Command::ARRAY_APPEND,
               "arr",
               R"({"id":-1})");
}

/** Strings with many escapes are the slow path when skipping strings */
void ArrayAppendEscapedStrings(benchmark::State& state) {
    runArrayOp(state,
               Elements::EscapedStrings,
               Command::ARRAY_APPEND,
               "arr",
               R"("new value")");
}

/** The worst case for unique; every element must be inspected */
void ArrayAddUniqueNotPresent(benchmark::State& state) {
    runArrayOp(state,
               Elements::Primitives,
               Command::ARRAY_ADD_UNIQUE,
               "arr",
               R"("not-there")");
}

/** The best case for unique; the first element is a duplicate */
void ArrayAddUniquePresentFirst(benchmark::State& state) {
    const auto doc = makeDocument(state.range(0), Elements::Primitives);
    runOp(state,
          doc.json,
          Command::ARRAY_ADD_UNIQUE,
          "arr",
          doc.first,
          Error::DOC_EEXISTS);
}

/** The last element is a duplicate */
void ArrayAddUniquePresentLast(benchmark::State& state) {
    const auto doc = makeDocument(state.range(0), Elements::Primitives);
    runOp(state,
          doc.json,
          Command::ARRAY_ADD_UNIQUE,
          "arr",
          doc.last,
          Error::DOC_EEXISTS);
}

void ArrayRemoveFirstPrimitives(benchmark::State& state) {
    // 7919 is the second element in the array
    runArrayOp(state,
               Elements::Primitives,
               Command::ARRAY_REMOVE_FIRST,
               "arr",
               "7919");
}

/** Half of the elements in the array are removed */
void ArrayRemoveAllManyMatches(benchmark::State& state) {
    runArrayOp(state,
               Elements::ManyDuplicates,
               Command::ARRAY_REMOVE_ALL,
               "arr",
               "1");
}

/** Insert before the last element in the array */
void ArrayInsertNearEnd(benchmark::State& state) {
    const auto doc = makeDocument(state.range(0), Elements::Primitives);
    runOp(state,
          doc.json,
          Command::ARRAY_INSERT,
          "arr[" + std::to_string(doc.num_elements - 1) + "]",
          "0");
}

/** Reference: only the beginning of the array has to be located */
void ArrayPrependPrimitives(benchmark::State& state) {
    runArrayOp(state,
               Elements::Primitives,
               Command::ARRAY_PREPEND,
               "arr",
               R"("new value")");
}

// ---------------------------------------------------------------------------
// Operations on the last element in the array / the size of the array
// ---------------------------------------------------------------------------

void ArrayGetCount(benchmark::State& state) {
    runArrayOp(state, Elements::Primitives, Command::GET_COUNT, "arr", {});
}

void ArrayGetLast(benchmark::State& state) {
    runArrayOp(state, Elements::Primitives, Command::GET, "arr[-1]", {});
}

void ArrayReplaceLast(benchmark::State& state) {
    runArrayOp(state, Elements::Primitives, Command::REPLACE, "arr[-1]", "0");
}

void ArrayRemoveLast(benchmark::State& state) {
    runArrayOp(state, Elements::Primitives, Command::REMOVE, "arr[-1]", {});
}

// ---------------------------------------------------------------------------
// Operations on a member located after a large array
// ---------------------------------------------------------------------------

void GetAfterArrayOfPrimitives(benchmark::State& state) {
    runArrayOp(state, Elements::Primitives, Command::GET, "tail", {});
}

void GetAfterArrayOfObjects(benchmark::State& state) {
    runArrayOp(state, Elements::Objects, Command::GET, "tail", {});
}

void CounterAfterArray(benchmark::State& state) {
    runArrayOp(state, Elements::Primitives, Command::COUNTER, "count", "1");
}

// ---------------------------------------------------------------------------
// Operations on a large object
// ---------------------------------------------------------------------------

void DictAddNewKey(benchmark::State& state) {
    const auto doc = makeObjectDocument(state.range(0));
    runOp(state, doc, Command::DICT_ADD, "obj.newkey", "1");
}

void DictUpsertNewKey(benchmark::State& state) {
    const auto doc = makeObjectDocument(state.range(0));
    runOp(state, doc, Command::DICT_UPSERT, "obj.newkey", "1");
}

/** Reference: an existing key at the beginning of the object */
void DictUpsertFirstKey(benchmark::State& state) {
    const auto doc = makeObjectDocument(state.range(0));
    runOp(state, doc, Command::DICT_UPSERT, "obj.key-number-0", "1");
}

} // namespace

#define SUBJSON_BENCHMARK(name) \
    BENCHMARK(name)->RangeMultiplier(8)->Range(1 << 10, 8 << 20)

SUBJSON_BENCHMARK(ArrayAppendPrimitives);
SUBJSON_BENCHMARK(ArrayAppendObjects);
SUBJSON_BENCHMARK(ArrayAppendEscapedStrings);
SUBJSON_BENCHMARK(ArrayAddUniqueNotPresent);
SUBJSON_BENCHMARK(ArrayAddUniquePresentFirst);
SUBJSON_BENCHMARK(ArrayAddUniquePresentLast);
SUBJSON_BENCHMARK(ArrayRemoveFirstPrimitives);
SUBJSON_BENCHMARK(ArrayRemoveAllManyMatches);
SUBJSON_BENCHMARK(ArrayInsertNearEnd);
SUBJSON_BENCHMARK(ArrayPrependPrimitives);
SUBJSON_BENCHMARK(ArrayGetCount);
SUBJSON_BENCHMARK(ArrayGetLast);
SUBJSON_BENCHMARK(ArrayReplaceLast);
SUBJSON_BENCHMARK(ArrayRemoveLast);
SUBJSON_BENCHMARK(GetAfterArrayOfPrimitives);
SUBJSON_BENCHMARK(GetAfterArrayOfObjects);
SUBJSON_BENCHMARK(CounterAfterArray);
SUBJSON_BENCHMARK(DictAddNewKey);
SUBJSON_BENCHMARK(DictUpsertNewKey);
SUBJSON_BENCHMARK(DictUpsertFirstKey);

BENCHMARK_MAIN();
