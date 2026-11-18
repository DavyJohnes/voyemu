# No Python

## Decision

The repository contains no Python files and no Python tooling. Build logic is CMake, host code is C++, preparation and developer checks are Java, and short glue scripts are shell.

## Context

Voyemu builds on macOS, Windows and Linux from pinned toolchains. Python is a common choice for build helpers, but its version and packages differ between hosts, and Windows has no system Python.

## Alternatives

**Python helper scripts.** Each host would need a pinned interpreter and packages, or the scripts would depend on whatever Python the host has.

**Python only for development tools.** A second language for checks and generators duplicates what Java and CMake already do in the repository.

## Argument

Java is already required for [preparation](java-preparation.md), and a single-file Java program runs directly with `java File.java`, so developer checks need no extra runtime. CMake scripts run wherever the build runs. Fewer languages mean fewer toolchains to pin and fewer places for host differences.

## Consequences

Generators and checks are written in Java, CMake or C++. External tools that contain their own Python keep it: building QEMU from source uses the Command Line Tools `python3` on macOS and an MSYS2 Python that Voyemu provisions on Windows, and the official NDK ships its own Python tools. Python remains acceptable as a personal working tool outside the repository.
