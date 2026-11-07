# Architecture

```text
libs/core/            ej_core: QtCore only; the domain model (Stage 1), namespace `ej`
  include/error_journaling/  public headers: uuid.hpp, ids.hpp (EntryId, ErrorTypeId, NoteTemplateId, LocationId),
                             header_field.hpp, result.hpp (the std::expected conventions), version.hpp,
                             timestamp.hpp, time_zone_provider.hpp (Qt implementation in src/qt_time_zone_provider.cpp,
                             the only file that includes QTimeZone), time_format.hpp, day_prompt.hpp,
                             configuration.hpp (the configuration file: schema, import, export)
  src/                       implementation files, one per component
  tests/                     Catch2 tests, all labelled `unit`
    golden/                  expected export files, compared byte for byte (docs/testing.md)
    fixtures/                other input files for tests
    golden.hpp, golden.cpp   the golden comparison helper
    fake_time_zone_provider.hpp  fixed-offset TimeZoneProvider for deterministic tests
libs/storage/         ej_storage: empty until Stage 2; links ej_core
libs/ios_bridge/      ej_ios_bridge: what Qt does not cover on an iPhone, behind interfaces
  src/platform/ios/      Objective-C++ (.mm), built for iOS
  src/platform/desktop/  stub, built everywhere else
apps/app/             ej_app: the Qt Quick application (executable error-journaling)
  qml/                the module ErrorJournalingApp (the URI differs from the executable name)
  src/                main.cpp
  packaging/ios.cmake    bundle properties, signing and the plugin defaults of Qt's iOS kit
  packaging/ios/         Info.plist.in, asset catalogue (icon, launch colour), verify_app.sh
cmake/icl/            vendored CMake modules from icl-common-infra (do not edit)
tools/                project scripts, baselines and coverage floors
```

## Targets

| Target | Kind | Depends on |
|---|---|---|
| `ej_core` | static library, C++23 | QtCore |
| `ej_storage` | static library | `ej_core` |
| `ej_ios_bridge` | static library; Objective-C++ on iOS, a stub elsewhere | nothing (UIKit and Foundation on iOS) |
| `ej_qml_bridge` | static library; the `qml_bridge` QObject QML calls, over the `ej_ios_bridge` interface | `ej_ios_bridge`, QtCore |
| `ej_app` | Qt Quick executable (`error-journaling`) | the libraries above, Qt Quick |

Each library has a placeholder test with the `unit` label; the application has two `ui` tests (`app.smoke` starts it
without a display and waits for the first frame, `app.version` answers `--version`). The application is the iPhone
application on iOS (portrait only, laid out inside `SafeArea.margins`) and a desktop program on macOS and Linux, built
to iterate on the user interface and to be tested in CI; no desktop package is released.

## Platform code

Platform differences live in `src/platform/<os>/` with the same file names in each directory.
`icl_platform_sources(target)` adds the directory that matches the target system; `ej_ios_bridge` picks its directory in
its own `CMakeLists.txt` (`ios` or `desktop`). Source files
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
