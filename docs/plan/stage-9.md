# Stage 9: Android and Google Play

Task breakdown, test list, device checklist and owner actions for Stage 9 of the
[implementation plan](../implementation-plan.md) ("Stages 7 to 12"). Each work package is one pull request into
`develop`. Requirement IDs refer to the [PRD](../prd.md). Background: issue #106.

**Exit gate:** the app installs from the Google Play internal-testing track on a real Android phone, and the
[device checklist](#device-checklist-the-exit-gate) passes on that phone.

**Status:** draft for owner review. No code is written until the owner approves this page.

The plan follows Stage 5's approach. The Java and JNI code stays thin. Every decision goes in portable C++ that CI tests
on Linux. Anything only a phone can confirm goes in [Assumptions](#assumptions-only-a-device-can-confirm), each with a
fallback. Nothing here has run on an Android device yet. Statements about how Android behaves come from Android's
documentation, and statements about Qt come from the Qt 6.12 kit on this machine and Qt's documentation.

## What the stage covers

| Area | PRD | Section | Work package |
|---|---|---|---|
| Android build, presets, storage platform files | NFR-08, DS-04 | [Builds](#builds-and-toolchain) | 9.1 |
| Share (Android share intent) | REV-05, DAY-01, PRF-08, NFR-09 | [Share](#share) | 9.2 |
| Haptics | BRD-03, NFR-03, NOTE-01 | [Haptics](#haptics) | 9.3 |
| File picker and open-with (intent filters) | PRF-08, PRF-09, NFR-10 | [File picker and open-with](#file-picker-and-open-with) | 9.4 |
| File protection, storage location, backup | NFR-09, NFR-08, DS-04 | [File protection and storage](#file-protection-and-storage-location) | 9.5 |
| Lifecycle, timing marks, accessibility settings | NOTE-03, DAY-01, NFR-03, NFR-04, NFR-07, NFR-08 | [Lifecycle](#lifecycle), [Timing marks](#timing-marks), [Accessibility](#accessibility-settings) | 9.6 |
| Launch screen, edge-to-edge, keyboard, back, orientation | NFR-04, NFR-05, NFR-06 | [Window](#launch-screen-insets-keyboard-and-back) | 9.7 |
| Emulator smoke test | NFR-04 | [CI](#ci-jobs) | 9.8 |
| Signing and the release workflow | — | [Release](#signing-and-release) | 9.9 |
| Exit gate | all of the above | [Device checklist](#device-checklist-the-exit-gate) | 9.10 |

Out of scope: the public store listing, screenshots, data-safety form and production release (Stage 11); the tablet
layout (Stage 8 designs it for the iPad, and [decision 2](#decisions-for-the-owner) asks whether Android tablets get it);
Android TV, Wear OS, Chromebooks and the desktop build (Stage 12).

## Scope and conventions

- **Where the code goes.** `ej_ios_bridge` and `ej_storage` already keep one directory per platform under
  `src/platform/`. Android gets `libs/ios_bridge/src/platform/android/` and `libs/storage/src/platform/android/`, both
  `.cpp` files using JNI through Qt's `QJniObject` and `QNativeInterface::QAndroidApplication`. There is no `#if` on a
  platform anywhere (`AGENTS.md`). CMake chooses the directory.
- **Java.** Android delivers activity results, intents and configuration changes to Java. A small Java layer receives
  them and calls into C++. Gradle reads Java only from the Android package source directory, so the Java lives in
  `apps/app/packaging/android/src/com/iainchesworthlabs/errorjournaling/`, next to `AndroidManifest.xml` and `res/`.
  That is the same place iOS keeps its `Info.plist` and asset catalogue. It holds three classes: `EjActivity`, which
  extends `QtActivity`; `EjShare`; and `EjDocuments`. Each method is a few lines that forward to a `native` method.
- **The library name.** `ej_ios_bridge` becomes the bridge for both phones. Renaming it touches every target and the
  tests, so it is not part of this stage. The rename is proposed as a follow-up.
- **The pairing check.** `tools/ci/check_platform_layout.py` pairs `ios/` with `desktop/` today. 9.1 extends it so that
  every file in `ios/` also exists in `android/` (under its `.cpp` stem) and the other way round. Its tests in
  `tools/tests/` get an Android case.
- **QML.** `qml_files.cmake` chooses `qml/platform/ios/` or `qml/platform/desktop/`. The iOS `FileChooser.qml` answers from
  C++ (`SetupFiles`) and asks no platform questions, so Android uses it too: `qml_files.cmake` maps `android` to the
  phone directory. The file is not copied.
- **New files** get the SPDX header. That means XML comments in the manifest and resources, and `//` comments in Java.
  `REUSE.toml` covers the binary resources, such as the icons.

## What is already built

- Stage 5 interfaces with an iOS implementation and a desktop stub: `Share`, `Haptics`, `DocumentPicker`,
  `BackgroundTasks`, `AccessibilitySettings` and `PerfMarks` (`libs/ios_bridge/include/error_journaling/`), plus
  `FileProtection` (`libs/storage/src/platform/file_protection.hpp`).
- Portable logic that Android reuses unchanged: `SharePresenter`, `HapticPolicy`, `LifecycleCoordinator`,
  `LifecycleAdapter` (from Qt's application states), `OpenFileFilter` (the queue for `QFileOpenEvent`s that arrive before
  the app is ready), `ConfigImporter`, `ConfigExporter`, `protect_store` and `purge_import_leftovers`.
- QML that already reads what it needs from Qt: `SafeFrame` reads `SafeArea.margins`, `Keyboard` reads
  `Qt.inputMethod.keyboardRectangle`, and the window has `Qt.ExpandedClientAreaHint`.
- `IclPlatform.cmake` already knows `ICL_PLATFORM=android`. `ej_storage` uses `icl_platform_sources`, which fails the
  configure step when `src/platform/android/` is missing. That is the first thing 9.1 fixes.
- The Qt 6.12.0 Android kit for `arm64-v8a` is installed at `C:\Repos\common\qt\6.12.0\android_arm64_v8a`. It is built
  with shared libraries and 16 KB pages, on NDK `27.2.12479018` (r27c), with a minimum of `android-28` and Java compiled
  against API 37.

## Builds and toolchain

| Item | Value | Why |
|---|---|---|
| Qt | 6.12.0 Android kit, from `.github/toolchain-versions.json` | One Qt version for every platform |
| Minimum SDK | 28 (Android 9) | Qt 6.12 supports API 28 to 36 (Qt for Android, "Supported Configurations") |
| Target SDK | 36 (Android 16) | Google Play: "Starting August 31 2026: New apps and app updates must target Android 16 (API level 36) or higher" |
| Compile SDK | 37 | The Java in the Qt kit is compiled against API 37 (`modules/Core.json`). Qt's default is kept |
| NDK | r27c, `27.2.12479018` | The version Qt built the kit with. Qt recommends matching it |
| JDK | 21 | Qt 6.12 lists JDK 21. The JDK 25 on this machine is not used for Android |
| Gradle, AGP | 9.5.1, 9.2.1 | What the kit's templates name |
| ABIs | `arm64-v8a` in the release bundle; `x86_64` only for the emulator | [Decision 3](#decisions-for-the-owner) |
| vcpkg triplets | `arm64-android`, `x64-android` | vcpkg's own triplets. The manifest already leaves Catch2 out on Android, so vcpkg installs nothing for a release build. Tracy is installed only with the `profiling` feature |
| Package | Android App Bundle (`.aab`) for Play; APK for CI and the emulator | Play takes bundles |
| Application ID | `com.iainchesworthlabs.errorjournaling` | The iOS bundle identifier. It cannot be changed after the first Play upload ([decision 1](#decisions-for-the-owner)) |
| Page size | 16 KB ELF and zip alignment | Play requires it for targets of API 35 and up, and refuses updates without it from 1 February 2027. NDK r27 needs `-Wl,-z,max-page-size=16384`; the kit is built with `android_16kb_pages`, and CI checks the result |

**Presets.** `android-base` hides the shared settings. Like `ios-base`, it uses vcpkg's toolchain file with
`VCPKG_CHAINLOAD_TOOLCHAIN_FILE` set to the Qt kit's `qt.toolchain.cmake`, plus `QT_HOST_PATH`, `ANDROID_SDK_ROOT` and
`ANDROID_NDK_ROOT`. Two presets build on it:

- `android-release`: `arm64-v8a`, Release, an unsigned `.aab` and `.apk`;
- `android-emulator`: `x86_64`, RelWithDebInfo, an APK for the smoke test.

No tests are built in either preset. `docs/building.md` gets an Android section, which `AGENTS.md` requires for a new
preset.

**Packaging** (`apps/app/packaging/android.cmake`, mirroring `ios.cmake`) sets:
- `QT_ANDROID_PACKAGE_SOURCE_DIR`, `QT_ANDROID_MIN_SDK_VERSION`, `QT_ANDROID_TARGET_SDK_VERSION` and
  `QT_ANDROID_VERSION_NAME`;
- `QT_ANDROID_VERSION_CODE`, from `ERROR_JOURNALING_ANDROID_VERSION_CODE`. The release workflow passes the run number,
  as it does for the iOS build number, and Play refuses a code it has seen before.

`apps/app/packaging/android/verify_package.sh` is the counterpart of `verify_app.sh`. It reads the merged manifest out of
the built package and fails the build when any of these is wrong:
- the package declares any `<uses-permission>` (NFR-09: no network, so no `INTERNET`);
- `minSdkVersion` is not 28 or `targetSdkVersion` is not 36;
- the application ID, the `exported` flags or the intent filters differ from the ones expected;
- `allowBackup` is not the decided value;
- any native library is not 16 KB aligned (`llvm-readelf` LOAD alignment and the RELRO end, and `zipalign -c -P 16 4`).

**C++23 on the NDK.** The NDK r27c compiler is Clang 17 with its libc++. 9.1 compiles every library for Android first,
before any platform code is added, and lists any standard-library feature the code uses that this libc++ lacks. If
there are gaps, each one gets a portable replacement in `libs/core`, or Qt is upgraded when a later kit moves to a newer
NDK. The only option ruled out is a preprocessor switch.

## Platform layer

Each interface below is a Stage 5 interface with an iOS implementation and a desktop stub. "Portable" means code that CI
tests on Linux. "Glue" means the Android-only `.cpp` and Java code, which only the emulator and the phone exercise.

### Share

**Android:** `Intent.ACTION_SEND` in `Intent.createChooser(target, title, intentSender)`. Text goes in `EXTRA_TEXT` with
the type `text/plain`. A file goes in `EXTRA_STREAM` as a `content://` URI from a `FileProvider` (authority
`<application id>.files`, path `cache/share/`) with `FLAG_GRANT_READ_URI_PERMISSION`. A `file://` URI is refused from
API 24.

**Outcome.** Android does not report whether a share completed. The chooser's `IntentSender` broadcasts
`EXTRA_CHOSEN_COMPONENT` when the person picks a target.

- A target was chosen: `SHARED`. `m_ActivityType` is the chosen package name. It is logged as iOS logs the activity type
  (the target app, never the content).
- The chooser returned with no choice: `CANCELLED`.
- `startActivity` threw: `FAILED`, with the exception class as `m_Detail`.

The rule that turns (result arrived, component chosen, exception) into a `ShareOutcome` is portable:
`android_share_outcome()` in `src/share_outcome.cpp`.

**The temporary file.** iOS deletes `tmp/<name>.ejconfig` on every outcome. On Android, the receiving app may read the
URI after the chooser has closed; for example, a mail draft attaches the file when it is sent. So the file stays until
the next export starts or the next launch, when `purge_import_leftovers` clears `cache/share/`. This differs from iOS, and
the owner accepts it under [decision 6](#decisions-for-the-owner).

**Size.** `EXTRA_TEXT` travels in a Binder transaction, which Android limits to about 1 MB for the whole process. Above a
portable threshold of 256 KB, measured in UTF-8 bytes, the export is shared as a `.txt` file instead. The largest golden
day in `libs/core/tests` sets the test case.

**Tests**
- `unit` `share.android_outcome`: every combination of result, chosen component and exception maps to one outcome.
  `done` is called exactly once, including when the broadcast arrives after the result.
- `unit` `share.text_or_file`: the text is shared inline at the threshold and as a file 1 byte above it.
- `integration` `share.android_cache_purge`: files left in a fake `cache/share/` are removed at launch, and the
  directory itself is kept.
- On the emulator, a text share opens the chooser without an exception (9.8). On the phone, checklist S1 to S4.

### Haptics

**Android:** `View.performHapticFeedback` on the activity's content view, run on the Android UI thread through
`QAndroidApplication::runOnAndroidMainThread`:
- `IMPACT` uses `HapticFeedbackConstants.VIRTUAL_KEY`;
- `SUCCESS` uses `CONFIRM` on API 30 and up, and `VIRTUAL_KEY` below it.

`performHapticFeedback` needs no `VIBRATE` permission, so the manifest stays without permissions. It follows the
system's "Touch feedback" setting, which is the Android counterpart of the iOS system haptics switch. `prepare()` does
nothing: Android has no generator to warm up.

**Timing.** The call is posted to the Android UI thread and returns at once. So `TAP_TO_HAPTIC` measures up to the post,
not to the motor. The phone check H2 judges the latency by feel against the iPhone, and logcat shows the time from the
post to the run.

**Tests**
- `unit` `haptics.android_constants`: the portable table from `HapticKind` and API level to a constant.
- `HapticPolicy`'s existing tests already cover when it fires. On the phone, checklist H1 and H2.

### File picker and open-with

**Picker** (Settings > Import, and the first-launch offer): `Intent.ACTION_OPEN_DOCUMENT` with `CATEGORY_OPENABLE`, the
type `*/*`, and `EXTRA_MIME_TYPES` of `application/json`, `application/octet-stream` and `text/plain`. A `.ejconfig`
file has no registered MIME type, and document providers report it as `application/octet-stream`.

`EjDocuments` receives the result. It copies the stream through `ContentResolver.openInputStream` into
`cache/import/<random>.ejconfig` and stops at the size limit `ConfigImporter` already enforces. Then it calls
`done(PICKED, path)`. The content is checked as on iOS: `ConfigImporter` validates it, and the extension is not trusted.

**Open-with.** `EjActivity` is `launchMode="singleTop"` and has two `exported` intent filters:
- `ACTION_VIEW` with the scheme `content` and the MIME types `application/json` and `application/octet-stream`;
- `ACTION_SEND` with the same MIME types, for "Share to" from Slack, Gmail and Drive.

There is no `*/*` filter, so the app does not show up for every file. In `onCreate` and `onNewIntent` it copies the URI
the same way and calls a native method. That method posts a `QFileOpenEvent` with the local path to `qApp`. From there,
iOS's path is unchanged: `OpenFileFilter` queues an event that arrived before `ready()`, which covers a cold launch from
another app.

**Tests**
- `unit` `open_file.android_copy_plan`: the portable part decides the target name and the size cut-off, and rejects a
  missing or empty stream. The Java only moves bytes.
- The existing `open_file.queue` and `importer.*` tests cover the queue and the import.
- `system` on the emulator, if `adb` can grant the URI (9.8): push `config/sample-config.json` as `sample.ejconfig`, then
  `am start -a android.intent.action.VIEW -t application/octet-stream --grant-read-uri-permission -d <content URI>`.
  The import confirmation appears in the log.
- On the phone, checklist F1 to F5.

### File protection and storage location

**Storage location.** `QStandardPaths::AppDataLocation` on Android is the app's internal files directory
(`Context.getFilesDir()`, `/data/user/0/<id>/files`). `TempLocation` is its cache directory. Both are private to the
app, need no storage permission and are removed when the app is uninstalled. `main.cpp` already uses these two, so the
path is unchanged.

`DocumentsLocation` is app-specific external storage. Only `purge_import_leftovers` reads it, and only to clear iOS's
`Inbox`, which is harmless here. The Android copies go under `cache/import/` and `cache/share/`, and the same purge clears
both.

**Protection.** Android encrypts internal storage per file. By default, an app's files are in credential-encrypted
storage, which can be read only after the user first unlocks the phone following a boot. That is the same guarantee as
iOS's `NSFileProtectionCompleteUntilFirstUserAuthentication`, and it applies to every file without a call.

`libs/storage/src/platform/android/file_protection.cpp`:
- `apply()` succeeds and changes nothing;
- `read_back()` returns `COMPLETE_UNTIL_FIRST_USER_AUTHENTICATION` for a path under the files or cache directory, and
  `OTHER` for any other path.

The app does not opt into direct-boot storage (`android:directBootAware` stays false), so it never runs before the first
unlock. Like iOS, it can never meet a locked store at launch. `protect_store`'s log line then reads, for example,
`protection: 14 files checked, all complete-until-first-auth` on both phones.

**The other storage files.** `durable_sync.cpp` and `file_lock.cpp` in `android/` use the same POSIX calls as the Linux
ones, because Bionic is a Linux libc: `fdatasync` and the directory `fsync`, then `flock`. The kill tests'
`process_containment.cpp` and `unreadable_file.cpp` get Android versions so the tree configures. They are only built
when tests are built, which the Android presets do not do.

**Backup.** Android Auto Backup copies the files directory to the user's Google account, and device-to-device transfer
copies it to a new phone. It is on by default. Stage 2 decision 8 keeps the iOS store in the device backup, so the
proposal is the same for Android:
- `allowBackup="true"`, with `dataExtractionRules` and `fullBackupContent` that include `files/` and exclude `cache/`;
- backup is not end-to-end encrypted unless the phone has a screen lock; that is stated in the Stage 10 FAQ.

A backup can capture the store mid-write. Restoring it is then the same case as a kill, which recovery already handles.
Auto Backup stops at 25 MB per app. [Decision 5](#decisions-for-the-owner) asks the owner to confirm this.

**Tests**
- `unit` `protect.android`: `read_back` inside and outside the app directories; `apply` never fails.
- The existing `protect.plan`, `protect.sweep` and `protect.warning` tests run unchanged against a fake.
- `verify_package.sh` checks `allowBackup` and the rules file.
- On the phone, checklist P1 to P3: the log line, and a reboot followed by opening the app before unlocking is not
  possible.

### Lifecycle

**Android:** `LifecycleAdapter` already maps `Qt::ApplicationState`. Qt's Android plugin reports `ApplicationInactive`
on `onPause` and `ApplicationSuspended` on `onStop` (assumption L1). So the flush on pause, the drain and the day check
on resume are the portable `LifecycleCoordinator` with no Android code.

`BackgroundTasks` has no Android counterpart. An app that has stopped keeps running until the system needs the memory,
so Android uses the desktop implementation: tokens and nothing else. That is one line in CMake, not a copy. The flush
runs on `ApplicationInactive`, which is the earliest signal, and recovery is the backstop for a process the system
kills.

**Tests**
- The existing `lifecycle.*` tests.
- `system` `app.android_pause_resume` on the emulator (9.8): `input keyevent KEYCODE_HOME`, then `am start` again. The
  log shows `lifecycle: inactive`, `flush`, `lifecycle: active` and `day check`.
- On the phone, checklist L1 to L4, including `am kill` while in the background.

### Timing marks

`PerfMarks` gets an Android implementation:
- `begin` and `end` use `ATrace_beginSection` and `ATrace_endSection` (NDK `android/trace.h`), which Perfetto shows;
- `log` uses `__android_log_print` with the tag `ej`;
- `uptime_ms` is `SystemClock.uptimeMillis()`, the clock of `MotionEvent.getEventTime()`, which Qt's touch timestamps
  come from (assumption T1);
- `ms_since_process_start` is `SystemClock.elapsedRealtime() - Process.getStartElapsedRealtime()`.

The timing readout in Settings then works as on iOS.

### Accessibility settings

`AccessibilitySettings` on Android:
- **text scale:** `Configuration.fontScale`, clamped to the iOS table's range of 0.82 to 3.12;
- **reduce motion:** `Settings.Global.ANIMATOR_DURATION_SCALE == 0`, which is what "Remove animations" sets;
- **screen reader:** `AccessibilityManager.isTouchExplorationEnabled()`, which reports TalkBack;
- **changes:** `EjActivity.onConfigurationChanged` covers the font scale, a `ContentObserver` covers the animator scale,
  and `TouchExplorationStateChangeListener` covers TalkBack.

Qt must not also scale the fonts, or the text would be scaled twice (assumption A1).

**Tests**
- `unit` `accessibility.android_scale`: the portable clamp and rounding.
- On the phone, checklist X1 to X3, with TalkBack reading each tile as VoiceOver does.

### Launch screen, insets, keyboard and back

**Launch screen.** Android 12 and later always show the system splash: the app icon centred on a background colour,
held until the first frame. An app cannot replace it with a full picture, so the iOS board-skeleton image is not used.
The plan:
- `windowSplashScreenBackground` is the board background token, light in `values/` and dark in `values-night/`;
- `windowSplashScreenAnimatedIcon` is the adaptive icon's foreground;
- on API 28 to 30, which have no splash API, the same colour and centred icon come from the theme's `windowBackground`.

That is the Android convention ([decision 4](#decisions-for-the-owner)). The adaptive icon needs a foreground, a
background and a monochrome layer (Android 13 themed icons). They are made from the existing 1024 px icon by a script,
and the owner approves the result.

**Edge-to-edge.** It is enforced for target 36 and cannot be turned off. The window already has
`Qt.ExpandedClientAreaHint`, and `SafeFrame` reads `SafeArea.margins`. Qt 6.12 fills these from Android's system-bar,
display-cutout and gesture insets (assumption W1). Status-bar and navigation-bar icons must be dark on the light theme
and light on the dark one. If Qt does not follow the colour scheme (assumption W2), `EjActivity` sets
`WindowInsetsController.setSystemBarsAppearance` from `Configuration.uiMode`, which needs no new C++ interface.

**Keyboard.** `windowSoftInputMode="adjustNothing"`, so the window neither pans nor resizes. `Keyboard.qml`'s
`coverage()` moves the focused field, as it does on iOS. Qt reports `keyboardRectangle` from the IME inset (assumption
K1). `Keyboard.qml`'s comment about iOS's rectangle mid-transition gets an Android paragraph once K1 is measured.

**Back.** The system back gesture or button must close the top sheet, then go back one screen, and on the root screen
leave the app, which Android sends to the background. It must never lose a draft: the draft is flushed on pause anyway.

With target 36, Android 16 uses predictive back and no longer dispatches `KEYCODE_BACK`. Qt 6.12 turns
`OnBackInvokedCallback` into `Qt::Key_Back` (assumption B1). The shell and `Sheet` handle `Key_Back` as they handle
Escape. That is a key event, not a platform question, so `check_qml_platform.py` allows it. The fallback is
`android:enableOnBackInvokedCallback="false"`.

**Orientation.** The activity is `screenOrientation="portrait"`. For apps that target API 36, Android 16 ignores that on
displays whose smallest width is at least 600 dp: tablets and unfolded foldables. An opt-out exists, but Android says it
"won't apply when targeting API level 37". [Decision 2](#decisions-for-the-owner) asks what v1 does on those screens.

**Tests**
- `ui` `launch.android_colours`: the splash colours in `values/` and `values-night/` equal the tokens in
  `docs/design/tokens.json`. This is a script test.
- `ui` `shell.back_key`: `Key_Back` closes an open sheet, then pops the stack, then is not accepted on the root screen.
  It runs offscreen on desktop.
- On the phone, checklist W1 to W5, K1 to K3 and B1 to B3.

## Assumptions only a device can confirm

| # | Assumption | How it is checked | If it is wrong |
|---|---|---|---|
| L1 | Qt's Android plugin reports `ApplicationInactive` on `onPause` and `ApplicationSuspended` on `onStop` | L1: the lifecycle log lines on the emulator and on the phone | `EjActivity.onPause` calls the coordinator directly through a native method |
| T1 | Qt's touch event timestamps are `MotionEvent.getEventTime()` (uptime ms) | T1: `TOUCH_AGE` stays under 50 ms and is never negative | `TOUCH_AGE` is left out on Android and `TAP_TO_HAPTIC` is used alone |
| A1 | Qt does not apply `fontScale` to QML font sizes itself | X1: at 200% text, the header line is 2x the default, not 4x | The Android `read()` reports a scale of 1.0 and leaves the scaling to Qt |
| W1 | `SafeArea.margins` includes the status bar, the cutout and the gesture bar | W1: no control is under the gesture bar on a gesture-navigation phone | `EjActivity` sends `WindowInsets` to `SafeFrame` through the bridge |
| W2 | Qt sets light or dark system-bar icons from the colour scheme | W2: the status bar is readable in both themes | `EjActivity` sets the appearance itself (above) |
| K1 | `keyboardRectangle` gives the IME inset in window coordinates, and is final when the animation ends | K1: the note field stays above the keyboard on Gboard and on Samsung Keyboard | `EjActivity` sends `WindowInsets.Type.ime()` to `Keyboard` as the override |
| B1 | Qt 6.12 delivers predictive back as `Qt::Key_Back` | B1: back closes a sheet on Android 16 | `enableOnBackInvokedCallback="false"` restores `KEYCODE_BACK` |
| S1 | The chooser's `IntentSender` fires for a choice, including the chooser's own Copy | S1 and S2 | Copy is reported as `CANCELLED`; the toast still confirms, and only the log differs |
| F1 | Slack, Gmail and Drive's "Open with" send `ACTION_VIEW` with `application/octet-stream` or `application/json` | F2 to F4 | Add the MIME type the phone reports, measured, not guessed |

## CI jobs

The new jobs are in `.github/workflows/android-build.yml`, triggered the way `ios-build.yml` is: on a pull request that
touches the Android paths, and on every push to `develop`. `.github/ci/plan.json` and `docs/ci.md` are updated, and
every `uses:` is pinned to a full SHA.

| Job | Runner | What it does | When |
|---|---|---|---|
| `android-build` | `ubuntu-24.04` | Configures and builds `android-release` (unsigned `.aab` and `.apk`), runs `verify_package.sh` and uploads the APK | PR on Android paths; push to `develop` |
| `android-emulator` | `ubuntu-24.04` with KVM | Builds `android-emulator` and boots an API 36 `x86_64` emulator. It installs the app, starts it with `-e applicationArguments --smoke-test` and waits for the existing first-frame exit with a timeout. Then the pause/resume check and, if feasible, the `VIEW` intent import. It uploads logcat | Push to `develop`; manual; PR only with the `android-emulator` label |
| Linux tests (existing) | — | All the portable logic above, with ASan, UBSan and TSan | Every PR |

The emulator uses `reactivecircus/android-emulator-runner`, pinned by SHA. If KVM is missing or the job is flaky, it is
non-blocking for its first two weeks and its pass rate is recorded in `docs/ci.md`.

Unit tests do not run on the device. Catch2 is left out on Android by design. Everything worth unit-testing is portable
and runs on Linux, and the glue is exercised by the emulator and the phone. Qt's `androidtestrunner` could run QtTest
executables on the emulator. It is left for later, because the smoke test covers the glue for less CI time.

**Shared jobs.** The build steps that are not specific to this app go to `icl-common-infra` as a reusable
`actions/setup-android`, in its own PR there:
- install JDK 21;
- install the SDK packages: `platforms;android-37`, `build-tools;37.0.0` and `ndk;27.2.12479018`;
- install the Qt Android kit and the Linux host kit;
- restore the Gradle cache.

The upload to Play goes there as `actions/play-upload` (see [Release](#signing-and-release)). This repository then
re-pins with `tools/ci/repin.py`. `cmake/icl/` is not edited.

**Actions cache budget.** The 10 GB budget in `docs/ci.md` already carries about 2.4 GB per `develop` generation, with
an 8 GB ceiling. These estimates are measured in 9.1 and 9.8 and recorded in the budget table:

| Entry | Estimate | Note |
|---|---|---|
| Qt Android kit, `arm64-v8a` | ~200 MB compressed (472 MB on disk here) | One entry per Qt version. The Linux host kit is the existing `setup-qt` entry |
| Qt Android kit, `x86_64` | ~200 MB | Emulator job only |
| Gradle (`~/.gradle/caches`, wrapper) | ~300 MB | Saved from `develop` only, per the existing write policy |
| ccache, two Android legs | ~250 MB each | One lineage per leg, as now |
| NDK, SDK, emulator image | not cached | Installed by `sdkmanager` each run, about 1.5 GB of download outside the cache. If the runner image already has NDK 27.2.12479018, it is used |

That adds about 1.2 GB per generation, for 3.6 GB. Three pushes in an hour then peak at about 7.2 GB, under the 8 GB
ceiling, but it uses most of the headroom kept for a Qt upgrade. If the measured figure is higher, the `x86_64` kit and
its ccache are saved only by the manual emulator run.

## Signing and release

**Keys.** Google Play App Signing is required for new apps.
- Google holds the **app signing key**, which signs what users install.
- The owner holds an **upload key**, which signs what CI uploads.
- A lost upload key can be reset through Play support. A lost app signing key cannot be replaced, which is why Google
  holds it.

The owner creates the upload key on their own machine (owner action O4), for example:

```bash
keytool -genkeypair -v -keystore upload.jks -alias upload -keyalg RSA -keysize 4096 -validity 10000
```

**Release workflow.** `release.yml` gets an `android` job beside the `ios` job, on the same `v*` tag and the same manual
dispatch:
1. Check out, then set up Android and Qt (the shared action).
2. Configure `android-release` with `-DERROR_JOURNALING_ANDROID_VERSION_CODE=${{ github.run_number }}`.
3. Build the `.aab`.
4. Write the keystore from `ANDROID_UPLOAD_KEYSTORE_BASE64` into `$RUNNER_TEMP` and sign with `jarsigner` (or Qt's
   `androiddeployqt --sign`). Delete the keystore in an `if: always()` step.
5. Run `verify_package.sh` on the signed bundle, plus `bundletool validate` and the 16 KB alignment check.
6. Upload to the internal-testing track with `actions/play-upload`, only when the `upload_play` input is set or a tag
   is pushed. Otherwise just validate, as the iOS job does with TestFlight.
7. Attach the `.aab` as a workflow artifact, never to the GitHub release (it is signed with the upload key, not for
   users).
8. Print how a tester installs it.

`actions/play-upload` is a short Python script that calls the Play Developer API:
`edits.insert`, `edits.bundles.upload`, `edits.tracks.update` (track `internal`, status `completed`) and `edits.commit`.
It is a script, not a third-party action, so no extra publisher holds a credential.

**Authentication.** The preferred route is GitHub OIDC with Google Cloud Workload Identity Federation
(`google-github-actions/auth`, pinned). It stores no long-lived key: the secret is two names, not a key. The alternative
is a service-account JSON key in `PLAY_SERVICE_ACCOUNT_JSON`. The owner chooses (O6).

**Limits that shape the plan**
- Play's API cannot create an app or make its first release. The owner uploads the first `.aab` by hand in Play Console
  (O5). CI uploads from the second build on.
- On a personal developer account created after November 2023, production access needs a closed test with at least 12
  testers for 14 days in a row (Play Console Help, answer 14151465). Internal testing is not affected, so the Stage 9 gate
  is not. But it is on Stage 11's critical path, so it is listed for the owner now.

**Tests**
- `unit` `android.version_code`: the CMake script refuses an empty or non-numeric code in a release configure, and
  checks that a code is higher than the previous one. It is a script test, like `ios.signing_identity`.
- `release.yml` with `upload_play` off: build, sign, verify and validate, upload nothing. This is the dry run before
  the owner's first manual upload.

## Work packages

Each package is one PR, squash auto-merged, with tests. The iOS build stays green on every one, because each package
touches only the Android directories or adds a CMake choice that iOS does not take.

| # | Package | Contents | Depends on |
|---|---|---|---|
| 9.1 | Android build skeleton | Presets, `android.cmake`, the manifest, `EjActivity` (empty), the storage `android/` files, the bridge CMake three-way choice with desktop stubs for Android, the `qml_files.cmake` mapping, the layout-check extension, `verify_package.sh`, the `android-build` job, the C++23 libc++ list, and the `docs/building.md` and `docs/ci.md` updates | Owner approval of this page |
| 9.1a | `icl-common-infra`: `setup-android` | The reusable action, then a re-pin here | 9.1 (to know the steps) |
| 9.2 | Share | `android/share.cpp`, `EjShare`, the `FileProvider`, the portable outcome and threshold | 9.1 |
| 9.3 | Haptics | `android/haptics.cpp` and the constant table | 9.1 |
| 9.4 | File picker and open-with | `android/document_picker.cpp`, `EjDocuments`, the intent filters, the native open-event hook | 9.1 |
| 9.5 | Protection, storage and backup | `file_protection.cpp`, the backup rules and the purge of `cache/` | 9.1 |
| 9.6 | Lifecycle, timing and accessibility | `perf_marks.cpp`, `accessibility_settings.cpp`, the lifecycle log check | 9.1 |
| 9.7 | Window | Splash, adaptive icon, system bars, keyboard mode, `Key_Back` in the shell and `Sheet`, orientation per decision 2 | 9.1, Stage 7 for the shell |
| 9.8 | Emulator smoke test | The `android-emulator` job and its scripts | 9.4, 9.6 |
| 9.9 | Release to internal testing | The `release.yml` job, `actions/play-upload` in `icl-common-infra`, the version code, and the `docs/releasing.md` update | 9.1 to 9.7; owner actions O1 to O6 |
| 9.10 | Exit gate | The device checklist run on the owner's phone from a Play internal-testing install, with the results recorded here | 9.9 |

Packages 9.2 to 9.6 touch separate files and can run in parallel once 9.1 has merged. A user-visible change, such as the
app being available on Android, gets a `changelog.d/` fragment in 9.9.

## Device checklist (the exit gate)

Run on the owner's Android phone (O8), installed from the Play internal-testing link, not sideloaded, in light and dark
and at the largest text size. Each line records pass or fail, the phone and the Android version.

| # | Check |
|---|---|
| I1 | Installs from the internal-testing opt-in link; the name and icon are right in the launcher and in recent apps |
| I2 | First launch: system splash in the token colour, then the setup offer, with no white flash |
| B1 to B3 | Back closes a sheet, back goes back one screen, back on the Log board backgrounds the app and keeps the draft |
| H1, H2 | A tile tap gives a haptic within the feel of the iPhone build. "Touch feedback" off in system settings silences it |
| S1 to S4 | Share the day's export to Slack and to Gmail, and the configuration export to Drive. Copy from the chooser. Dismiss the chooser: the app reports cancelled and nothing is lost |
| F1 to F5 | Import through Settings > Import from Downloads, and from Drive. "Open with" from Slack, Gmail and Files. A non-setup JSON file is refused with the existing message |
| L1 to L4 | Background and return mid-note: the draft is kept. Return after midnight: the day prompt. `adb shell am kill` while backgrounded, then reopen: recovery and nothing lost. Rotate on a phone: stays portrait |
| P1 to P3 | The protection log line. A reboot then a launch before unlocking is not possible. Backup per decision 5 (`adb shell bmgr backupnow`) |
| W1 to W5 | Gesture navigation and three-button navigation: nothing under the bars. Readable status bar in both themes. The display cutout is clear. Split screen does not lose state |
| K1 to K3 | The note field stays above Gboard and above Samsung Keyboard. The keyboard closes on save |
| X1 to X3 | Text at 200%, "Remove animations" and TalkBack on the board and a sheet |
| T1 | The timing line: cold start to a usable board under 1.5 s (NFR-04) and tap to haptic under 100 ms (NFR-03) on the phone |
| G1 | The Stage 6 acceptance checklist, run on the Android phone |

## Licence facts

This section lists what the licence texts say and what the build does. It is not legal advice. The owner will take
advice (implementation plan, "Decisions the owner must make before Stage 11").

**What the Android build links**
- The Qt 6.12.0 Android kit is built with shared libraries (`QT_CONFIG += shared` in `mkspecs/qconfig.pri`). The app
  links Qt dynamically on Android. Qt's `.so` files (`libQt6Core_arm64-v8a.so` and others) go inside the APK or bundle
  next to the app's own `.so`, and Qt's Java launcher loads them. A static Qt for Android is not used, and would mean
  building Qt from source.
- The kit's SBOM (`sbom/qtbase-6.12.0.spdx`, `sbom/qtdeclarative-6.12.0.spdx`) declares each module the app links as
  `LicenseRef-Qt-Commercial OR LGPL-3.0-only OR GPL-2.0-only OR GPL-3.0-only`. That covers Core, Gui, Qml, QmlModels,
  QmlMeta, QmlWorkerScript, Quick, QuickControls2, QuickTemplates2, QuickShapes, Sql, QSQLiteDriverPlugin and
  QAndroidIntegrationPlugin.
- The app itself is `GPL-3.0-or-later`. The NDK's `libc++_shared.so` is packaged too, under Apache-2.0 with the LLVM
  exception. The Qt template's Gradle file adds `androidx.core:core:1.17.0`, under Apache-2.0. `IclNotices.cmake` lists
  notices for the desktop builds, and 9.1 extends it to the Android package.
- With Play App Signing, the APKs users install are generated by Google from the bundle and signed with Google's key.
  Android installs an update only when it is signed with the same key. A user can install a package they rebuilt and
  signed themselves only by first uninstalling the Play version, which removes its data unless it was backed up.

**LGPL-3.0, section 4 "Combined Works"** (quoted from <https://www.gnu.org/licenses/lgpl-3.0.txt>):

> You may convey a Combined Work under terms of your choice that, taken together, effectively do not restrict
> modification of the portions of the Library contained in the Combined Work and reverse engineering for debugging such
> modifications, if you also do each of the following:
>
> a) Give prominent notice with each copy of the Combined Work that the Library is used in it and that the Library and
> its use are covered by this License.
>
> b) Accompany the Combined Work with a copy of the GNU GPL and this license document.
>
> c) For a Combined Work that displays copyright notices during execution, include the copyright notice for the Library
> among these notices, as well as a reference directing the user to the copies of the GNU GPL and this license document.
>
> d) Do one of the following:
>
> 0) Convey the Minimal Corresponding Source under the terms of this License, and the Corresponding Application Code in a
> form suitable for, and under terms that permit, the user to recombine or relink the Application with a modified
> version of the Linked Version to produce a modified Combined Work, in the manner specified by section 6 of the GNU GPL
> for conveying Corresponding Source.
>
> 1) Use a suitable shared library mechanism for linking with the Library. A suitable mechanism is one that (a) uses at
> run time a copy of the Library already present on the user's computer system, and (b) will operate properly with a
> modified version of the Library that is interface-compatible with the Linked Version.
>
> e) Provide Installation Information, but only if you would otherwise be required to provide such information under
> section 6 of the GNU GPL, and only to the extent that such information is necessary to install and execute a modified
> version of the Combined Work produced by recombining or relinking the Application with a modified version of the
> Linked Version. (If you use option 4d0, the Installation Information must accompany the Minimal Corresponding Source
> and Corresponding Application Code. If you use option 4d1, you must provide the Installation Information in the manner
> specified by section 6 of the GNU GPL for conveying Corresponding Source.)

**LGPL-3.0, section 0, definitions used above:**

> A "Combined Work" is a work produced by combining or linking an Application with the Library. The particular version
> of the Library with which the Combined Work was made is also called the "Linked Version".
>
> The "Minimal Corresponding Source" for a Combined Work means the Corresponding Source for the Combined Work, excluding
> any source code for portions of the Combined Work that, considered in isolation, are based on the Application, and not
> on the Linked Version.
>
> The "Corresponding Application Code" for a Combined Work means the object code and/or source code for the Application,
> including any data and utility programs needed for reproducing the Combined Work from the Application, but excluding
> the System Libraries of the Combined Work.

**LGPL-3.0, section 3** (object code with material from the library's headers, for example inline functions and
templates):

> The object code form of an Application may incorporate material from a header file that is part of the Library. You
> may convey such object code under terms of your choice, provided that, if the incorporated material is not limited to
> numerical parameters, data structure layouts and accessors, or small macros, inline functions and templates (ten or
> fewer lines in length), you do both of the following:
>
> a) Give prominent notice with each copy of the object code that the Library is used in it and that the Library and its
> use are covered by this License.
>
> b) Accompany the object code with a copy of the GNU GPL and this license document.

**GPL-3.0, section 6**, which section 4 of the LGPL refers to (quoted from <https://www.gnu.org/licenses/gpl-3.0.txt>):

> d) Convey the object code by offering access from a designated place (gratis or for a charge), and offer equivalent
> access to the Corresponding Source in the same way through the same place at no further charge. You need not require
> recipients to copy the Corresponding Source along with the object code. If the place to copy the object code is a
> network server, the Corresponding Source may be on a different server (operated by you or a third party) that supports
> equivalent copying facilities, provided you maintain clear directions next to the object code saying where to find
> the Corresponding Source. Regardless of what server hosts the Corresponding Source, you remain obligated to ensure that
> it is available for as long as needed to satisfy these requirements.

> "Installation Information" for a User Product means any methods, procedures, authorization keys, or other information
> required to install and execute modified versions of a covered work in that User Product from a modified version of
> its Corresponding Source. The information must suffice to ensure that the continued functioning of the modified
> object code is in no case prevented or interfered with solely because modification has been made.

> If you convey an object code work under this section in, or with, or specifically for use in, a User Product, and the
> conveying occurs as part of a transaction in which the right of possession and use of the User Product is transferred
> to the recipient in perpetuity or for a fixed term (regardless of how the transaction is characterized), the
> Corresponding Source conveyed under this section must be accompanied by the Installation Information. But this
> requirement does not apply if neither you nor any third party retains the ability to install modified object code on
> the User Product (for example, the work has been installed in ROM).

**Facts the adviser may want**
- The repository is public on GitHub. A tag's source archive is already a release asset (`release.yml`, "Package
  (source)").
- The build is reproducible from the repository with the presets on this page, the pinned Qt version and the pinned
  vcpkg baseline.
- The Play listing has a description field and a website field. The app's About screen shows the version, with an
  optional commit (PR #120).
- Whether the app's own GPL terms fit Google Play's terms of service is the same open question as for the App Store. It
  is the owner's to take advice on, and it is not answered here.

## Decisions for the owner

1. **Application ID.** Keep `com.iainchesworthlabs.errorjournaling`, which matches iOS. It is permanent once uploaded.
2. **Large screens.** With target API 36, Android 16 ignores the portrait lock on tablets and unfolded foldables. The
   options:
   - (a) Exclude tablets in Play Console's device catalogue for v1, and set the temporary opt-out property for foldables.
     This is the recommendation. It keeps v1 to phones, as the scope says.
   - (b) Use Stage 8's iPad landscape layout on Android tablets as well.
   - (c) Let the phone layout stretch.

   The opt-out stops working when Play requires target 37. That is expected about August 2027, and by then (b) is
   needed.
3. **ABIs.** Ship `arm64-v8a` only, which is recommended: every phone that runs API 28 or later and is sold with Play is
   very likely 64-bit capable. Or also add `armeabi-v7a` for 32-bit-only low-end phones, which costs a second Qt kit and
   a second build.
4. **Launch screen.** The Android convention is the icon on the token colour. The iOS skeleton image cannot be the
   splash on Android 12 and later.
5. **Backup.** Include the store in Android Auto Backup and device transfer, to match iOS (recommended), or exclude it.
6. **Shared export file.** Accept that on Android the temporary `.ejconfig` is removed at the next export or launch, not
   at once.
7. **Play authentication.** Workload Identity Federation (recommended) or a service-account JSON key.
8. **Emulator job blocking.** Keep it non-blocking for two weeks, then make it required if it is stable.

## Owner actions

Only the owner can do these. Nothing in 9.9 can upload until O1 to O6 are done. Packages 9.1 to 9.8 need none of them.

| # | Action | Notes |
|---|---|---|
| O1 | Create a Google Play developer account | Personal or organisation. An organisation account needs a D-U-N-S number. A new personal account must run a closed test with 12 testers for 14 days before production (Stage 11's critical path). There is a one-off registration fee, and Google verifies identity |
| O2 | Create the app in Play Console | Name "CS Bingo Board", default language, app (not game), free or paid (Stage 11 decision) |
| O3 | Complete what Play Console asks for before a first test release | Some App content declarations, such as app access, ads and target audience, are asked for before any track. The full listing, screenshots and data-safety form are Stage 11 |
| O4 | Create the upload key, and add the repository secrets | `keytool` as above, on the owner's machine. Back the `.jks` file and its passwords up offline. Secrets: `ANDROID_UPLOAD_KEYSTORE_BASE64`, `ANDROID_UPLOAD_KEYSTORE_PASSWORD`, `ANDROID_UPLOAD_KEY_ALIAS`, `ANDROID_UPLOAD_KEY_PASSWORD` |
| O5 | Enrol in Play App Signing, and upload the first `.aab` by hand to internal testing | Use the artifact of a `release.yml` dry run. Let Google generate the app signing key |
| O6 | Give CI access to the Play Developer API | A Google Cloud project with the API enabled, and a service account invited in Play Console with release rights to this app only. Then either Workload Identity Federation for this repository (variables `PLAY_WIF_PROVIDER` and `PLAY_SERVICE_ACCOUNT`) or the secret `PLAY_SERVICE_ACCOUNT_JSON` |
| O7 | Add the internal testers | An email list in Play Console (up to 100 testers), and share the opt-in link |
| O8 | Name the Android phone or phones for the exit gate | A real phone, preferably on Android 16, plus one older phone on Android 9 to 12 if one is available |
| O9 | Approve the local SDK download | See below |
| O10 | Answer the [decisions](#decisions-for-the-owner) | Before 9.7 (decisions 2 and 4) and 9.9 (decision 7) |
| O11 | Take licence advice | See [Licence facts](#licence-facts) |
| O12 | If wanted, add a GitHub environment `play` with a required reviewer for the upload step | A repository setting, so the owner does it |

## The developer machine

The Qt Android kit is already installed (472 MB). The Android SDK at `%LOCALAPPDATA%\Android\Sdk` holds only
`platform-tools`: there is no NDK, no build tools and no platform. `C:` has about 19 GB free. Everything new goes on
`E:\android\` (`ANDROID_SDK_ROOT=E:\android\sdk`), with JDK 21 in `E:\android\jdk-21`.

| Download | About |
|---|---|
| Command-line tools, `platforms;android-37`, `build-tools;37.0.0` | 0.3 GB |
| NDK `27.2.12479018` | 2.5 GB unpacked |
| JDK 21 | 0.3 GB |
| Gradle distribution and dependency cache (first build) | 0.5 GB |
| **Build only** | **about 3.6 GB** |
| Emulator plus an API 36 `x86_64` Google APIs system image, plus the Qt `x86_64` Android kit | about 3.5 GB more |
| **Build and local emulator** | **about 7 GB** |

The build-only set is under 5 GB and is downloaded when 9.1 starts. The emulator set takes the total over 5 GB, so it
waits for the owner's yes (O9). Without it, the emulator runs only in CI.

## Sources

- Google Play target API level: <https://developer.android.com/google/play/requirements/target-sdk>
- 16 KB page sizes: <https://developer.android.com/guide/practices/page-sizes>
- Android 16 behaviour changes for apps targeting API 36 (edge-to-edge, predictive back, large screens):
  <https://developer.android.com/about/versions/16/behavior-changes-16>
- Qt 6.12 for Android, supported configurations: <https://doc.qt.io/qt-6/android.html>
- Play Console Help, testing requirements for new personal accounts:
  <https://support.google.com/googleplay/android-developer/answer/14151465>
- LGPL-3.0: <https://www.gnu.org/licenses/lgpl-3.0.txt>; GPL-3.0: <https://www.gnu.org/licenses/gpl-3.0.txt>
