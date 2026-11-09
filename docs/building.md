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
| `ios-simulator` | The same for the iOS simulator. Not working with the aqtinstall kit and not in CI: the link fails because the kit's `qmltooling` plugin objects are built for the device only ("building for 'iOS-simulator', but linking in object file ... built for 'iOS'") |
| `android-release` | The Android application, `arm64-v8a`, Release: an unsigned APK (build preset `android-release`) and bundle (`android-release-bundle`), on Linux or Windows; no tests |
| `android-emulator` | The same for an `x86_64` emulator, RelWithDebInfo, APK only; no tests |
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

#### Reading the timings on a device

The exit-gate figures (cold start to a usable board, tap to haptic) are read from the application itself, on a TestFlight
build: there is no Mac, so no Instruments. Open the Settings tab, scroll to the end, open About CS Bingo Board, and tap the Version row
five times quickly (five more hide it) to see `launch 812 ms; tap 41 ms (last of 12), median 38 ms, worst 77 ms`. The tap figure stops at the haptic call
returning and does not include the actuator. The same launch figure goes to the system log as a `launch:` line, and the
marks go to Points of Interest, for anyone with a Mac and Console or Instruments.

`ERROR_JOURNALING_IOS_SIGNING_IDENTITY` defaults to `Apple Distribution`, the release workflow's identity; a development
identity is only needed to run from Xcode.

`ej_dataset --out <directory>` writes a year of fictional data (about 80,000 entries) through the real repository. There is
no way yet to put it on a phone without a Mac; see `docs/plan/stage-5.md`, "Timing marks".

`ej_launch_image` draws the launch image of the iPhone app: the skeleton of the Log screen
(`apps/app/qml/screens/launch/LaunchSkeleton.qml`) at 402 x 874 pt and 3x, in light and dark, into
`apps/app/packaging/ios/Assets.xcassets/LaunchBoard.imageset`. Run it (no arguments) after a change to the Log screen's
layout, the tab bar or the header card's shapes, and commit the two PNGs; `ctest -R launch.` fails until you do, and so does
CI. `ej_launch_image --check` compares without writing. iOS caches launch screens: delete the app from the phone before
judging a new image.

### Android

`cmake --preset android-release` needs:

- a vcpkg checkout at the baseline of `vcpkg.json` (`VCPKG_ROOT`);
- Qt's Android kit for the ABI (`QT_ANDROID_DIR`, for example `C:/Repos/common/qt/6.12.0/android_arm64_v8a`;
  `QT_ANDROID_X86_64_DIR` for `android-emulator`);
- a desktop Qt of the same version for the host tools (`QT_HOST_PATH`, for example `C:/Repos/common/qt/6.12.0/msvc2022_64`);
- the Android SDK (`ANDROID_SDK_ROOT`) with `platforms;android-37.0` and `build-tools;37.0.0`;
- the NDK Qt 6.12.0 was built with, `27.2.12479018` (r27c), as `ANDROID_NDK_ROOT` and `ANDROID_NDK_HOME` (vcpkg's
  Android triplets read the second);
- JDK 21 (`JAVA_HOME`).

Write the paths with forward slashes, also on Windows: Qt's CMake reads `QT_HOST_PATH` into a string where a backslash
is an escape. On the developer machine the SDK, the NDK, JDK 21, the emulator and the `x86_64` kit are under `E:/android/`
(`E:/android/sdk`, `E:/android/jdk-21.0.13.0.2+1`, `E:/android/qt/6.12.0/android_x86_64`). The SDK in
`C:/Repos/common/android-sdk` has NDK 28 and is not used here.

`cmake --build --preset android-release` runs Qt's `androiddeployqt` and Gradle and writes the unsigned APK, and
`cmake --build --preset android-release-bundle` then writes the bundle (one after the other: the two share one Gradle
directory). Both are under `build/android-release/apps/app/android-build/build/outputs/`. The package is
`com.iainchesworthlabs.errorjournaling` (`ERROR_JOURNALING_APPLICATION_ID`), API 28 to 36 (`apps/app/packaging/android.cmake`).
`ERROR_JOURNALING_ANDROID_VERSION_CODE` sets `android:versionCode`; empty, it is `MAJOR * 10000 + MINOR * 100 + PATCH`
of the project version. Check a package with
`python3 tools/ci/check_android_package.py --sdk "$ANDROID_SDK_ROOT" --ndk "$ANDROID_NDK_ROOT" <apk or aab>`.
Until Stage 9's packages 9.2 to 9.6 add `libs/ios_bridge/src/platform/android/`, the Android build uses the desktop stubs
of the bridge: no share sheet, haptics or document picker (docs/plan/stage-9.md).

### The About page: commit, date and licences

`apps/app` runs two small Python programs on every build (`cmake/EjAbout.cmake`; the build needs a Python 3 on `PATH`,
found as `python3` or `python`). Neither adds a preprocessor directive or a compile definition.

- `tools/licences/build_info.py` writes `about/build_info_values.cpp` in the build directory: the short commit hash, the tag
  or branch, the build number and the date of the commit. Outside git, or without it, the values are `unknown` and the build goes on.
  The build number is `ERROR_JOURNALING_IOS_BUILD_NUMBER` (the `CFBundleVersion`; the project version when empty) on iOS, else
  `$GITHUB_RUN_NUMBER`. The date is `$SOURCE_DATE_EPOCH` when it is set (a reproducible build, or CI), else the date of the commit,
  and never the time of the build, so building one commit twice gives one program. The file changes only when a value does.
- `tools/licences/notices.py` reads the resolved link line of `ej_app` from the CMake file API and writes
  `about/third-party-notices.json` (embedded as the resource `:/about/third-party-notices.json`) and `THIRD-PARTY-NOTICES.txt`:
  Qt and what Qt bundles (from the SBOMs in `tools/licences/data/qt-modules.json`), and the vcpkg ports that are linked (from the
  installed tree). Test-only ports (`catch2`) are listed in `tools/licences/data/manifest.json` and left out. A linked library with
  no entry fails the build. `docs/licensing.md` has the design.

| Option | Default | Meaning |
|---|---|---|
| `ERROR_JOURNALING_NOTICES_STRICT` | `ON` | A linked component that the notices cannot account for fails the build. Turn it `OFF` only to get past a problem while it is being fixed; the test `about.notices` still fails. |

After a Qt upgrade run `python -I tools/licences/qt_sbom_manifest.py --sbom <platform>=<qt>/sbom ... --out tools/licences/data/qt-modules.json`
and review the diff (`docs/licensing.md`, "Maintaining it").

### Windows (developer machine)

`cmake --preset windows-msvc-release` from a Visual Studio developer prompt builds the libraries, the application and
their tests. Nothing ships from Windows.

Windows has no rpath, so a test executable finds the Qt DLLs only through `PATH`. The build does this for you:
`cmake/EjQtTestEnvironment.cmake` prepends the `bin` directory of the Qt kit that `find_package(Qt6)` found to `PATH` for
every test, both when the build discovers the tests (`ej_discover_tests`, which wraps `catch_discover_tests`) and when CTest
runs them. You do not need Qt on `PATH`. Without it the loader shows a dialog ("Qt6Qml.dll was not found") for each launch,
before `main` runs, so a new Qt test must be added with `ej_discover_tests`, or with `ej_qt_test_environment(<test>)` after
`add_test`, and not with a bare `catch_discover_tests`. On other systems both are a no-op.

Put personal settings in `CMakeUserPresets.json`, which is ignored by git.
