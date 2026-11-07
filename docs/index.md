# ICL C++ template

A small C++23 project that builds, tests and releases with the shared infrastructure in
[icl-common-infra](https://github.com/iainchesworthlabs/icl-common-infra). Copy it to start a project.

It contains one library (`libs/core`), one command-line application (`apps/cli`), the CMake
presets, the CI callers, the repository settings as files, and the check configuration.
The code is small on purpose so the structure is easy to see.

| Page | Covers |
|---|---|
| [Building](building.md) | Prerequisites, shared roots, presets |
| [Architecture](architecture.md) | Layout, platform sources, naming |
| [Testing](testing.md) | Test kinds, labels, coverage floors |
| [Continuous integration](ci.md) | What runs when, required checks |
| [Releasing](releasing.md) | Changelog, tags, signing, publishing |
| [Starting a project](new-project.md) | Instantiate and configure a new repository |
