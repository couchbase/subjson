# AGENTS.md — subjson Repository Guide

Repository (mirror): https://github.com/couchbase/subjson

Primary code review system: https://review.couchbase.org

---

## 📌 Purpose

This document defines expectations for:

- Automated coding agents
- Human contributors
- Reviewers

subjson uses Gerrit (review.couchbase.org) for all change submission,
validation, and approval.

GitHub is a mirror only. Do NOT open GitHub Pull Requests.

---

## 🧱 Repository Overview

Welcome to the Couchbase _subjson_ project.

The committed README.md provides a high-level overview of the project and is a
good starting point for new contributors. For more detailed information on
architecture, protocols, and other topics, please refer to the various
documentation files linked from the README:

@README.md


## 🚦 Authoritative Contribution Workflow

All changes must be submitted via:

    https://review.couchbase.org

### Standard Flow

1. Sync with latest target branch (typically `master`)
2. Create a local topic branch (if `repo` is available):

       `repo start name-of-topic-branch`

3. Make code changes and run `git clang-format` to fix any formatting errors before committing.
   Verify that the changes don't introduce any new `clang-tidy` warnings.
4. Make atomic, focused commits
5. Always create suitable unit tests
6. Run local build and tests
7. Sync with upstream and rebase the change. If `repo` is available:

       repo sync .
       repo rebase .

8. Push to Gerrit (substitute master for correct branch when working on named
   branches):

       git push gerrit HEAD:refs/for/master

   Alternatively, if the `repo` tool is available:

       repo upload --cbr .

9. CI validation runs automatically on jenkins servers that are monitoring for
   new code pushed to gerrit.
10. Review feedback is asynchronous

---

## 📐 Code Standards

Language: **C++ (up to C++23 supported)**

Guidelines:

- Prefer modern C++ where appropriate (C++23 features are supported)
- Use RAII, smart pointers, and standard library facilities where they improve
  safety and clarity
- Structured bindings, `std::optional`, `std::variant`, `constexpr`, and other
  modern features are acceptable
- Prefer `std::expected` over a `std::variant<T, Error>` (or similar ad-hoc
  encoding) when a function can fail with an informative error. `std::expected`
  is the idiomatic, standard-library way to express "value or error" in
  modern C++ (C++23) — it makes the success/failure contract explicit in the
  return type, avoids the boilerplate of visiting a variant to distinguish
  the value case from the error case, and composes naturally with
  `and_then`/`or_else`/`transform`. Only fall back to exceptions or
  `std::optional` where the surrounding code's existing error-handling style
  makes that the better fit.
- Use designated initializers introduced in C++20
- Don't use C-style arrays, use `std::array` or `std::vector` instead
- Don't use single character variable names
- Always use braces for control structures, even if they are optional
- Match the surrounding style within the modified module.
- It is acceptable to use older constructs if required for consistency with the
  existing code in that area
- Avoid unnecessary large-scale modernization in unrelated code
- Run `git clang-format` to format changed lines and fix any formatting errors before committing
- Maintain cross-platform compatibility
- New source files must use the current year in the banner comment.
- The `C++ Core Guidelines<http://isocpp.github.io/CppCoreGuidelines/CppCoreGuidelines>`
  are also a key part of our coding style.
- Always build (see building hints) before running tests!
- Use Doxygen style for all code comments, using `/** ... */` block comment
  style exclusively (not `///` or `/*!`). The first sentence of the comment
  acts as the brief description (do not use the `@brief` tag explicitly).
  Use tags such as `@param`, `@return`, and `@throws`.

Language: **CMake**

Guidelines:

- Use modern CMake style (target-based, avoid global properties)
- Prefer `target_include_directories` and `target_link_libraries` for dependencies
- Follow existing patterns in the repository for consistency
- Avoid global variables and side effects in CMake scripts
- The `CMake Best Practices<https://cliutils.gitlab.io/modern-cmake/>` are also a key part of our CMake style.

## New C++ File Headers
Whenever you create a new C++ file (e.g., `.cc`, or `.h`), you must prepend the exact copyright header specified below.

### Execution Rules
1. **Dynamic Year**: Automatically replace [YEAR] with the current year (e.g. 2026).
2. **Existing Files**: Do not modify or duplicate the header if editing an existing file.

### C++ Header Template
```cpp
/*
 *     Copyright [YEAR]-Present Couchbase, Inc.
 *
 *   Use of this software is governed by the Business Source License included
 *   in the file licenses/BSL-Couchbase.txt.  As of the Change Date specified
 *   in that file, in accordance with the Business Source License, use of this
 *   software will be governed by the Apache License, Version 2.0, included in
 *   the file licenses/APL2.txt.
 */
```

---

## Building hints

The project uses CMake for build configuration.

- The build directory is `build/` in the parent directory of the `subjson`
  git repository (`../build` relative to the root of the repository).
- To build all code (including all tests) use the target `subjson_everything`,
  which can be invoked from the build directory.
- Prefer to reuse the existing build directory and do incremental builds.
  - Do not create a new build directory unless asked.
- Prefer `ninja` when building.
- If configuring a new build, prefer using the script ../Build.sh (should be executed
  in the parent directory)

---

## 🚨 Safety Rules

Agents must not:

- Change file formats silently
- Break backward compatibility unintentionally
- Reformat large areas of code
- Submit speculative refactors
- Bypass Gerrit workflow
- Create new build directories unless asked

## 📝 Commit Message Requirements

All commit messages must:

- Strict follow the industry-standard 50/72 rule
- Start with: `MB-XXXXX: Short summary`
- Include a clear explanation of:
  - What changed
  - Why it changed
  - Risk or compatibility impact
- Don't add a 'Co-authored-by' tag for automated agents. The author information
  is already captured in the commit metadata.

Example:

```
MB-12345: Fix incorrect StoredValue::size calculation

Correct off-by-one error in StoredValue::size when the value is
compressed.
```

### Change-Id Handling

Do NOT manually add or generate a `Change-Id`.

A local Gerrit commit-msg hook is expected to already be installed and will
automatically add the required `Change-Id` footer when the commit is created
or amended.

Agents must not fabricate or modify the Change-Id.

When amending an existing commit (e.g. to update the commit message or add
further changes), preserve the `Change-Id` line exactly as it appears in the
original commit message. Dropping or replacing the `Change-Id` on an amended
commit would create a new Gerrit review instead of updating the existing one.

---

## 🧪 Testing Requirements

All functional changes must include:

- New or updated unit tests
- Passing test suite locally
- No unintended regression in file format compatibility
