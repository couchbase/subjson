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

#define CLIOPTS_ENABLE_CXX
#define INCLUDE_SUBDOC_NTOHLL
#include "subdoc/subdoc-api.h"
#include "subdoc/subdoc-util.h"
#include "subdoc/path.h"
#include "subdoc/match.h"
#include "subdoc/operations.h"
#include "contrib/cliopts/cliopts.h"

using std::string;
using std::vector;
using std::map;
using namespace cliopts;

class Options {
public:
    Options() :
        o_iter('i', "iterations", 1000),
        o_path('p', "docpath"),
        o_value('v', "value"),
        o_jsfile('f', "json"),
        o_cmd('c', "command"),
        parser("subdoc-bench")
    {
        o_iter.description("Number of iterations to run");
        o_path.description("Document path to manipulate").mandatory();
        o_value.description("Document value to insert");
        o_jsfile.description("JSON file to operate on");
        o_cmd.description("Command to use").mandatory();

        parser.addOption(o_iter);
        parser.addOption(o_path);
        parser.addOption(o_value);
        parser.addOption(o_jsfile);
        parser.addOption(o_cmd);

        totalBytes = 0;
        // Set the opmap
        initOpmap();
    }

    void initOpmap() {
        // generic ops:
        opmap["replace"] = SUBDOC_CMD_REPLACE;
        opmap["delete"] = SUBDOC_CMD_DELETE;
        opmap["get"] = SUBDOC_CMD_GET;
        opmap["exists"] = SUBDOC_CMD_EXISTS;

        // dict ops
        opmap["add"] = SUBDOC_CMD_DICT_ADD;
        opmap["upsert"] = SUBDOC_CMD_DICT_UPSERT;
        opmap["add_p"] = SUBDOC_CMD_DICT_ADD_P;
        opmap["upsert_p"] = SUBDOC_CMD_DICT_UPSERT_P;

        // list ops
        opmap["append"] = SUBDOC_CMD_ARRAY_APPEND;
        opmap["prepend"] = SUBDOC_CMD_ARRAY_PREPEND;
        opmap["addunique"] = SUBDOC_CMD_ARRAY_ADD_UNIQUE;
        opmap["append_p"] = SUBDOC_CMD_ARRAY_APPEND_P;
        opmap["prepend_p"] = SUBDOC_CMD_ARRAY_PREPEND_P;
        opmap["addunique_p"] = SUBDOC_CMD_ARRAY_ADD_UNIQUE_P;

        // arithmetic ops
        opmap["incr"] = SUBDOC_CMD_INCREMENT;
        opmap["decr"] = SUBDOC_CMD_DECREMENT;
        opmap["incr_p"] = SUBDOC_CMD_INCREMENT_P;
        opmap["decr_p"] = SUBDOC_CMD_DECREMENT_P;
    }

    UIntOption o_iter;
    StringOption o_path;
    StringOption o_value;
    StringOption o_jsfile;
    StringOption o_cmd;
    map<string,uint8_t> opmap;
    Parser parser;
    size_t totalBytes;
};

#ifdef _WIN32
#include <windows.h>
static uint64_t
get_nstime(void) {
    double ret;
    static LARGE_INTEGER pf = { 0 };
    static double freq;
    LARGE_INTEGER currtime;

    if (pf.QuadPart == 0) {
        QueryPerformanceFrequency(&pf);
        freq = 1.0e9 / (double)pf.QuadPart;
    }

    QueryPerformanceCounter(&currtime);

    ret = (double)currtime.QuadPart * freq ;
    return (uint64_t)ret;
}
#else
#include <sys/time.h>
static uint64_t
get_nstime(void) {
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return (tv.tv_sec * 1000000000) + (tv.tv_usec * 1000);
}
#endif

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

    const uint8_t opcode = o.opmap[o.o_cmd.const_result()];
    string value = o.o_value.const_result();
    string path = o.o_path.const_result();
    const char *vbuf = value.c_str();
    size_t nvbuf = value.length();
    uint64_t dummy;

    switch (opcode) {
    case SUBDOC_CMD_INCREMENT:
    case SUBDOC_CMD_INCREMENT_P:
    case SUBDOC_CMD_DECREMENT:
    case SUBDOC_CMD_DECREMENT_P: {
        int64_t ctmp = (uint64_t)strtoll(vbuf, NULL, 10);
        if (ctmp == LLONG_MAX && errno == ERANGE) {
            throw string("Invalid delta for arithmetic operation!");
        }
        dummy = ctmp;
        dummy = htonll(dummy);
        vbuf = (const char *)&dummy;
        nvbuf = sizeof dummy;
        break;
    }
    }

    subdoc_OPERATION *op = subdoc_op_alloc();


    size_t itermax = o.o_iter.result();
    for (size_t ii = 0; ii < itermax; ii++) {
        subdoc_op_clear(op);
        const string& curInput = inputStrs[ii % inputStrs.size()];
        SUBDOC_OP_SETCODE(op, opcode);
        SUBDOC_OP_SETDOC(op, curInput.c_str(), curInput.size());
        SUBDOC_OP_SETVALUE(op, vbuf, nvbuf);

        uint16_t rv = subdoc_op_exec(op, path.c_str(), path.size());
        if (rv != SUBDOC_STATUS_SUCCESS) {
            throw string("Operation failed!");
        }
    }

    // Print the result.
    if (opcode == SUBDOC_CMD_GET || opcode == SUBDOC_CMD_EXISTS) {
        string match(op->match.loc_match.at, op->match.loc_match.length);
        printf("%s\n", match.c_str());
    } else {
        string newdoc;
        for (size_t ii = 0; ii < op->doc_new_len; ii++) {
            const subdoc_LOC *loc = &op->doc_new[ii];
            newdoc.append(loc->at, loc->length);
        }
        printf("%s\n", newdoc.c_str());
    }

    subdoc_op_free(op);
}

static void
execPathParse(Options& o)
{
    size_t itermax = o.o_iter.result();
    string path = o.o_path.const_result();
    subdoc_PATH *pth = subdoc_path_alloc();

    for (size_t ii = 0; ii < itermax; ii++) {
        subdoc_path_clear(pth);
        int rv = subdoc_path_parse(pth, path.c_str(), path.size());

        if (rv != 0) {
            throw string("Failed to parse path!");
        }
    }

    subdoc_path_free(pth);
}

void runMain(int argc, char **argv)
{
    Options o;
    if (!o.parser.parse(argc, argv)) {
        throw string("Bad options!");
    }
    // Determine the command
    string cmdStr = o.o_cmd.const_result();

    uint64_t t_begin = get_nstime();

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

    uint64_t t_total = get_nstime() - t_begin;
    // Get the number of seconds:
    double n_seconds = t_total / 1000000000.0;
    double ops_per_sec = (double)o.o_iter.result() / n_seconds;
    double mb_per_sec = ((double)o.totalBytes * (double)o.o_iter.result()) / n_seconds;
    mb_per_sec /= (1024 * 1024);

    fprintf(stderr, "DURATION=%.2lfs. OPS=%lu\n", n_seconds, o.o_iter.result());
    fprintf(stderr, "%.2lf OPS/s\n",  ops_per_sec);
    fprintf(stderr, "%.2lf MB/s\n", mb_per_sec);
}

int main(int argc, char **argv)
{
    try {
        runMain(argc, argv);
        return EXIT_SUCCESS;
    } catch (string& exc) {
        fprintf(stderr, "%s\n", exc.c_str());
        return EXIT_FAILURE;
    }
}
