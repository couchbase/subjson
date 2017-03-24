/* -*- Mode: C++; tab-width: 4; c-basic-offset: 4; indent-tabs-mode: nil -*- */
/*
*     Copyright 2015 Couchbase, Inc
*
*   Licensed under the Apache License, Version 2.0 (the "License");
*   you may not use this file except in compliance with the License.
*   You may obtain a copy of the License at
*
*       http://www.apache.org/licenses/LICENSE-2.0
*
*   Unless required by applicable law or agreed to in writing, software
*   distributed under the License is distributed on an "AS IS" BASIS,
*   WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
*   See the License for the specific language governing permissions and
*   limitations under the License.
*/

#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#include <string.h>
#include <limits.h>
#include <errno.h>
#include <stdexcept>

#include <map>
#include <vector>
#include <string>
#include <fstream>
#include <iostream>
#include <istream>
#include <ostream>
#include <sstream>
#include <chrono>

#define CLIOPTS_ENABLE_CXX
#define INCLUDE_SUBDOC_NTOHLL
#include "subdoc/subdoc-api.h"
#include "subdoc/path.h"
#include "subdoc/match.h"
#include "subdoc/operations.h"
#include "contrib/cliopts/cliopts.h"

using std::string;
using std::vector;
using std::map;
using namespace cliopts;

using Subdoc::Path;
using Subdoc::Operation;
using Subdoc::Command;
using Subdoc::Error;
using Subdoc::Result;

struct OpEntry {
    uint8_t opcode;
    const char *description;
    OpEntry(uint8_t opcode = 0, const char *description = NULL) :
        opcode(opcode),
        description(description) {}

    operator uint8_t() const { return opcode; }
};

class Options {
public:
    Options() :
        o_iter('i', "iterations", 1000),
        o_path('p', "docpath"),
        o_value('v', "value"),
        o_jsfile('f', "json"),
        o_cmd('c', "command"),
        o_mkdirp('M', "create-intermediate"),
        o_txtscan('T', "text-scan"),
        parser("subdoc-bench")
    {
        o_iter.description("Number of iterations to run");
        o_path.description("Document path to manipulate");
        o_value.description("Document value to insert");
        o_jsfile.description("JSON files to operate on. If passing multiple files, each file should be delimited by a comma");
        o_cmd.description("Command to use. Use -c help to show all the commands").mandatory();
        o_mkdirp.description("Create intermediate paths for mutation operations");
        o_txtscan.description("Simply scan the text using a naive approach. Used to see how much actual overhead jsonsl induces");

        parser.addOption(o_iter);
        parser.addOption(o_path);
        parser.addOption(o_value);
        parser.addOption(o_jsfile);
        parser.addOption(o_cmd);
        parser.addOption(o_mkdirp);
        parser.addOption(o_txtscan);

        totalBytes = 0;
        // Set the opmap
        initOpmap();
    }

    void initOpmap() {
        // generic ops:
        opmap["replace"] = OpEntry(Command::REPLACE, "Replace a value");
        opmap["delete"] = OpEntry(Command::REMOVE, "Delete a value");
        opmap["get"] = OpEntry(Command::GET, "Retrieve a value");
        opmap["exists"] = OpEntry(Command::EXISTS, "Check if a value exists");

        // dict ops
        opmap["add"] = OpEntry(Command::DICT_ADD, "Create a new dictionary value");
        opmap["upsert"] = OpEntry(Command::DICT_UPSERT, "Create or replace a dictionary value");

        // list ops
        opmap["append"] = OpEntry(Command::ARRAY_APPEND, "Insert values to the end of an array");
        opmap["prepend"] = OpEntry(Command::ARRAY_PREPEND, "Insert values to the beginning of an array");
        opmap["addunique"] = OpEntry(Command::ARRAY_ADD_UNIQUE, "Add a unique value to an array");
        opmap["insert"] = OpEntry(Command::ARRAY_INSERT, "Insert value at given array index");
        opmap["size"] = OpEntry(Command::GET_COUNT, "Count the number of items in an array or dict");

        // arithmetic ops
        opmap["counter"] = OpEntry(Command::COUNTER);

        // Generic ops
        opmap["path"] = OpEntry(0xff, "Check the validity of a path");
    }

    UIntOption o_iter;
    StringOption o_path;
    StringOption o_value;
    StringOption o_jsfile;
    StringOption o_cmd;
    BoolOption o_mkdirp;
    BoolOption o_txtscan;
    map<string,OpEntry> opmap;
    Parser parser;
    size_t totalBytes;
};

static void
readJsonFile(string& name, vector<string>& out)
{
    std::ifstream input(name.c_str());
    if (input.fail()) {
        throw name + ": " + strerror(errno);
    }
    fprintf(stderr, "Reading %s\n", name.c_str());
    std::stringstream ss;
    ss << input.rdbuf();
    out.push_back(ss.str());
    input.close();
}

static void
execOperation(Options& o)
{
    vector<string> fileNames;
    vector<string> inputStrs;
    string flist = o.o_jsfile.const_result();
    if (flist.find(',') == string::npos) {
        fileNames.push_back(flist);
    } else {
        while (true) {
            size_t pos = flist.find_first_of(',');
            if (pos == string::npos) {
                fileNames.push_back(flist);
                break;
            } else {
                string curName = flist.substr(0, pos);
                fileNames.push_back(curName);
                flist = flist.substr(pos+1);
            }
        }
    }
    if (fileNames.empty()) {
        throw string("At least one file must be passed!");
    }
    for (size_t ii = 0; ii < fileNames.size(); ii++) {
        readJsonFile(fileNames[ii], inputStrs);
        o.totalBytes += inputStrs.back().length();
    }

    uint8_t opcode = o.opmap[o.o_cmd.result()];
    if (o.o_mkdirp.passed()) {
        opcode |= 0x80;
    }

    string value = o.o_value.const_result();
    string path = o.o_path.const_result();
    Operation op;
    Result res;


    size_t nquotes = 0;
    bool is_txtscan = o.o_txtscan.result();
    size_t itermax = o.o_iter.result();

    int char_table[256] = { 0 };
    char_table[static_cast<uint8_t >('"')] = 1;
    char_table[static_cast<uint8_t >('\\')] = 1;
    char_table[static_cast<uint8_t >('!')] = 1;

    for (size_t ii = 0; ii < itermax; ii++) {
        const string& curInput = inputStrs[ii % inputStrs.size()];

        if (is_txtscan) {
            const char *buf = curInput.c_str();
            size_t nbytes = curInput.size();
            for (; nbytes; buf++, nbytes--) {
                if (char_table[static_cast<unsigned char>(*buf)]) {
                    nquotes++;
                }
            }
            continue;
        }

        op.clear();
        res.clear();
        op.set_value(value);
        op.set_code(opcode);
        op.set_doc(curInput);
        op.set_result_buf(&res);

        Error rv = op.op_exec(path);
        if (!rv.success()) {
            throw rv;
        }
    }

    if (nquotes) {
        printf("Found %lu quotes!\n", nquotes);
    }

    // Print the result.
    if (opcode == Command::GET ||
            opcode == Command::EXISTS ||
            opcode == Command::GET_COUNT) {
        string match = res.matchloc().to_string();
        printf("%s\n", match.c_str());
    } else {
        string newdoc;
        for (auto ii : res.newdoc()) {
            newdoc.append(ii.at, ii.length);
        }
        printf("%s\n", newdoc.c_str());
    }
}

static void
execPathParse(Options& o)
{
    size_t itermax = o.o_iter.result();
    string path = o.o_path.const_result();
    Path pth;

    for (size_t ii = 0; ii < itermax; ii++) {
        pth.clear();
        int rv = pth.parse(path);

        if (rv != 0) {
            throw string("Failed to parse path!");
        }
    }
}

void runMain(int argc, char **argv)
{
    using namespace std::chrono;

    Options o;
    if (!o.parser.parse(argc, argv)) {
        throw string("Bad options!");
    }
    // Determine the command
    string cmdStr = o.o_cmd.const_result();
    auto t_begin = steady_clock::now();

    if (cmdStr == "help") {
        map<string,OpEntry>::const_iterator iter = o.opmap.begin();
        for (; iter != o.opmap.end(); ++iter) {
            const OpEntry& ent = iter->second;
            fprintf(stderr, "%s (0x%x): ", iter->first.c_str(), ent.opcode);
            fprintf(stderr, "%s\n", ent.description);
        }
        exit(EXIT_SUCCESS);
    }
    if (!o.o_path.passed()) {
        fprintf(stderr, "Path (-p) required\n");
        exit(EXIT_FAILURE);
    }

    if (o.opmap.find(cmdStr) != o.opmap.end()) {
        if (!o.o_jsfile.passed()) {
            throw string("Operation must contain file!");
        }
        execOperation(o);
    } else if (cmdStr == "path") {
        execPathParse(o);
    } else {
        throw string("Unknown command!");
    }

    auto total = duration_cast<duration<double>>(steady_clock::now() - t_begin);

    // Get the number of seconds:
    double n_seconds = total.count();
    double ops_per_sec = static_cast<double>(o.o_iter.result()) / n_seconds;
    double mb_per_sec = (
            static_cast<double>(o.totalBytes) *
            static_cast<double>(o.o_iter.result())) /
                    n_seconds;

    mb_per_sec /= (1024 * 1024);

    fprintf(stderr, "DURATION=%.2fs. OPS=%u\n", n_seconds, o.o_iter.result());
    fprintf(stderr, "%.2f OPS/s\n",  ops_per_sec);
    fprintf(stderr, "%.2f MB/s\n", mb_per_sec);
}

int main(int argc, char **argv)
{
    try {
        runMain(argc, argv);
        return EXIT_SUCCESS;
    } catch (string& exc) {
        fprintf(stderr, "%s\n", exc.c_str());
        return EXIT_FAILURE;
    } catch (Error& rc) {
        fprintf(stderr, "Command failed: %s\n", rc.description());
        return EXIT_FAILURE;
    } catch (std::exception& ex) {
        fprintf(stderr, "Command failed: %s\n", ex.what());
        return EXIT_FAILURE;
    }
}
