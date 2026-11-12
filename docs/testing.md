# Testing

Tests use Catch2 through CTest. Each test carries one label that names its kind: `unit`,
`integration`, `system`, `ui` or `perf`. CI runs them in that order, with `unit`,
`integration` and `system` in parallel, `ui` at two jobs, and `perf` last on its own.
A failed test is run once more by name; a pass on the second run is reported as a warning so
flaky tests stay visible. A phase that matches no tests fails, unless the phase is marked optional.

| Kind | Where | Example |
|---|---|---|
| unit | `libs/*/tests` | `ej_core_tests`, `ej_storage_tests`, `ej_ios_bridge_tests` (the desktop stubs, the portable share presenter and the QML-facing wiring, against fakes; the tests that use real files are tagged `[integration]` and get that label; the iOS code is only compiled in CI) |
| integration | `libs/storage/tests` | `ej_storage_integration_tests`: real SQLite and real files, each test in a directory of its own under the build tree (never the system temp folder). It has its own `main` because Qt SQL loads its drivers through a `QCoreApplication` |
| unit | `apps/app/tests/viewmodel/test_*.cpp` | `ej_viewmodel_tests`: the view-model layer on `MemoryLogRepository`, the fakes and `ManualStorageGateway` |
| integration | `apps/app/tests/viewmodel/integration/*.cpp` | `ej_viewmodel_integration_tests`: `ThreadedStorageGateway` on a real thread, the threading tests, and the full-day test on the real store (`test_full_day.cpp`); TSan runs them in CI |
| perf | `apps/app/tests/viewmodel/perf/*.cpp` | `ej_viewmodel_perf_tests`, built only when the directory has files. The timing checks of the threading section (no gap over 30 ms while a 40 ms commit runs) are in `integration/test_threading.cpp` so that TSan runs them; the final budgets are set from a device in Stage 5 |
| system | `libs/storage/tests/kill` | `ej_kill_driver`: the kill harness. It starts `ej_kill_worker` as a separate process, kills it, and checks the store (see below) |
| ui | `apps/app` | `app.smoke` starts the application off-screen and waits for a frame; `app.version` |
| ui | `apps/app/tests/ui/*_test.cpp` | `ej_ui_tests`: Catch2 cases that load the QML offscreen and drive it from C++ (below) |
| ui | `tools/launch_image` | `launch.image_current`: the PNGs of the launch image in the asset catalogue are what `LaunchSkeleton.qml` draws now, light and dark (`ej_launch_image --check`); `launch.image_compare`: that comparison fails on a moved tile and passes on a rounding |
| perf | `apps/app/tests/ui/*_test.cpp`, cases tagged `[perf]` | the same program: `perf.press_path`, `perf.scene` and `perf.first_frame` (below) |
| unit | `apps/app/tests/viewmodel/test_about_model.cpp` | `AboutModel`: the facts of the build, the list of components and the page of a licence from made-up notices, the paragraphs of a licence text, and a document that cannot be read |
| ui | `apps/app/tests/ui/settings_screen_test.cpp`, `app_flow_test.cpp` | the cases `settings.about*`, `app.about` and `app.timing`: the About row, the page, the licence and component pages, the five taps on the Version row, the source row, VoiceOver names, light and dark, and the largest text size |
| unit | `apps/app` (CMake) | `about.notices`: `tools/licences/notices.py --strict --check` over the real link line of `ej_app`; it fails when a linked library has no licence entry or a licence has no text. `about.build_info`: the commit is seven hex digits or `unknown`. `about.tools`: `tools/tests/test_notices.py` (made-up link lines for an iPhone, a desktop and a vcpkg port; a made-up repository). `about.qt_snapshot`: the SBOMs of a Windows or macOS Qt kit against `qt-modules.json`, when the kit has them |
| unit | `tools/ci/check_setup_doc.py`, `tools/tests/test_check_setup_doc.py` | The setup-file page of the user guide against the reader in `libs/core`: every key, required or optional, limit, range, clock choice, message kind and the example (`docs/ci.md`). The tests change one fact of a copy of the reader or the page and expect the check to find it |
| unit | `tools/ci/check_qml_*.py` | `qml.platform`, `theme.no_literals`, `i18n.strings`, `qml.tests`, `motion.animators`, `qml.format` and `qml.lint`: the checks that keep the QML honest (`docs/ci.md`) |

## Tests of the QML

The QML is tested with Catch2 and not with Qt Quick Test (`docs/plan/stage-4.md`, decision 1): a case loads a component
or a screen in an offscreen window (`QT_QPA_PLATFORM=offscreen`, the software renderer, as `app.smoke`), injects its
required properties, drives it with synthesised touch and key events and asserts on the item tree. The program is
`ej_ui_tests` (`apps/app/tests/ui/`); every `*_test.cpp` in that directory is found by a glob, and a package adds its
file and nothing else. A test file is named after what it tests in snake case (`Tile` is `tile_test.cpp`,
`qml/theme/Theme.qml` is `theme_test.cpp`), and `qml.tests` fails a QML file without one. A case tagged `[perf]` has the
label `perf`; the others are `ui`. The module `ErrorJournalingApp` is compiled into the test program from the same files
as the application, plus the stubs in `tests/ui/stubs/` that only the rig's own tests use.

```cpp
#include "rig/ui_checks.hpp"   // project and Stage 3 headers first, Qt headers after (see the note below)
#include "rig/ui_rig.hpp"

#include <catch2/catch_test_macros.hpp>
#include <QQuickItem>

TEST_CASE("tile: the second tap of a double-tap asks on its touch-down", "[ui][tile]")
{
    ej::ui::UiRig rig;
    QQuickItem* tile = rig.load("Tile", {{"typeId", "mute-delay"}, {"label", "Mute Delay"} /* required properties */});
    REQUIRE(tile != nullptr);                     // rig.errors() says why not
    rig.tap(tile);                                // the first tap asks for nothing
    const int finger = rig.press(tile);           // touch-down of the second tap: the handler has run
    rig.advance(120ms);                           // animations and the attached TestClock
    rig.release(finger);
    ej::ui::expect_clean(ej::ui::check_everything(rig, [](const ej::ui::Node&) { return 88.0; }));
}
```

| `UiRig` | What it is for |
|---|---|
| `load(type, properties)`, `load_source(qml, properties)`, `errors()`, `root()` | Creates a type of the module (or QML text) with its required properties, sized to the window (402 by 874 pt, an iPhone 17). Each call replaces the last item. One rig is one engine, so `Theme` is a new singleton in each |
| `set_dark`, `set_text_scale`, `set_reduce_motion`, `theme()` | The three inputs of `Theme`, as the shell sets them from the bridge |
| `press`, `move`, `release`, `tap`, `hold`, `drag`, `swipe`, `type`, `key` | Touch (several fingers may be down at once; a press is delivered at touch-down) and keys, in the item's coordinates. `hold(item, duration)` is a press, `advance(duration)` of virtual time with the finger still, and a release: a long press, with nothing run on the gateway in between |
| `advance(ms)`, `settle()`, `attach(TestClock&)`, `attach(ManualStorageGateway&)`, `on_advance`, `on_settle`, `wait_real(ms)` | Time is manual. The animation clock is replaced by `ManualAnimationDriver`, and `advance` steps it by 16 ms and renders a frame after each step, so an animation or an animator (`ScaleAnimator`, `XAnimator`, `OpacityAnimator`) of 120 ms is finished after one `advance` of that span, and a test never sleeps. An animator writes its value back when it ends, so look at its result after the whole duration. `advance` also moves an attached `TestClock`; `settle` runs the jobs that wait in an attached `ManualStorageGateway`. A QML `Timer` and a `QTimer` run on the wall clock and `advance` does not move them (the UI has none); `wait_real` waits for one in real time. Time that the UI needs from a QML animation (the 500 ms hold of a tile) is a `NumberAnimation` of a property nothing reads inside a `SequentialAnimation` with a `ScriptAction`, because Qt ticks a lone `PauseAnimation` by the wall clock; `steady_time()` keeps one endless animation running so that the driver ticks between holds and a new animation is not credited with the time that went by while none ran (off by default: it also makes a QML `Timer` follow `advance`) |
| `rig/services.hpp`: `use(rig, services)`, `screen_properties(services)`, `type_id`, `board_count` | Puts Stage 3's `Rig` under the `UiRig`: `advance` and `settle` move its clocks and run its store, and a screen gets the nine models as required properties. `Rig` is `RigStore::MEMORY`, so nothing is committed until `settle()` |
| `tree()`, `accessible_tree()`, `geometry(item)`, `find(objectName)`, `find_item(objectName)`, `find_items(objectName)`, `layout()`, `grab()` | The item tree with each item's rectangle in points, whether it takes a press, whether a screen reader reaches it, and its accessible role, name, description and value; the tree in the order a screen reader reads it. `find` walks the QObject tree and misses the delegates of a Repeater or a view and the pages of a StackView; `find_item` walks the item tree and finds them. A StackView is a container and is not checked as a control (give it `Accessible.name`) |
| `HideBase` | Hides the base layer of a screen for as long as the object lives, so that the reading order of a sheet or an alert open over it can be checked; Qt cannot hide what is under a modal from a screen reader |

The cross-cutting tests of the plan are functions in `rig/ui_checks.hpp` that each component and screen test calls on what it
loaded, in the states it can be in. Each returns the faults, one line for each, and `expect_clean` fails the case with them.
A check that cannot fail proves nothing, so `rig_test.cpp` runs each against a stub that follows the rule and a stub that
breaks it (`stubs/UiStubBad*.qml`).

| Function (test name) | What it finds |
|---|---|
| `check_names` (`a11y.names`) | An interactive item that is shown and has no `Accessible.role` or `Accessible.name`, or that a screen reader does not reach, and two controls under one parent with the same name and value |
| `check_targets` (`a11y.targets`) | A target under 44 pt (or what the caller says, 88 for a tile), counting the `margin` of its pointer handlers |
| `check_reading_order` (`a11y.reading_order`) | An item read before one that is above it, or before one to its left in the same row |
| `check_text_scale` (`a11y.text_scale`) | A `Text` that is cut off (`truncated` with `elide: Text.ElideNone`, which is 3 in Qt and the default), taller or (without wrapping) wider than its box, or that leaves the window; `check_everything` runs it at 1.0, 1.3 and 3.1 |
| `check_safe_area(top, bottom)` (`a11y.safe_area`) | Anything interactive or readable inside an inset or outside the window |
| `check_motion_reduced` (`motion.reduce`) | An animation over 100 ms or a slide, with Reduce Motion on |
| `time_each`, `percentile` (`perf.press_path`) | The time of each press, p95 under 16 ms over 200 |
| `measure_scene` (`perf.scene`) | The number of items and delegates (asked of each Repeater and ListView, however the delegate is declared), against `tests/ui/perf-budget.json` |
| `snapshot_geometry`, `moved_items` (`perf.first_frame`) | A rectangle that moved between two states, for the Log screen in `LOADING` and after the labels arrive |

`motion.curves` (in `theme_test.cpp`, label `ui`) holds the durations and the easing curves of `Theme.motion` and of `docs/design/tokens.json` to the
values recorded in `docs/plan/stage-4.md` ("Recorded motion values") and in the test, so that a number changes in all of them or the test fails.
`motion.animators` (`tools/ci/check_qml_motion.py`, label `unit`) fails a `NumberAnimation`, `PropertyAnimation`, `SmoothedAnimation`,
`SpringAnimation` or `Behavior` on `x`, `y`, `scale` or `opacity` anywhere in `apps/app/qml/`; the Animator types are the only ones allowed.

`contrast_test.cpp` is `a11y.contrast`: every pair of `Theme.contrastPairs` (4.5 to 1 for text, 3 to 1 for a non-text
indicator) in light and in dark, the 11 tile colours under the tile text, no red or green tile hue, and a floor on how far
apart two tiles are for a person with deuteranopia. The approved light palette has four pairs under the rule (the Other
tile border and the placeholder, on two backgrounds each); they are listed in `Theme` with the ratio they have, so that
they cannot get worse. `theme_test.cpp` is `theme.matches_tokens` (every value of `docs/design/tokens.json` against the
singleton, both ways) and `theme.dark_resolves`; `safe_frame_test.cpp` is `safe_frame.insets`, `safe_frame.override`, `safe_frame.keyboard` and `safe_frame.reveal`;
`keyboard_test.cpp` is `keyboard.coverage`, `keyboard.is_text_input` and `keyboard.dismiss`.

The keyboard tests model the phone's window: `UiRig::set_safe_area(62, 34)` gives every `SafeFrame` the insets of an iPhone 17
under `Qt.ExpandedClientAreaHint` (the window is the whole screen), and `show_keyboard(h)` reports the rectangle as Qt's iOS
plugin does, in window coordinates and docked at the bottom (`show_keyboard_starting` is the "will show" report, below the
window). `Keyboard.hide()` takes the rig's keyboard down, so a test sees whether the app put it away. `app_flow_test.cpp` has
the cases of each screen with a text field (`app.keyboard_details` to `app.keyboard_first_launch`) and of leaving one
(`app.keyboard_back`, `app.keyboard_picker`, `app.keyboard_tabs`, `app.keyboard_sheets`).

The core defines a name (`STRICT`) that the Windows headers behind Qt Quick define as a macro, so in a test file the
project and Stage 3 headers come before any `<Q...>` header, which clang-format's include order gives. The headers of the
rig declare the Qt Quick classes and do not include them, so they can be first.

Not proved by these tests: how it looks, what VoiceOver says, the real insets, Dynamic Type and the appearance switch as iOS
delivers them, the frame rate, a real finger. The plan lists them under "What cannot be verified here"; they are the device
checklist.

## Visual capture

Comparing how the app looks with the mock-ups of `docs/design/screens` is done by eye, on pairs of pictures that
`tools/visual/run_capture.py` makes (the commands are in its header). It is not a test and no CI phase runs it.
`apps/app/tests/ui/visual_capture_test.cpp` holds one hidden case, `capture.mockups` (tag `[.capture]`: Catch2 does not select a
hidden case with the exclusion-only spec `~[perf]` that `catch_discover_tests` uses, so `ctest -N` does not list it, and the case
skips itself unless `EJ_CAPTURE_DIR` is set). For each of the 34 mock-ups it brings the real shell, over the Stage 3 services and a
fake clock, to the state that the mock-up draws, as a person would (taps, a swipe, typing), under the safe-area insets of an
iPhone 17 (59 pt and 34 pt, put in by an item above the shell), and writes `<Name>.app.png` and `<Name>.dark.app.png` at
`QT_SCALE_FACTOR` (2: 804 by 1748) with `capture-manifest.json`, which says what differs in the data. The offscreen platform has no
usable system font on the developer machine, so the case registers the `.ttf` files of `EJ_CAPTURE_FONTS` (Inter, from
`tools/visual/fetch_inter.py` into `build/visual/fonts`, never committed). The phone uses the system font, so the font is not a finding.
`render_mockups.py` renders the mock-ups in headless Edge with the same font files, and `make_pairs.py` puts the two side by side
and writes `index.html`. `Rig::set_startup_recovery` hands the next `restart()` a recovery report, for the Recovery mock-up.
The iPad mock-ups of Stage 8 (`*.ipad-portrait*` and `*.ipad-landscape*`, generated by `tools/visual/ipad_mockups.py`)
are rendered at the size in their `$preview` (820 by 1180 or 1180 by 820). `ipad_mockups.py --check` fails when a generated
file differs from what the script writes.
The foldable mock-ups of Stage 13 (`*.fold-*` and `*.flip-*`, generated by `tools/visual/fold_mockups.py` from the same
building blocks) are rendered the same way. Until package 13.1 gives the capture case the foldable sizes and folds, their
pairs show "No app state". `fold_mockups.py --check` fails when a generated file differs from what the script writes.

The in-app help mock-ups of Stage 10 (`HelpSheet`, `HelpSheetLarge`, `SettingsHelp`, `SettingsQuickHelp`, `LogFirstRunTip`,
`ErrorTypesHelp` and `ImportErrorsHelp`, generated by `tools/visual/help_mockups.py`) are phone pages like the others. They have
no app state to pair with until package 10.6 builds the help. `tools/tests/test_help_mockups.py` fails when a generated file
differs from what the script writes, or a help button has no accessible name.

Each scene has a device: the phone (402 by 874, insets 59 and 34) or, for the iPad mock-ups of Stage 8
(`Name.ipad-portrait` and `Name.ipad-landscape`), an iPad Air 11-inch (820 by 1180 or 1180 by 820, insets 24 and 20). The
iPad scenes bring the app to the phone scene's state in the iPad window.

The layout class of a window (Stage 8: Compact, Regular or Wide) comes from the `WindowLayout` singleton, which the shell
binds to its size. A test picks a class by giving the rig a size: `UiRig(QSize)`, or `UiRig::set_window_size` to resize
the window and the loaded item as a rotation does. `UiRig::set_safe_area_sides` adds left and right insets to those of
`set_safe_area`. The cases `layout.*` (`window_layout_test.cpp`) check the classes and their sizes.

## Screenshots of every screen

All screenshot output goes under `screenshots/`, which git ignores: `screenshots/screens/` for the pictures of every screen (this section) and `screenshots/pairs/` for the mock-up and app pairs of the visual capture (above). The scripts default to those folders; CI uploads `screenshots/screens/` as the `screenshots` artifact.

`tools/ej_screenshots/ej_screenshots.py` renders every screen in light, dark and large text (Stage 7, 7.1;
`docs/plan/stage-7.md`). The screens are the states of `tools/ej_screenshots/screens.json`, grouped by flow: the scenes of
the visual capture (the mock-up states, phone and iPad) and the states the design package did not draw (`extra_scenes()` in
`visual_capture_test.cpp`: the tap hint, the hold sheet, the Other search, the environment and version-time sheets, and
the Facilitator and Time format pages). The modes are in the same file: `light`, `dark`, and `large` (light at a text
scale of 3.1, the largest iOS size); a mode is added there without code.

The pictures come from the `ui` case `screenshots.render`, which ctest runs on every build leg (with its own timeout of
900 seconds, `[screenshots]`). Without `EJ_SCREENSHOT_DIR` it renders into a folder it throws away, so the run checks that
every state renders; with it, it writes `<State>.<mode>.png` and `screenshots.json` there. `EJ_SCREENSHOT_ONLY` narrows it to
a comma separated list of states. The same case fails when a state of `screens.json` has no scene, a scene is not listed,
or a `.qml` file under `apps/app/qml/screens/` (other than `private/` and `launch/`) is not named by any state's `files`
(or by `not_screens`): a new screen needs a state before it can merge. The script runs the case with the folder set and
writes `index.html`. The pictures are not golden files and are not compared; CI uploads them (`docs/ci.md`).

### Store screenshots

`tools/ej_screenshots/store_screenshots.py` makes the App Store and Google Play screenshots (Stage 11, 11.4,
`docs/plan/stage-11.md`). `tools/ej_screenshots/store_shots.json` is the fixed shot list: the sets (iPhone 6.9", iPad 13" in
portrait and in landscape, Play phone, Play large screen in portrait and in landscape), their exact pixel sizes, and for each
shot the state of `screens.json`, the mode and the file name. The script renders those states (the phone at a scale of 3,
the iPad at 2), draws a neutral status bar (9:41, signal, battery) in the safe-area band, fits the picture to the canvas with
the screen's own colour in any margin, and writes RGB PNGs with no alpha and no metadata to
`<out>/store/<store>/<set>/<file>.png`, with `manifest.json` (size and SHA-256 of each). It adds no caption. `--check` reads
the list only. `tools/tests/test_store_screenshots.py` (`unit`) checks that every state and mode is in `screens.json`, that
the counts and shapes are what the stores take, that every picture has its set's exact size, and that two runs give the same
bytes. The Screenshots job of CI makes the sets for every ready pull request. They are not committed.

## Golden tests and fixtures

The export format is pinned by golden files: `libs/core/tests/golden/<name>.txt` holds the exact bytes an export must
produce. A test builds a log, renders it and calls `ej::test::check_golden("<name>", text)` (`libs/core/tests/golden.hpp`). The
comparison is on bytes, so a missing final newline, a tab or a different dash fails. On a mismatch the failure message
names the first differing line and the helper writes the actual output to `<name>.actual.txt` in
`<build>/libs/core/tests/golden-actual/`. To accept an intended change, copy that file over the golden and review the diff
in the pull request. `.gitattributes` marks the golden and fixture files `-text eol=lf`, so a checkout on Windows cannot
convert line endings.

Every golden is built by a scenario of the same name in `libs/core/tests/golden_scenarios.cpp`, which also fixes the
context (unit prefix, clock format, totals, time zones). `test_export_golden.cpp` renders every scenario and compares it,
and fails when a file in `golden/` has no scenario. The two daylight-saving goldens use the platform's time zone database
(`melbourne-dst-spring`, `melbourne-dst-autumn`); the others use fixed offsets. The totals line wording is provisional
(`export_format.hpp`): changing it means regenerating `totals-on.txt`, `totals-on-empty.txt` and `deleted-entries.txt`.

A fixture is any other input file a test reads, kept in `libs/core/tests/fixtures/` and found with
`ej::test::fixture_path(name)`. Fixtures and goldens contain only the fictional stand-ins (Sam Example, UNIT, North Site,
Wrong Response); the confidentiality check runs over them.

## Tests of the view-model layer

The cases are Catch2 and link `Qt6::Test` for `QSignalSpy` only (Stage 3, decision 3). Each executable owns a `QCoreApplication`
(`support/test_main.cpp`). The sources of the tests are found by globs, so a work package adds `test_<thing>.cpp` (`unit`),
`integration/<thing>.cpp` or `perf/<thing>.cpp` and nothing else. The support library `ej_viewmodel_test_support`
(`apps/app/tests/viewmodel/support/`, included as `"support/<name>.hpp"`, namespace `ej::test`) is the same for all three:

| Helper | What it is for |
|---|---|
| `TestClock` | The wall `Clock` and the monotonic `Scheduler` in one. `advance(ms)` moves both and fires due timers in order; `step_wall_back` and `step_wall_forward` move the wall clock only; `set_wall` puts it anywhere. Starts at 08:55 Melbourne on 7 Nov 2026 |
| `SequentialIds`, `FakeDeviceZone`, `TestEnvironment` | Ids that count (`Uuid::from_random(0, n)`, the same as `test_id<Id>(n)`), a settable device zone, and one struct that owns them with the fixed-offset zones and hands out the `Environment`. `sample_configuration()` reads `config/sample-config.json` |
| `EventLog` | The order of things that happened (`COMMIT`, `HAPTIC`, `CONFIRMATION`, `TOAST`, `SHARE`, `CLIPBOARD`, `WRITE_FAILED`, `LOG_CHANGE`, `CONFIGURATION`, `MARK`), for the tests that the touch haptic comes before the commit and the confirmation after it. Thread safe |
| `RecordingRepository` | A `LogRepository` over another: records every call and its thread, writes a `COMMIT` event after each mutating call that succeeded, makes the next calls of an operation fail (`fail_next`), holds a commit (`set_before_commit`) or slows it (`set_commit_delay`), and counts the open logs after each mutating call (`open_log_counts`) |
| `MemoryStorage`, `FakeConfigStore` | `MemoryLogRepository` seen through a `RecordingRepository`, a text `ConfigStore` in memory that can fail a load or a save, and the `StorageContext` that holds both |
| `ManualStorageGateway` | Jobs wait until the test calls `run_next()` or `run_all()`; a job and its completion run in the same call, so no event loop is needed. This is how a test looks at the state between "submitted" and "committed" |
| `FakePlatform`, `FakeClipboard` | `platform_services` with a share sheet that ends at once (the desktop stub) or later (`hold_completion`, `complete_share_sheet`, as iOS), and the haptics; a clipboard that keeps every text byte for byte |
| `Rig` (`support/rig.hpp`) | The whole application, headless: `app::AppRoot` built exactly as `main.cpp` builds it, on `TestClock`, `SequentialIds`, `FakePlatform` and `FakeClipboard`. `RigStore::MEMORY` is the `unit` kind (the in-memory repository behind `ManualStorageGateway`: nothing runs until `settle()`); `RigStore::SQLITE` is the `integration` kind (`SqliteLogRepository` over the file journal and `FileConfigStore` in a temporary folder, a `ThreadedStorageGateway`, Qt's zone provider, recovery at every start). `go_to` and `advance` move the clock, `tap` fires the touch-down impact haptic and then `recordError`, `hold_commits` and `release_commits` look at the state between "asked" and "committed" in either kind, `restart` rebuilds every service on the same store, and `in_store` and `stored_log` read the store through the gateway. The screen tests of Stage 4 take `root()` and give its models to QML |
| `full_day(Rig&)` (`support/full_day.hpp`) | The exit-gate test of Stage 3: a day from the empty store to History, in 19 steps, through the invokables and properties QML uses, with three comparisons against `libs/core/tests/golden/prd-example.txt`. Run by `test_full_day.cpp` (`unit`) and `integration/test_full_day.cpp` |
| `FakeFeedbackSink`, `FakeConfigurationSource`, `FakeConfigurationEditor`, `FakeOpenLogSource`, `FakeLogObserver`, `FakeConfigurationObserver`, `FakeOpenLogObserver` | The interfaces a service reads its neighbours through, so a package is tested without the class that implements them |

`ThreadedStorageGateway` tests are `integration`. They pump the event loop with `QCoreApplication::processEvents` or call
`drain()`; they never sleep to wait for a result.

The golden comparison, the scenarios behind the golden files and the fixed-offset zone provider of the core tests are the
library `ej_core_test_support` (`libs/core/tests`), linked by the core tests and the view-model tests. A mismatch writes its
`<name>.actual.txt` to `<build>/libs/core/tests/golden-actual/` whichever test found it.

## Repository contract tests

`libs/storage/tests/contract_cases.hpp` holds one set of cases for `LogRepository` and runs each against every
fixture listed in `Fixtures` (a `TEMPLATE_LIST_TEST_CASE`). A new repository adds a fixture that builds it with a
`ScriptedClock` (and a `fill_entries` for the size-limit case); it gets every case for free. `contract_cases.hpp` is included
by `test_contract_memory.cpp` (label `unit`) and `test_contract_sqlite.cpp` (label `integration`). The in-memory repository
applies the same `apply_record` rules as the real one, so a behaviour that differs between them is a failing case, not a surprise. Sample data comes from
`libs/core/tests/storage_samples.hpp` and uses only the fictional stand-ins.

## The journal and power loss

`LossyFile` (`libs/storage/tests/lossy_file.hpp`) is a `JournalFile` that keeps appended bytes apart until `sync`, and
`after_power_loss` gives the file as a disk might leave it: everything synced and a random prefix of the rest, sometimes
followed by zeros. `test_journal_writer.cpp` loses power inside 2,000 appends (a different seed each) and checks that every
earlier record is read back whole and that the one in flight is whole or a torn tail, never a bad line. It models the journal
only. SQLite does its own file I/O, and the kill harness (below) proves the ordering of the two, not the drive.

## The kill harness

`libs/storage/tests/kill` holds the `system` tests of Stage 2: two programs and a shared workload. `ej_kill_worker` opens a store
directory with the production code (recovery first, then the repository over the file journal, as the app does at launch) and runs
a scripted workload, writing `START <n>` before operation `n` and `ACK <n>` after the repository call returned success, one
flushed write per line. `ej_kill_driver` (a Catch2 program with its own `main`) starts the worker, reads the pipe, ends the worker
with `QProcess::kill()` (SIGKILL on Linux and macOS, `TerminateProcess` on Windows, where the worker also joins a Job Object that
dies with the driver), and then runs the production `Recovery` over the dead worker's store. Nothing is shared with the child,
so no page cache of the child's is "helping". Because the `ACK` line is written after the real acknowledgement, the driver's list
of acknowledgements is never longer than the real one, so it never demands something that was not promised.

The workload comes from `(master seed, iteration)` alone (`kill_workload.cpp`, SplitMix64, fixed across platforms): an add of an
error entry (60%), a note (15%), an edit (8%), a soft delete or restore (7%), a header correct or change (4%), a share (2%), an
archive and start of a new log (1%) and a draft (3%), against the open log or, one time in four for edits, deletes and shares, an
archived log. The driver replays the first `k` operations into the in-memory repository to know what the store must hold, so the
worker needs to tell it nothing. After a kill at acknowledgement `A` (with operation `A + 1` possibly in flight) the checks are:

1. `Recovery::run` (full check) succeeds and reports `clean` or `replayed`; `rebuilt` or worse fails the iteration;
2. the recovered store equals the model after `k` operations for some `k` of `A` and `A + 1`, compared on every log (a digest of
   everything a journal records), which log is open, and the draft: this catches loss, duplication, reordering and half-applied
   operations in one comparison;
3. `integrity_check` is `ok`, and the journals agree with the database (the full recovery compares them);
4. the next operation of the workload applies and gives the state after `k + 1`;
5. a second `Recovery::run` is `clean` and changes no file.

On a failure the iteration prints `FAIL seed=<master> iteration=<i> A=<A> k=<k>: <reason>` (counts and short ids, never entry text)
and copies the store as the kill left it to `$EJ_KILL_ARTIFACT_DIR/seed-<master>-iteration-<i>`.

| Test (Catch2 name) | What it kills | Count |
|---|---|---|
| `kill: random kills lose no acknowledged entry` | the worker after a random acknowledgement `N` (1 to 400) and a random delay (0 to 5 ms, weighted to 0) | `EJ_KILL_ITERATIONS`, default 25 |
| `kill: every crash point leaves a store that recovers whole` | K1 to K6 and K9, each in two ways: the worker ends itself at the point (`--crash-mode exit`), and it stops there and the driver kills it (`--crash-mode wait`). The point is reached in the first write, an ordinary write, a later write and, for the archive pair, in each of its two records | 24 points, twice, every run |
| `kill: recovery killed at each of its crash points ...` | K7 and X3: recovery over C1, C4 and C6 damage, killed at each of its crash points in both ways, then recovered again | every run |
| `kill: recovery killed at random moments ...` | the recovery worker, 0 to 150 ms after it starts | `EJ_KILL_RECOVERY_ITERATIONS`, default 25 |
| `kill: a typed draft is lost for no more than the debounce` | the draft worker (see The note draft) at a random moment | `EJ_KILL_DRAFT_ITERATIONS`, default 6 |
| `kill: a migration killed at each of its crash points ...` | K8 and M3: the test-only migration (version 1 to 3) of a populated store, killed at each of its five crash points in both ways; then the next launch's `Migrator::resume`, and the migration run again to the end | every run |
| `kill: a migration killed at random moments ...` | the migration worker, 0 to 120 ms after it starts | `EJ_KILL_MIGRATION_ITERATIONS`, default 25 |

Environment: `EJ_KILL_SEED` (decimal or `0x` hex; drawn at random when unset, and printed first as
`ej-kill master_seed=0x... iterations=...`), `EJ_KILL_ITERATIONS`, `EJ_KILL_FIRST` and `EJ_KILL_LAST` (the range of iterations,
to shard a long run; iteration `i` depends only on `(master seed, i)`), `EJ_KILL_RECOVERY_ITERATIONS`, `EJ_KILL_ARTIFACT_DIR`
(default `./ej-kill-artifacts` in the test's working directory) and `EJ_KILL_SUMMARY` (a file that gets the one-line result of
the run appended). To repeat one iteration: `ej_kill_driver --seed 0x... --iteration 123 "kill: random kills*"`. It runs the
iteration 20 times and says how often it failed; a kill that lands inside an operation depends on how the system schedules the
two processes, so a random-kill failure may not repeat at once, and the saved store can be recovered and looked at either way.
Crash-point failures repeat every time.

To run the harness by hand, build `ej_kill_driver` (which builds the worker) and run the driver from the directory it was built
in, with the Qt libraries on the path (CTest arranges this itself on Windows, where a program started by hand needs the Qt
`bin` directory on `PATH`; `docs/building.md`, "Windows"):

```bash
EJ_KILL_SEED=0x1234 EJ_KILL_ITERATIONS=100 build/linux-debug/libs/storage/tests/kill/ej_kill_driver "kill: random kills*"
```

`ctest -L system` runs every case with the default counts, which is what the pull request gate does. The larger tiers (merge
queue, nightly, the Stage 2 exit gate) run the same program with more iterations: `docs/ci.md`. On Windows (the developer
machine only) a store is synced after every write, so an iteration takes seconds; use a few iterations.

A process kill ends the process, but the operating system still holds and writes what the process had already handed to it, so a
missing or weak `fsync` does not show. The harness proves the application's own ordering and atomicity. Power loss and the Apple
`fsync` caveat are covered only by `LossyFile` (the journal) and by the pragma read-back; a pull-the-battery run on a device is
not part of Stage 2.

## Recovery tests

`test_recovery.cpp` damages copies of one populated store (`recovery_fixture.hpp`): an open log of 300 entries, two archived
logs that were edited, deleted from and shared after they were archived, and a draft, all written through the production
code. It takes seconds to build because every write is synced, and CTest starts a process for each case, so the first to need
it builds it in the working directory (`ej-test-tmp/pristine-<compile time>`) and renames it into place; the others copy it.
After every recovery a case checks that each log has the digest it should, that the checks pass, that every journal is clean,
and that a second recovery finds nothing to do and changes no file. A file made unreadable (C13) needs the system's own way of
doing it, in `libs/storage/tests/platform/<os>/`.

## The note draft

`DraftStore` (`libs/storage/include/error_journaling/draft_store.hpp`) keeps the note field's text and writes it after at most
300 ms. Its tests never sleep: `fake_draft_timer.hpp` has a `DraftTimer` the test advances by hand and a wall clock that counts
its reads (one for each write), so "twenty changes within 300 ms make one write", "a change at 299 ms and one at 301 ms", the
wall clock stepping back ten days and forward a year, `flush`, an empty string, `committed()`, a write that fails and is tried
again, and a store that goes away are exact. `test_draft_store.cpp` (`unit`) runs on the in-memory repository;
`test_draft_store_sqlite.cpp` (`integration`) runs the same store on SQLite and the file journal, and checks that a flushed
draft survives a reopen, that the timer's write reaches the database, and that adding a note clears the draft in the same
transaction. The kill harness covers it under real kills: `kill: a typed draft is lost for no more than the debounce` types
into the field every 10 to 90 ms in the worker (`--mode draft`, with a timer polled on the monotonic clock), kills the worker
at a random moment, and requires the stored draft to be the text of a change that was typed, and no older than the last change
that was at least 1.5 seconds old at the kill (the 300 ms debounce plus slack). `EJ_KILL_DRAFT_ITERATIONS` sets the count
(default 6).

## The launch cost

`libs/storage/tests/perf/test_launch_cost.cpp` (`perf`, executable `ej_storage_perf_tests`) builds the store of a year of use,
80,300 entries in 365 logs with a journal each (written in bulk, one transaction a log, since the synced write path would take
hours), and times, on a warm cache, `quick_check`, `integrity_check`, a connection open, the 364 reads of an archived journal's
tail, the launch (`Recovery::run` with its default options) and the first launch after an upgrade (`Recovery::run` with the full
check). It prints the figures on one line starting `ej-perf`, and fails when the launch is over its budget. A build with
sanitizers, coverage or no optimisation gets a factor (`EJ_PERF_BUDGET_FACTOR`, set by CMake) so the same test runs in every
leg. The figures and the budget are in `docs/plan/stage-2.md`, section 11.

## Migration tests

`test_migrator.cpp` (`integration`) runs `Migrator` over copies of the populated store with the test-only steps of
`migration_steps.hpp` (from the schema version the product writes now, two versions on: a column that every log gets a value
in, and a table filled from the entries). The product's own step, 1 to 2 (`day_log.changed_since_share`), is tried by
`test_schema_upgrade.cpp` on a version 1 database made from a real store: it is migrated at launch, nothing is lost, a log
that was shared reads as shared, the schema equals a new one, and a database rebuilt from the journals knows which logs
changed after their share. M1 forward migration keeps every row and adds what the steps add, and the backup is the old database
byte for byte; M2 a step that fails restores the previous database byte for byte; M4 a `foreign_key_check` or caller check that
fails restores; M5 a newer schema or another application's database is refused with no file changed; M6 the backup copy and the
marker are durable, and the marker names the copy, before the first step runs (the `Observer` option records the order); M7 a
migrated database that is then lost rebuilds from the journals written before the migration. The states a killed migration can
leave (a marker with the database at the old version, at the new version, damaged, missing, or failing its checks; no backup;
an unreadable marker) are made by hand and resumed, and `Recovery::run` is shown to deal with a marker before it opens anything.
M3 and K8 are real kills in the kill harness (above): the next launch must find the old schema whole or the new schema whole,
with every row, no marker, and a migration that then runs to the end.

## Coverage

`linux-coverage` builds with instrumentation. CI runs gcovr and `coverage_gate.py` against
`tools/coverage-floors.json`. Floors are per component: `default` applies to any component not
listed. The gate prints a hint when a component is above its floor; raise the floor in the same
pull request so coverage only moves up.

A line that cannot be reached by a test, such as a failing system call, is excluded with a
`GCOVR_EXCL_BR_LINE` comment and a reason.

## Running one kind

```bash
ctest --preset linux-debug -L unit
```
