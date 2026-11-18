# Tests check behavior

## Decision

Tests run code on inputs and check its results and effects. A test never reads source files and never pins hashes of repository files. C++ tests sit next to their code as `*.test.cpp`; Java tests live under `src/prepare/test`.

## Context

Voyemu has many `guard` checks, recipes and ownership checks whose correctness matters more than their wording. Asserting that a source file contains a certain string is a quick way to "cover" such code.

## Alternatives

**Source-text assertions.** A test searches a source file for an expected call or constant.

**Golden hashes of repository files.** A test fails when a tracked file changes.

## Argument

A source-text test passes whenever the text exists, whether or not the code works, and fails on a refactoring that keeps the behavior. A hash test fails on every edit and proves nothing about the result. A behavior test survives refactoring and fails on a regression, which is what a test is for.

## Consequences

Fixtures are synthetic or built by the test itself. Tests that need OEM inputs or real graphics exit with the skip code 77 when their pinned inputs are absent. A bug fix starts from a failing regression test when practical. Host tests check host behavior; a guest scenario still needs acceptance on a running machine. The same rule keeps [pins](pins.md) limited to external inputs. [Tests](../dev/tests.md) describes where tests live and how they run.
