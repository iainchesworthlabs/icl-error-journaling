# Building

## Prerequisites

CMake 4.0 or later, Ninja, and a C++23 compiler with `std::expected` and the C++23 library. CI builds with the
floor compilers, installed from the pins in `.github/toolchain-versions.json`: GCC 16, Clang 23 (with GCC 16's
libstdc++), MSVC 14.51 (a newer toolset passes) and the newest Apple Clang the macOS runners offer (Xcode 26.6 on
`macos-26`, with Xcode 27 tried in a leg that cannot fail the run). Older compilers may work but are not tested.

## Dependencies

Dependencies come from vcpkg, using the `iainchesworthlabs/vcpkg` fork. The build finds the fork, and Qt for a
project that uses it, through environment variables with the `ICL_` prefix, which are shared by every project of
the organisation; a variable with the prefix `ICL_TEMPLATE_` overrides it for this project. The presets do not
hard-code paths.

| Variable | Meaning |
|---|---|
| `ICL_VCPKG_STORE` | A directory for vcpkg checkouts. The commit named by `builtin-baseline` in `vcpkg.json` gets its own checkout in it (`<store>/<first 12 characters>`), made once and reused by every project that pins the same commit. |
| `ICL_VCPKG_MIRROR` | A clone of the fork to make those checkouts from. Default `<store>/mirror`, cloned on first use. |
| `ICL_QT_ROOT` | A prebuilt Qt kit, for example `~/Qt/6.12.0/gcc_64`. `QT_ROOT_DIR` and `QTDIR` are the fallbacks. Qt is not a vcpkg dependency; install it with aqtinstall. |
| `ICL_TEMPLATE_VCPKG_ROOT`, `ICL_TEMPLATE_QT_ROOT` | A vcpkg checkout or Qt kit used for this project only. |

A configure with none of them set stops and lists the options. Set them once in the shell profile. A project with a
Qt application pins the version in the `qt` key of `.github/toolchain-versions.json`; CI installs it from there.

## Presets

```bash
cmake --preset linux-debug
cmake --build --preset linux-debug
ctest --preset linux-debug
```

| Preset | Purpose |
|---|---|
| `linux-debug` | Debug with Tracy available (feature `profiling`) |
| `linux-release` | Optimised build |
| `linux-asan-ubsan` | AddressSanitizer and UndefinedBehaviorSanitizer |
| `linux-tsan` | ThreadSanitizer |
| `linux-coverage` | Instrumented build for gcovr |
| `macos-release`, `windows-msvc-release`, `windows-msvc-asan` | Platform builds |
| `ios-release` | The iPhone and iPad application (`apps/ios`), unsigned, on a Mac with Xcode; no tests and no command-line program |

Sanitizers, Tracy, coverage and fuzzing exclude each other. The configure step stops with a
message when two are enabled.

### iOS

`cmake --preset ios-release` needs a vcpkg checkout at the baseline of `vcpkg.json` (`VCPKG_ROOT`; CI's `setup-vcpkg` action
sets it) and Xcode. It builds `build/ios-release/icl-template.app` without signing. Three cache variables make a signed
build, which is the release workflow's (`docs/releasing.md`, "iOS and TestFlight"): `ICL_TEMPLATE_IOS_SIGNING_TEAM` and
`ICL_TEMPLATE_IOS_SIGNING_PROFILE` go together (CMake stops when only one is given), and `ICL_TEMPLATE_IOS_BUILD_NUMBER`
sets `CFBundleVersion`. The identifier is `ICL_TEMPLATE_APPLICATION_ID` in `CMakeLists.txt`. Check the bundle with
`apps/ios/packaging/verify_app.sh <app> --bundle-id <id> --executable icl-template`.

Put personal settings in `CMakeUserPresets.json`, which is ignored by git.
