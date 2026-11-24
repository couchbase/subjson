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
 * Benchmarks for subdoc operations on a JSON document read from a file
 * (for instance "real world" data such as twitter.json), so that the effect
 * of a change can be measured on documents of different shapes.
 *
 * As the document is only parsed until the path is found, the cost of an
 * operation depends on where in the document the value is. The program
 * therefore picks a number of paths spread out over the document (by the
 * offset of the end of their value, preferring a mix of strings, numbers,
 * arrays and objects, and true, false and null) and benchmarks a Get of
 * each, as well as validating the whole document as a baseline. The paths
 * are picked deterministically, so the same paths are used when comparing
 * builds.
 *
 * Usage: subjson-file-bench [benchmark options] [--paths=<count>]
 *                           [--path=<path> ...] <file>
 */

#define INCLUDE_JSONSL_SRC
#include "subdoc/jsonsl_header.h"
#include "subdoc/operations.h"
#include "subdoc/path.h"
#include "subdoc/validate.h"
#include <benchmark/benchmark.h>
#include <algorithm>
#include <array>
#include <charconv>
#include <cmath>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <iterator>
#include <limits>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <unordered_set>
#include <vector>

using Subdoc::Command;
using Subdoc::Error;
using Subdoc::Operation;
using Subdoc::Result;

namespace {

/** The kind of a value in the document */
enum class Kind { String, Number, Container, Literal };

const char* to_string(Kind kind) {
    switch (kind) {
    case Kind::String:
        return "string";
    case Kind::Number:
        return "number";
    case Kind::Container:
        return "container";
    case Kind::Literal:
        return "literal";
    }
    return "unknown";
}

/** A value in the document which may be the target of an operation */
struct Candidate {
    std::string path;
    /** The offset of the first character of the value */
    size_t begin = 0;
    /** The offset of the character following the value */
    size_t end = 0;
    Kind kind = Kind::String;
};

/**
 * Collects the paths of the values in a (validated) JSON document, with
 * their position in the document, ordered by the offset of the end of the
 * value. Values with a key which can't be expressed in a path (empty, or
 * containing an escape sequence or a control character), with a key which
 * is a duplicate of an earlier key in the same object (as a Get finds the
 * first), or which are too deep for a path, are left out.
 *
 * As the validation done by jsonsl isn't strict (it accepts for instance
 * "[1 2]"), the collector checks the structure itself, and throws
 * std::runtime_error if the document isn't valid JSON.
 */
class PathCollector {
public:
    explicit PathCollector(std::string_view doc) : doc(doc) {
    }

    std::vector<Candidate> collect() {
        pos = 0;
        skipWhitespace();
        value({}, 0, true);
        return std::move(candidates);
    }

private:
    void value(const std::string& path, size_t depth, bool usable) {
        const size_t begin = pos;
        Kind kind;
        switch (peek()) {
        case '{':
            kind = Kind::Container;
            object(path, depth, usable);
            break;
        case '[':
            kind = Kind::Container;
            array(path, depth, usable);
            break;
        case '"':
            kind = Kind::String;
            string();
            break;
        case 't':
        case 'f':
        case 'n':
            kind = Kind::Literal;
            token();
            break;
        default:
            kind = Kind::Number;
            token();
            break;
        }
        if (pos == begin) {
            fail("expected a value");
        }
        if (depth > 0 && usable) {
            candidates.push_back(
                    {.path = path, .begin = begin, .end = pos, .kind = kind});
        }
    }

    void object(const std::string& path, size_t depth, bool usable) {
        ++pos;
        skipWhitespace();
        if (peek() == '}') {
            ++pos;
            return;
        }
        std::unordered_set<std::string_view> keys;
        for (;;) {
            if (peek() != '"') {
                fail("expected a key");
            }
            const auto key = string();
            const bool first = keys.insert(key).second;
            const auto component = pathComponent(key);
            skipWhitespace();
            expect(':');
            skipWhitespace();
            // The path has a component for the root in addition to one for
            // each level
            const bool childUsable = usable && first && component.has_value() &&
                                     depth + 1 < Subdoc::Limits::MAX_COMPONENTS;
            std::string childPath;
            if (childUsable) {
                childPath = path.empty() ? *component : path + "." + *component;
            }
            value(childPath, depth + 1, childUsable);
            skipWhitespace();
            if (separator('}')) {
                return;
            }
            skipWhitespace();
        }
    }

    void array(const std::string& path, size_t depth, bool usable) {
        ++pos;
        skipWhitespace();
        if (peek() == ']') {
            ++pos;
            return;
        }
        for (size_t index = 0;; ++index) {
            const bool childUsable =
                    usable && depth + 1 < Subdoc::Limits::MAX_COMPONENTS;
            value(childUsable ? path + "[" + std::to_string(index) + "]"
                              : std::string{},
                  depth + 1,
                  childUsable);
            skipWhitespace();
            if (separator(']')) {
                return;
            }
            skipWhitespace();
        }
    }

    /** Skip the string at the current position, and return its contents */
    std::string_view string() {
        const size_t begin = ++pos;
        while (peek() != '"') {
            pos += doc[pos] == '\\' ? 2 : 1;
        }
        return doc.substr(begin, pos++ - begin);
    }

    /** The character at the current position */
    char peek() const {
        if (pos >= doc.size()) {
            fail("unexpected end of document");
        }
        return doc[pos];
    }

    void expect(char ch) {
        if (peek() != ch) {
            fail(std::string("expected '") + ch + "'");
        }
        ++pos;
    }

    /**
     * Skip the separator following a member or element: ',' or the
     * character closing the container.
     *
     * @return true if the container was closed
     */
    bool separator(char close) {
        const auto ch = peek();
        if (ch != ',' && ch != close) {
            fail(std::string("expected ',' or '") + close + "'");
        }
        ++pos;
        return ch == close;
    }

    [[noreturn]] void fail(const std::string& message) const {
        throw std::runtime_error(message + " at offset " + std::to_string(pos));
    }

    /** Skip the number, true, false or null at the current position */
    void token() {
        while (pos < doc.size() &&
               std::string_view(",]} \t\n\r").find(doc[pos]) ==
                       std::string_view::npos) {
            ++pos;
        }
    }

    void skipWhitespace() {
        while (pos < doc.size() && (doc[pos] == ' ' || doc[pos] == '\t' ||
                                    doc[pos] == '\n' || doc[pos] == '\r')) {
            ++pos;
        }
    }

    /**
     * The path component for the key, quoted with backticks if it contains
     * characters with a special meaning in paths, or nothing if it can't be
     * used in a path (the path parser doesn't accept control characters,
     * which jsonsl accepts some of).
     */
    static std::optional<std::string> pathComponent(std::string_view key) {
        if (key.empty() || key.find('\\') != std::string_view::npos ||
            std::ranges::any_of(key, [](char ch) {
                return static_cast<unsigned char>(ch) < 0x20;
            })) {
            return {};
        }
        if (key.find_first_of(".[]`") == std::string_view::npos) {
            return std::string(key);
        }
        std::string quoted = "`";
        for (const auto ch : key) {
            quoted.push_back(ch);
            if (ch == '`') {
                quoted.push_back('`');
            }
        }
        quoted.push_back('`');
        return quoted;
    }

    const std::string_view doc;
    size_t pos = 0;
    std::vector<Candidate> candidates;
};

/**
 * Pick count paths spread out over the document: for each of count evenly
 * spaced target offsets, the value ending closest to it, preferring (within
 * half the distance between the targets) a value of the kind next in turn.
 * The candidates must be ordered by the offset of the end of the value.
 */
std::vector<Candidate> pickPaths(std::vector<Candidate> candidates,
                                 size_t docSize,
                                 size_t count) {
    static constexpr std::array<Kind, 4> kinds = {
            Kind::String, Kind::Container, Kind::Number, Kind::Literal};
    std::vector<Candidate> picked;
    std::vector<bool> used(candidates.size());
    const double spacing =
            static_cast<double>(docSize) / static_cast<double>(count);
    auto distance = [&](size_t idx, double target) {
        return std::abs(static_cast<double>(candidates[idx].end) - target);
    };
    // The index of the first candidate ending at or after offset
    auto lowerBound = [&](double offset) {
        return static_cast<size_t>(
                std::ranges::lower_bound(candidates,
                                         offset,
                                         {},
                                         [](const auto& c) {
                                             return static_cast<double>(c.end);
                                         }) -
                candidates.begin());
    };
    for (size_t ii = 0; ii < count && picked.size() < candidates.size(); ++ii) {
        const double target = (static_cast<double>(ii) + 0.5) * spacing;
        const Kind preferred = kinds[ii % kinds.size()];

        // The closest unused candidate of the preferred kind within the
        // window (on a tie the first)
        std::optional<size_t> best;
        const auto windowEnd = lowerBound(std::nextafter(
                target + (spacing / 2), std::numeric_limits<double>::max()));
        for (auto idx = lowerBound(target - (spacing / 2)); idx < windowEnd;
             ++idx) {
            if (!used[idx] && candidates[idx].kind == preferred &&
                (!best || distance(idx, target) < distance(*best, target))) {
                best = idx;
            }
        }

        if (!best) {
            // The closest unused candidate (on a tie the first), searching
            // outwards from the target
            const auto start = lowerBound(target);
            size_t left = start;
            size_t right = start;
            while (left > 0 && used[left - 1]) {
                --left;
            }
            while (right < candidates.size() && used[right]) {
                ++right;
            }
            if (left > 0 &&
                (right == candidates.size() ||
                 distance(left - 1, target) <= distance(right, target))) {
                best = left - 1;
            } else {
                best = right;
            }
        }
        used[*best] = true;
        picked.push_back(candidates[*best]);
    }
    return picked;
}

/**
 * Run a Get of the path on the document.
 *
 * @return The location of the value, or nothing if it failed
 */
std::optional<std::string_view> get(const std::string& doc,
                                    const std::string& path,
                                    Error& error) {
    Operation op;
    Result result;
    op.set_doc(doc.data(), doc.size());
    op.set_code(Command::GET);
    op.set_result_buf(&result);
    error = op.op_exec(path);
    if (error != Error::SUCCESS) {
        return {};
    }
    return std::string_view(result.matchloc().at, result.matchloc().length);
}

/**
 * Benchmark a Get of the path. The bytes processed are those up to the
 * end of the value, as the document is only parsed that far.
 */
void runGet(benchmark::State& state,
            const std::string& doc,
            const std::string& path,
            size_t end) {
    Operation op;
    Result result;
    while (state.KeepRunning()) {
        op.clear();
        result.clear();
        op.set_doc(doc.data(), doc.size());
        op.set_code(Command::GET);
        op.set_result_buf(&result);
        const auto rv = op.op_exec(path);
        if (rv != Error::SUCCESS) {
            state.SkipWithError("Get of " + path +
                                " returned: " + rv.description());
            return;
        }
        const char* match = result.matchloc().at;
        benchmark::DoNotOptimize(match);
    }
    state.SetBytesProcessed(static_cast<int64_t>(state.iterations()) *
                            static_cast<int64_t>(end));
}

/** Benchmark validating the whole document, as a baseline */
void runValidate(benchmark::State& state, const std::string& doc) {
    jsonsl_t jsn = jsonsl_new(Subdoc::Limits::PARSER_DEPTH);
    while (state.KeepRunning()) {
        const auto rv = Subdoc::Validator::validate(doc, jsn);
        if (rv != JSONSL_ERROR_SUCCESS) {
            state.SkipWithError(std::string("Validation failed: ") +
                                Subdoc::Validator::errstr(rv));
            break;
        }
    }
    jsonsl_destroy(jsn);
    state.SetBytesProcessed(static_cast<int64_t>(state.iterations()) *
                            static_cast<int64_t>(doc.size()));
}

[[noreturn]] void usage(const char* program) {
    std::cerr << "Usage: " << program
              << " [benchmark options] [--paths=<count>] [--path=<path> ...]"
                 " <file>\n\n"
                 "Benchmarks a Get of <count> (default 10) paths spread out "
                 "over the JSON\ndocument in <file>, or of the paths given "
                 "with --path, and validating\nthe whole document.\n";
    std::exit(EXIT_FAILURE);
}

} // namespace

int main(int argc, char** argv) {
    benchmark::Initialize(&argc, argv);

    std::string filename;
    std::optional<size_t> count;
    std::vector<std::string> paths;
    for (int ii = 1; ii < argc; ++ii) {
        const std::string_view arg = argv[ii];
        if (arg.starts_with("--paths=")) {
            const auto value = arg.substr(std::string_view("--paths=").size());
            size_t num = 0;
            const auto [ptr, ec] = std::from_chars(
                    value.data(), value.data() + value.size(), num);
            if (ec != std::errc() || ptr != value.data() + value.size() ||
                num == 0) {
                usage(argv[0]);
            }
            count = num;
        } else if (arg.starts_with("--path=")) {
            paths.emplace_back(arg.substr(std::string_view("--path=").size()));
        } else if (arg.starts_with("--") || !filename.empty()) {
            usage(argv[0]);
        } else {
            filename = arg;
        }
    }
    if (filename.empty()) {
        usage(argv[0]);
    }
    if (count && !paths.empty()) {
        std::cerr << "--paths and --path can't be combined\n\n";
        usage(argv[0]);
    }

    std::ifstream file(filename, std::ios::binary);
    if (!file) {
        std::cerr << "Failed to open " << filename << "\n";
        return EXIT_FAILURE;
    }
    const std::string doc{std::istreambuf_iterator<char>(file),
                          std::istreambuf_iterator<char>()};
    const auto valid = Subdoc::Validator::validate(doc, nullptr);
    if (valid != JSONSL_ERROR_SUCCESS) {
        std::cerr << filename << " isn't a valid JSON document: "
                  << Subdoc::Validator::errstr(valid) << "\n";
        return EXIT_FAILURE;
    }

    std::vector<Candidate> picked;
    if (paths.empty()) {
        std::vector<Candidate> candidates;
        try {
            candidates = PathCollector(doc).collect();
        } catch (const std::runtime_error& e) {
            std::cerr << filename
                      << " isn't a valid JSON document: " << e.what() << "\n";
            return EXIT_FAILURE;
        }
        picked = pickPaths(
                std::move(candidates), doc.size(), count.value_or(10));
    } else {
        for (const auto& path : paths) {
            picked.push_back({.path = path});
        }
    }

    // Verify that the paths are found (and are the values picked), and
    // list them
    std::cerr << "Document: " << filename << " (" << doc.size() << " bytes)\n";
    for (auto& candidate : picked) {
        Error error;
        const auto match = get(doc, candidate.path, error);
        if (!match) {
            std::cerr << "Get of " << candidate.path
                      << " failed: " << error.description() << "\n";
            return EXIT_FAILURE;
        }
        const auto begin = static_cast<size_t>(match->data() - doc.data());
        const auto end = begin + match->size();
        if (paths.empty() &&
            (begin != candidate.begin || end != candidate.end)) {
            std::cerr << "Get of " << candidate.path << " found the value at "
                      << begin << "-" << end << " rather than "
                      << candidate.begin << "-" << candidate.end << "\n";
            return EXIT_FAILURE;
        }
        candidate.end = end;
        std::cerr << "  " << (end * 100 / doc.size()) << "%\t";
        if (paths.empty()) {
            std::cerr << to_string(candidate.kind) << "\t";
        }
        std::cerr << candidate.path << "\n";
    }
    if (picked.empty()) {
        std::cerr << "  No paths found in the document (the values aren't in "
                     "an object or array,\n  or their keys can't be used in "
                     "a path), only validation is benchmarked\n";
    }

    benchmark::RegisterBenchmark("Validate", runValidate, doc);
    for (const auto& candidate : picked) {
        benchmark::RegisterBenchmark("Get/" + candidate.path,
                                     runGet,
                                     doc,
                                     candidate.path,
                                     candidate.end);
    }
    benchmark::RunSpecifiedBenchmarks();
    benchmark::Shutdown();
    return EXIT_SUCCESS;
}
