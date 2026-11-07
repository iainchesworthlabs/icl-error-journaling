# icl-cpp-template

[![CI](https://github.com/iainchesworthlabs/icl-cpp-template/actions/workflows/ci.yml/badge.svg?branch=develop)](https://github.com/iainchesworthlabs/icl-cpp-template/actions/workflows/ci.yml)

A C++23 starting point for projects in the `iainchesworthlabs` organisation. It builds with CMake
presets and vcpkg, tests with Catch2 and CTest, and uses the shared CI, release and repository settings in
[icl-common-infra](https://github.com/iainchesworthlabs/icl-common-infra).

```bash
cmake --preset linux-debug
cmake --build --preset linux-debug
ctest --preset linux-debug
build/linux-debug/apps/cli/icl_template_cli --version
```

Start a new project with `python3 tools/instantiate.py --name my_project`; see
[docs/new-project.md](docs/new-project.md). Other documentation is in `docs/`: building, architecture,
testing, CI and releasing.

The template has no Qt, SIMD or Tracy source code yet. Qt is found through `FindQt6.cmake` and Tracy is
available as the `profiling` vcpkg feature; add the scaffolding when the first project needs it.

Licence: GPL-3.0-or-later. Contributions need a DCO sign-off (`git commit -s`).
