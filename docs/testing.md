# Testing

Tests use Catch2 through CTest. Each test carries one label that names its kind: `unit`,
`integration`, `system`, `ui` or `perf`. CI runs them in that order, with `unit`,
`integration` and `system` in parallel, `ui` at two jobs, and `perf` last on its own.
A failed test is run once more by name; a pass on the second run is reported as a warning so
flaky tests stay visible. A phase that matches no tests fails, unless the phase is marked optional.

| Kind | Where | Example |
|---|---|---|
| unit | `libs/*/tests` | `ej_core_tests`, `ej_storage_tests`, `ej_ios_bridge_tests` (the desktop stub and the QML-facing wiring, against a fake; the iOS code is only compiled in CI) |
| integration | `libs/storage/tests` | `ej_storage_integration_tests`: real SQLite and real files, each test in a directory of its own under the build tree (never the system temp folder). It has its own `main` because Qt SQL loads its drivers through a `QCoreApplication` |
| unit | `apps/app/tests/viewmodel/test_*.cpp` | `ej_viewmodel_tests`: the view-model layer on `MemoryLogRepository`, the fakes and `ManualStorageGateway` |
| integration | `apps/app/tests/viewmodel/integration/*.cpp` | `ej_viewmodel_integration_tests`: `ThreadedStorageGateway` on a real thread (and, in work package 3.11, the real store and the threading tests; TSan runs them in CI) |
| perf | `apps/app/tests/viewmodel/perf/*.cpp` | `ej_viewmodel_perf_tests`: the budgets of the threading section, added by 3.11 |
| ui | `apps/app` | `app.smoke` starts the application off-screen and waits for a frame; `app.version` |

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
only. SQLite does its own file I/O, and the kill harness (work package 2.5) proves the ordering of the two, not the drive.

## Recovery tests

`test_recovery.cpp` damages copies of one populated store (`recovery_fixture.hpp`): an open log of 300 entries, two archived
logs that were edited, deleted from and shared after they were archived, and a draft, all written through the production
code. It takes seconds to build because every write is synced, and CTest starts a process for each case, so the first to need
it builds it in the working directory (`ej-test-tmp/pristine-<compile time>`) and renames it into place; the others copy it.
After every recovery a case checks that each log has the digest it should, that the checks pass, that every journal is clean,
and that a second recovery finds nothing to do and changes no file. A file made unreadable (C13) needs the system's own way of
doing it, in `libs/storage/tests/platform/<os>/`.

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
