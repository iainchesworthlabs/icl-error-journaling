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
| integration | `apps/app/tests/viewmodel/integration/*.cpp` | `ej_viewmodel_integration_tests`: `ThreadedStorageGateway` on a real thread (and, in work package 3.11, the real store and the threading tests; TSan runs them in CI) |
| perf | `apps/app/tests/viewmodel/perf/*.cpp` | `ej_viewmodel_perf_tests`: the budgets of the threading section, added by 3.11 |
| system | `libs/storage/tests/kill` | `ej_kill_driver`: the kill harness. It starts `ej_kill_worker` as a separate process, kills it, and checks the store (see below) |
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
in, with the Qt libraries on the path:

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
`migration_steps.hpp` (version 1 to 3: a column that every log gets a value in, and a table filled from the entries); the
product has no steps. M1 forward migration keeps every row and adds what the steps add, and the backup is the old database
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
