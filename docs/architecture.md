# Architecture

```text
libs/core/            library: public headers, sources, tests
  include/icl_template/
  src/
    platform/<os>/    one directory per platform, chosen by CMake
  tests/
apps/cli/             command-line application and its integration tests
apps/ios/             iPhone and iPad application: a one-screen UIKit sample and its packaging
  packaging/          Info.plist.in, asset catalogue (icon, launch colour), verify_app.sh
cmake/icl/            vendored CMake modules from icl-common-infra (do not edit)
tools/                project scripts, baselines and coverage floors
```

## Platform code

Platform differences live in `src/platform/<os>/` with the same file names in each directory.
`icl_platform_sources(target)` adds the directory that matches the target system. Source files
contain no `#if` on the platform or compiler; the preprocessor check fails the pull request if
one appears. Other directives are counted per file against `tools/preprocessor-baseline.json`,
which can go down but not up.

## Naming

Namespaces, functions and variables use `snake_case`. Types use `PascalCase`. Members use
`m_PascalCase`. Constants and enumerators use `UPPER_SNAKE_CASE`. Braces follow Allman style.
`.clang-format` and `.clang-tidy` enforce this.

## Vendored modules

`cmake/icl/` is a copy of `icl-common-infra/cmake/`. `cmake/icl/SOURCE` records the commit.
CI compares the copy with the commit its workflows pin and fails on a difference. Update with
`python3 tools/ci/sync_cmake.py --infra <checkout of icl-common-infra>`.
