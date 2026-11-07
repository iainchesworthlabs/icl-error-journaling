# Architecture

```text
libs/core/            ej_core: QtCore only; the domain model (Stage 1), namespace `ej`
  include/error_journaling/  public headers: uuid.hpp, ids.hpp (EntryId, ErrorTypeId, NoteTemplateId, LocationId, LogId),
                             header_field.hpp, result.hpp (the std::expected conventions), version.hpp,
                             timestamp.hpp, time_zone_provider.hpp (Qt implementation in src/qt_time_zone_provider.cpp,
                             the only file that includes QTimeZone), time_format.hpp, day_prompt.hpp,
                             configuration.hpp (the configuration file: schema, import, export)
                             Storage contract (Stage 2, no file or database access): log_repository.hpp (the
                             `LogRepository` interface, `DeleteConfirmation`), storage_error.hpp, clock.hpp,
                             journal_record.hpp (the journal record types, their one-line JSON encoding and CRC),
                             stored_log.hpp (what each operation does to a log: shared by every repository and by
                             recovery), recovery_report.hpp, utf8.hpp
  src/                       implementation files, one per component
  tests/                     Catch2 tests, all labelled `unit`
    golden/                  expected export files, compared byte for byte (docs/testing.md)
    fixtures/                other input files for tests
    golden.hpp, golden.cpp   the golden comparison helper
    fake_time_zone_provider.hpp  fixed-offset TimeZoneProvider for deterministic tests
    storage_samples.hpp      sample data (fictional stand-ins) shared with the storage tests
    golden_scenarios.hpp     the scenarios behind the golden files; built with golden.* as the library
                             `ej_core_test_support`, which the view-model tests link too
libs/storage/         ej_storage: the repositories behind `LogRepository` and the journal files (Stage 2); links ej_core, QtSql
                      privately
  include/error_journaling/  memory_log_repository.hpp, sqlite_log_repository.hpp (no Qt SQL type in the header),
                             journal.hpp (the `Journal` the repository appends to; `NullJournal`), file_journal.hpp
                             (the journal of record on disk: one file per log, archived ones under journal/archive/),
                             journal_writer.hpp, journal_reader.hpp (`scan_journal`: the rules of section 5.2),
                             journal_file.hpp (`JournalFile`, the interface under the writer), recovery.hpp
                             (`Recovery::run`, called at launch before the board appears), crash_point.hpp
                             (`CrashPoint` and `CrashHook`, the places the kill harness ends the process),
                             draft_store.hpp (`DraftStore` and the `DraftTimer` interface under it: the note draft,
                             written at most 300 ms after it changes), migrator.hpp (`Migrator`: schema migrations with a
                             backup copy and a marker; the product's list of steps is empty at version 1),
                             storage_location.hpp (the folders of one store)
  src/                       one file per component; quarantine (where recovery puts what it replaces), journal_seed
                             (a journal written again from a log)
  src/platform/              the pieces that differ by operating system, with one directory per system (linux, macos,
                             ios, windows) defining the same functions from the same headers: durable_sync (`fsync`,
                             `F_FULLFSYNC`, `FlushFileBuffers`), file_lock (an exclusive lock that refuses a second
                             writer), file_protection (data protection on iOS, a stub
                             elsewhere; `protect.hpp` decides which paths get it). CMake picks the directory
  tests/                     contract_cases.hpp: one typed suite for `LogRepository`, instantiated by
                             test_contract_memory.cpp (`unit`) and test_contract_sqlite.cpp (`integration`);
                             test_sqlite_repository.cpp (schema, settings, the write path W1 to W7, damaged files);
                             test_journal_reader.cpp (J1 to J12) and test_journal_writer.cpp with lossy_file.hpp (a
                             file that loses power; `unit`); test_file_journal.cpp and test_platform_files.cpp (real
                             files, the lock, the repository over the file journal; `integration`);
                             test_recovery.cpp with recovery_fixture.hpp (a populated store, copied and damaged for
                             each case: C1 to C16, J1 to J12, X1 to X3; `integration`);
                             test_draft_store.cpp (`unit`, on fake_draft_timer.hpp) and test_draft_store_sqlite.cpp
                             (`integration`); test_migrator.cpp with migration_steps.hpp (test-only steps; `integration`); kill/ (`system`: ej_kill_worker, the child process, ej_kill_driver, the
                             Catch2 program that kills it and checks the store, and the workload they share);
                             perf/test_launch_cost.cpp (`perf`: the launch check on a year of logs);
                             helpers: temp_dir.hpp, raw_db.hpp (a second connection), recording_journal.hpp
libs/ios_bridge/      ej_ios_bridge: what Qt does not cover on an iPhone, one small interface per capability, namespace
                      `ej::bridge`
  include/error_journaling/
    share.hpp haptics.hpp            the capability interfaces (Share, Haptics) and their value types; each has a
                                     `make_<name>()` factory defined once per platform
    platform_services.hpp            PlatformServices: a struct of unique_ptr to the capabilities, and
                                     make_platform_services(), one line per capability
    share_presenter.hpp              SharePresenter: the portable decisions around a Share (exactly one of text and file,
                                     valid UTF-8, one sheet at a time, a temporary file deleted on every outcome) and
                                     purge_directory for the launch purge
    accessibility_settings.hpp       AccessibilitySettings (Stage 4, 4.3): text scale from the content size category,
                                     Reduce Motion, VoiceOver, and a change notice; the category-to-scale table is portable
    document_types.hpp               the setup file's type identifier, extension, description and size limit, one
                                     constant each; tools/ci/check_ios_plist.py compares Info.plist.in with them
    document_picker.hpp              DocumentPicker (UIDocumentPickerViewController as copy; UNAVAILABLE on the desktop)
    open_file_filter.hpp             OpenFileFilter: the QFileOpenEvent the system sends for an opened file, queued
                                     until the importer is ready
    config_importer.hpp              ConfigImporter: size check, hand to SettingsModel::loadImport, delete the file on
                                     every outcome; purge_import_leftovers at launch
    config_exporter.hpp              ConfigExporter: SettingsModel::exportTo into a temporary .ejconfig, then the share
                                     sheet (QmlBridge::shareTemporaryFile), which deletes it
    background_tasks.hpp             BackgroundTasks (beginBackgroundTaskWithName on iOS; tokens only on the desktop)
    lifecycle.hpp                    LifecycleCoordinator: on inactive, inside a background task, flush the draft and
                                     drain the writes; on active, tell the haptic policy and run the day check once the
                                     store is ready; at quit, flush. Plain C++ over functions, so a test needs no model
    lifecycle_adapter.hpp            LifecycleAdapter: the only code that listens to Qt for it (applicationStateChanged,
                                     aboutToQuit)
    haptic_policy.hpp                HapticPolicy: when to prepare the generators (board visible, application active, an
                                     optional keep-alive), against an injected clock
    perf_marks.hpp, launch_tracker.hpp
                                     PerfMarks (os_signpost marks for Instruments, the launch log line) and LaunchTracker
                                     (when the board is usable, reported once)
    qml_bridge.hpp                   QmlBridge, the QObject QML sees, over the capabilities (ej_qml_bridge)
  src/platform/ios/      Objective-C++ (.mm, -fobjc-arc), built for iOS: share.mm, haptics.mm, perf_marks.mm, accessibility_settings.mm, document_picker.mm, background_tasks.mm and the internal
                         top_view_controller.mm shared by the presenters
  src/platform/desktop/  stubs with the same file stems, built everywhere else
apps/app/             ej_app: the Qt Quick application (executable error-journaling)
  qml/                the module ErrorJournalingApp (the URI differs from the executable name). Every .qml under it is
                      found by a glob (qml/qml_files.cmake). theme/ holds the token singleton `Theme` and `SafeFrame`;
                      components/, screens/ and shell/ are Stage 4 (docs/plan/stage-4.md). Main.qml is the window and
                      shell/AppShell.qml is everything in it; platform/<ios|desktop>/ holds what differs by platform
  src/                main.cpp (builds the root and hands the models to QML), app_root.* (`ej_app_root`: the composition root,
                      the product's environment and the opening of the store), qt_clipboard.* (the QtGui clipboard)
  src/viewmodel/      ej_viewmodel: the thin QObject layer between the core and storage and QML (Stage 3), namespace
                      `ej::viewmodel`; every .cpp and .hpp here is picked up by a glob (see below). Headers are
                      included as "viewmodel/<name>.hpp"
                        environment.hpp      Scheduler (monotonic one-shot timers), IdGenerator, DeviceZone, and the
                                             `Environment` struct of references that also holds the core `Clock`
                        storage_gateway.*    StorageGateway (post a job and a completion; `run` is the typed form),
                                             StorageContext (the repository and the ConfigStore), ThreadedStorageGateway
                        config_store.hpp     ConfigStore: where the configuration file and the import and export files
                                             are read and written, on the storage thread
                        feedback_sink.*      FeedbackSink: touch acknowledgement (the impact haptic, before a commit)
                                             and confirmation (after a commit); PlatformFeedbackSink, NullFeedbackSink
                        clipboard.hpp        Clipboard (the QtGui implementation is in src/, work package 3.11)
                        configuration_source.hpp  ConfigurationSource and ConfigurationObserver (the read side) and
                                             ConfigurationEditor (the one write the Other sheet makes)
                        log_observer.hpp     LogChange and LogObserver
                        open_log_source.hpp  OpenLogSource and OpenLogObserver
                        observer_list.hpp    ObserverList<T>, the observer set the services share
                        move_only_function.hpp  MoveOnlyFunction, a stand-in for std::move_only_function (Apple libc++ lacks it)
  tests/ui/           the tests of the QML: *_test.cpp (`ui`, or `perf` for a case tagged [perf]), rig/ (UiRig, the
                      cross-cutting checks, the main), stubs/ (QML used by the rig's own tests), perf-budget.json
  tests/viewmodel/    test_*.cpp (`unit`), integration/*.cpp (`integration`), perf/*.cpp (`perf`, built only when the
                      directory has files), support/: the test support (docs/testing.md)
  packaging/ios.cmake    bundle properties, signing and the plugin defaults of Qt's iOS kit
  packaging/ios/         Info.plist.in, asset catalogue (icon, launch colour), verify_app.sh
cmake/icl/            vendored CMake modules from icl-common-infra (do not edit)
tools/                project scripts, baselines and coverage floors (ci/check_ios_plist.py, ci/check_platform_layout.py)
  dataset/             ej_dataset: a desktop tool that writes the one-year fictional store for the device measurements
```

## Targets

| Target | Kind | Depends on |
|---|---|---|
| `ej_core` | static library, C++23 | QtCore |
| `ej_storage` | static library: the in-memory and SQLite repositories, the journal files, recovery, the note draft and migrations | `ej_core`, QtSql |
| `ej_ios_bridge` | static library; Objective-C++ on iOS, a stub elsewhere | nothing (UIKit and Foundation on iOS) |
| `ej_qml_bridge` | static library; the `QmlBridge` QObject QML calls, over the `ej_ios_bridge` capabilities | `ej_ios_bridge`, QtCore |
| `ej_viewmodel` | static library: the view-model layer of Stage 3; no Qt Quick and no QtGui, so its tests run without a display | `ej_core`, `ej_storage`, `ej_ios_bridge`, QtCore |
| `ej_app_root` | static library: `app::AppRoot` (the composition root), `ProductionEnvironment`, `start_sqlite_storage`; QtCore only, so the headless tests build the app the way `main.cpp` does | `ej_viewmodel`, QtCore |
| `ej_app` | Qt Quick executable (`error-journaling`) | the libraries above, `ej_viewmodel`, Qt Quick |

`apps/app/CMakeLists.txt` finds the sources of `ej_viewmodel` with `file(GLOB ... CONFIGURE_DEPENDS)` over `src/viewmodel/`,
and `apps/app/tests/viewmodel/CMakeLists.txt` does the same for the tests, so a work package adds its files without
editing CMake. The view-model layer owns one storage thread (`ThreadedStorageGateway`: a `std::thread` joined in the
destructor, because Apple libc++ keeps `std::jthread` behind an experimental flag). The repository and the configuration
store are used only on that thread; the GUI thread keeps a mirror of what it shows and changes it when a commit has
succeeded.

Each library has tests with the `unit` label; the application has two `ui` tests (`app.smoke` starts it, with a data folder of its own,
without a display and waits for the first frame, `app.version` answers `--version`). The application is the iPhone
application on iOS (portrait only, laid out inside `SafeArea.margins`) and a desktop program on macOS and Linux, built
to iterate on the user interface and to be tested in CI; no desktop package is released.

## Composition

`apps/app/src/app_root.*` is the composition root. `main.cpp` and the headless tests (`Rig`, `apps/app/tests/viewmodel/support/rig.hpp`)
build the application through the same `app::AppRoot`, so the full-day test runs the wiring the phone runs.

```text
main.cpp                                   Rig (tests)
  QmlBridge(PlatformServices)                FakePlatform (Share and Haptics), FakeClipboard
  ProductionEnvironment (clock, QtScheduler,   TestClock, SequentialIds, FakeDeviceZone, the fixed or the Qt zones
    RandomIds, SystemDeviceZone, Qt zones)
  QtClipboard
  start_sqlite_storage(StoreOptions)         the same, with a RecordingRepository around the repository;
    -> StartedStorage                          RigStore::MEMORY: ManualStorageGateway over MemoryStorage instead
  AppRoot(Environment, AppPlatform, StartedStorage)
```

**The store.** `start_sqlite_storage` starts the `ThreadedStorageGateway`. Its factory runs on the storage thread: it makes
the folders, runs `Recovery::run`, opens the `FileJournal`, the `SqliteLogRepository` and the `FileConfigStore`, and hands the
`RecoveryReport` back. This is the one place the GUI thread waits for the store, because Stage 2 puts recovery before the
board appears and the session reads the report once. Everything after it goes through the gateway. A store that cannot be
opened (a newer schema, a file that is not ours) is repository calls that all answer with the reason; nothing is written.

**Order.** `AppRoot` builds, and destroys in the reverse order: the gateway, `SettingsModel` (then `load()`),
`DocumentRegistry`, `PlatformFeedbackSink` (over the bridge's haptics), `LogSession` (then `load()`), `UndoController`, the
follower that keeps the controller on the open document, `BoardModel`, `ErrorPickerModel`, `TimelineModel` (which follows the
open log), `ShareService`, `DayWatcher` and `HistoryModel`.

**Lifetime contract.** A model dies before the documents it observes. The open document is held by `LogSession`, which is
built before every model; an archived log is held by `HistoryModel`, which is built after `UndoController`. `LogSession` tells
its followers "no document" before it lets one go, and the controller stops observing then. `HistoryModel::reviewOpened` and
`reviewClosing` bracket the review document: the root calls `UndoController::observe` and `stop_observing`.

**Connections.** Every `writeFailed` (`LogSession`, `UndoController`, `TimelineModel` and the review timeline, `ShareService`,
`SettingsModel`, and a failed delete in `HistoryModel`) arrives at `AppRoot::writeFailed`, the one place the banner of Stage 4
listens. An undone note goes back to the composer through `LogSession`'s draft. `AppRoot::drain()` is called once from
`aboutToQuit`.

**What QML sees.** The window takes the services as `required property` (Stage 4, decision 3): `session`
(`LogSession`), `undo` (`UndoController`), `board` (`BoardModel`), `picker` (`ErrorPickerModel`), `timeline`
(`TimelineModel`), `history` (`HistoryModel`), `day` (`DayWatcher`), `settings` (`SettingsModel`), `share` (`ShareService`) and
`bridge` (`QmlBridge`). `AppRoot` has the first nine as constant properties, and `main.cpp` passes them, with `bridge`, as the
initial properties of the engine. The engine is declared after the root and the bridge, so QML is destroyed first.

## QML

All QML is in the one module `ErrorJournalingApp`, in three folders (docs/plan/stage-4.md): `qml/theme/` (`Theme`, a
`pragma Singleton` with every colour, size, radius, spacing, type role and motion value of `docs/design/tokens.json`, and
`SafeFrame`, the only reader of the safe-area insets), `qml/components/` (one file for each component, which knows no view
model) and `qml/screens/<screen>/` (the only layer that knows the view models). A file in one folder uses a type from
another after `import ErrorJournalingApp`, which a file of the module may do.

The view models reach a screen as `required property` values passed down from the root window, never as context
properties or singletons, so a test creates a screen with whatever it wants to inject and `qmllint` checks the types.
`Theme` takes three inputs from outside, `colourScheme`, `textScale` and `reduceMotion`, which the shell sets from the
bridge; nothing else in QML reads the platform (`tools/ci/check_qml_platform.py`).

An executable's resources cannot be linked into another executable, so the module is built twice from the same files:
into `ej_app`, and into `ej_ui_tests` together with the stubs. `qml/qml_files.cmake` is the one place that lists the files
(a glob) and marks the singleton, and both `CMakeLists.txt` call it.

**The shell.** `Main.qml` is the window (the colour of the first frame, the title, the required properties from `main.cpp`)
and holds `shell/AppShell.qml`, which is the shell and which the test rig loads, because it loads an item and not a window.
It has four tabs on `TabBar`; Log, Review and Settings each have a `PageStack` for the pages they push (Today's details,
the editors), and a tab is made when it is first chosen. History is a placeholder page until package 4.11 is wired in. One
`SheetHost` for the window carries the Other picker (and, with 4.11, the new-day alert), so it covers the tab bar; the Log screen's template prompt and
Review's edit sheet are put in its layer. The first-launch screen covers the tabs while `settings.configured` is false, and
`ImportFlow` covers everything. `AppRoot.writeFailed` is one banner at the top (the Log screen shows the failures of its
own models itself), and the Undo toast is the Log screen's, and the shell's for Review. `Theme.textScale` and
`Theme.reduceMotion` follow the bridge.

**What differs by platform.** `qml/platform/desktop/FileChooser.qml` (Qt's file dialogs, the one place they are used) and
`qml/platform/ios/FileChooser.qml` (nothing, until Stage 5's document picker, 5.7, replaces it; the C++ side exists as `ConfigImporter` and `ConfigExporter`) have the same type name; the
build takes the file of its platform (`qml_files.cmake`), so no screen asks which platform it is on. Import and export rows
emit `importRequested()` and `exportRequested()`; the shell answers them with the chooser, then `ImportFlow.begin(path,
name)` or `settings.exportTo(path)`. Stage 5 (5.7) keeps the launch tracker (`AppShell.boardEnabled`), the document picker
and the device pass; the day check and the haptic policy's active state are done by the lifecycle of `main.cpp`.

## Platform code

Platform differences live in `src/platform/<os>/` with the same file names in each directory. In the bridge the pairs
are `ios/<name>.mm` and `desktop/<name>.cpp`; `tools/ci/check_platform_layout.py` keeps them in step. An `.mm` file does
only what UIKit or Foundation must do (call the API, translate the result to a plain struct); every decision is portable
C++ against the interface and tested with a fake.
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
