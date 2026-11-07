# Building

## Prerequisites

CMake 4.0 or later, Ninja, and a C++23 compiler with `std::expected` and the C++23 library. CI builds with the
floor compilers, installed from the pins in `.github/toolchain-versions.json`: GCC 16, Clang 23 (with GCC 16's
libstdc++), MSVC 14.51 (a newer toolset passes) and the newest Apple Clang the macOS runners offer (Xcode 26.6 on
`macos-26`, with Xcode 27 tried in a leg that cannot fail the run). Older compilers may work but are not tested.

## Dependencies

Dependencies come from vcpkg, using the `iainchesworthlabs/vcpkg` fork. The build finds the fork, and Qt for a
project that uses it, through environment variables with the `ICL_` prefix, which are shared by every project of
the organisation; a variable with the prefix `ERROR_JOURNALING_` overrides it for this project. The presets do not
hard-code paths.

| Variable | Meaning |
|---|---|
| `ICL_VCPKG_STORE` | A directory for vcpkg checkouts. The commit named by `builtin-baseline` in `vcpkg.json` gets its own checkout in it (`<store>/<first 12 characters>`), made once and reused by every project that pins the same commit. |
| `ICL_VCPKG_MIRROR` | A clone of the fork to make those checkouts from. Default `<store>/mirror`, cloned on first use. |
| `ICL_QT_ROOT` | A prebuilt Qt kit, for example `~/Qt/6.12.0/gcc_64`. `QT_ROOT_DIR` and `QTDIR` are the fallbacks. Qt is not a vcpkg dependency; install it with aqtinstall. |
| `ERROR_JOURNALING_VCPKG_ROOT`, `ERROR_JOURNALING_QT_ROOT` | A vcpkg checkout or Qt kit used for this project only. |

A configure with none of them set stops and lists the options. Set them once in the shell profile. The
Qt version is the `qt` key of `.github/toolchain-versions.json` (6.12.0); CI installs it from there. The libraries link
QtCore and the application links Qt Quick, so every configure needs a Qt kit; CTest runs the Qt Quick tests with
`QT_QPA_PLATFORM=offscreen`.

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
| `macos-debug`, `macos-release` | The desktop program on a Mac, for iterating on the user interface; built with tests |
| `ios-release` | The iPhone and iPad application (`apps/app`), unsigned, for a device, on a Mac with Xcode; no tests |
| `ios-simulator` | The same for the iOS simulator (Release, `arm64-ios-simulator`); no tests |
| `windows-msvc-release`, `windows-msvc-asan` | The developer machine only. Windows is not a v1 platform and CI has no Windows leg |

Sanitizers, Tracy, coverage and fuzzing exclude each other. The configure step stops with a
message when two are enabled.

### iOS

`cmake --preset ios-release` (or `ios-simulator`) needs a vcpkg checkout at the baseline of `vcpkg.json` (`VCPKG_ROOT`;
CI's `setup-vcpkg` action sets it), Xcode, Qt's iOS kit (`QT_ROOT_DIR`, for example `~/Qt/6.12.0/ios`) and a macOS Qt
of the same version for the host tools (`QT_HOST_PATH`, for example `~/Qt/6.12.0/macos`). aqtinstall installs both:
`aqt install-qt mac ios 6.12.0 --autodesktop`. It builds `error-journaling.app` under `build/ios-release` without
signing. Three cache variables make a signed
build, which is the release workflow's (`docs/releasing.md`, "iOS and TestFlight"): `ERROR_JOURNALING_IOS_SIGNING_TEAM` and
`ERROR_JOURNALING_IOS_SIGNING_PROFILE` go together (CMake stops when only one is given), and `ERROR_JOURNALING_IOS_BUILD_NUMBER`
sets `CFBundleVersion`. The identifier is `ERROR_JOURNALING_APPLICATION_ID` in `CMakeLists.txt`. Check the bundle with
`apps/app/packaging/ios/verify_app.sh <app> --bundle-id <id> --executable error-journaling`.

### Windows (developer machine)

`cmake --preset windows-msvc-release` from a Visual Studio developer prompt builds the libraries, the application and
their tests. Put the Qt `bin` directory on `PATH` first (for example `C:\repos\common\Qt\6.12.0\msvc2022_64\bin`):
the test executables load the Qt libraries when CTest discovers the tests. Nothing ships from Windows.

Put personal settings in `CMakeUserPresets.json`, which is ignored by git.
