# Stage 2: storage and data safety

Task breakdown and test list for Stage 2 of the [implementation plan](../implementation-plan.md). Each work package is
one pull request into `develop`. Requirement IDs refer to the [PRD](../prd.md).

**Exit gate:** zero lost acknowledged entries across 10,000 kill iterations, and all corruption cases recover.

Losing an entry from the open log is a P1 defect, and no UI work starts until this gate has passed. This plan is
therefore written to find ways to lose data, not to show that the happy path works. Sections 3 and 5 are the core of it.

## 1. Decisions to confirm first

The implementation plan leaves four points open. Each has a recommendation and is repeated in the
[open questions](#10-open-questions). Work package 2.1 cannot start until the owner has answered the first.

1. **Order of journal and database (needs a decision).** The plan says the journal is fsynced *after* the database
   commit. That order makes the journal never ahead of the database, so "replay the journal over the database for any
   change missing from it" could only ever fire if the database had lost committed data. Recommended: **append and fsync
   the journal record first, then commit the database, and acknowledge only after both.** The journal is then always a
   superset of the database, replay has a real job, and a crash between the two steps loses nothing. Section 3.2 gives
   the argument and the cost.
2. **What "acknowledged" means.** A write is acknowledged when the repository call has returned success to its caller,
   which happens only after the database commit *and* the journal fsync have both completed (DS-01, DS-02). Anything
   later than that point must survive. Anything earlier may or may not survive.
3. **A write that was in flight when the kill landed** (not yet acknowledged) may appear after recovery or may not. It
   must appear whole or not at all.
4. **Archived logs are not covered by the journal in the plan.** Rebuild from the journal restores only the open log.
   Section 5.4 proposes keeping each archived log's journal.

## 2. Design

### 2.1 Targets and layout

| Target | Contents |
|---|---|
| `ej_core` (QtCore only) | `LogRepository` interface, `StorageError`, `RecoveryReport`, `Clock`, journal record types and their JSON encoding (pure, no file access) |
| `ej_storage` (QtCore, QtSql) | `SqliteLogRepository`, `MemoryLogRepository`, `JournalWriter`, `JournalReader`, `Recovery`, `Migrator`, `DraftStore`, `StorageLocation` |
| `src/platform/<os>/` | `durable_sync` (fsync on Linux and Windows `FlushFileBuffers`, `F_FULLFSYNC` on macOS and iOS), `file_protection` (no-op on desktop; the iOS implementation lands in Stage 5), `file_lock` |
| `tests/storage/` | Unit, integration and system tests; `ej_kill_worker` and `ej_kill_driver` for the harness |

The interface lives in `ej_core` so that the core and, later, the services are testable with no database. No platform
`#if` anywhere: the three platform pieces above are chosen by CMake.

### 2.2 `LogRepository`

Library code returns `std::expected<T, StorageError>`; nothing throws across the boundary. Methods (names are a
proposal; Stage 1's `Log` aggregate decides the exact value types):

- `create_log(header)`, `open_log()`, `archive_log(id)`, `list_logs()`, `load_log(id)`
- `add_entry(log_id, entry)`, `edit_entry(entry)`, `set_entry_deleted(entry_id, bool)`
- `correct_header(log_id, field, value)` (Correct, no entry) and `add_entry` of kind `header_change` (Change)
- `hard_delete_log(id, DeleteConfirmation)` where `DeleteConfirmation` is a type only the History confirmation path can
  construct, so DS-05 is enforced by the type system and not by a comment
- `set_draft(text)`, `get_draft()`
- `mark_shared(log_id, utc)`

Every mutating call is one atomic unit: one journal record, one database transaction. Entry and log ids are generated
by the caller (UUID) before the call, which is what makes replay idempotent.

`MemoryLogRepository` implements the same interface with a map. A single typed contract test suite runs every case
against both implementations, so the memory version cannot drift from the real one.

### 2.3 Schema version 1

`PRAGMA application_id` is set to a fixed constant and `PRAGMA user_version` to 1. Tables follow the PRD data model:

| Table | Columns |
|---|---|
| `day_log` | id, local_date, facilitator, unit_number, location_id, location_text, software_version, created_utc, last_shared_utc, archived |
| `entry` | id, day_log_id, seq, kind, captured_utc, captured_tz, edited_utc, deleted |
| `error_entry` | entry_id, error_type_id, error_type_label, attached_note |
| `note_entry` | entry_id, template_id, value_text |
| `header_change_entry` | entry_id, field, old_value, new_value |
| `draft` | one row: text, updated_utc |
| `store_state` | one row: `applied_seq` (the highest journal sequence number committed to this database), `journal_generation` |

Notes: `seq` is the journal sequence number of the record that created the entry and breaks ties when two entries have
the same `captured_utc`. `error_type_label` is a snapshot, so an entry still renders after the error type is removed
from the configuration (open question 5). Foreign keys on, `ON DELETE` restricted except `hard_delete_log`, which
deletes children in one transaction. Indexes on `entry(day_log_id, captured_utc, seq)` and `day_log(archived)`.

### 2.4 Connection settings and durable sync

Set on every open and **read back**; the open fails if the database does not report what was asked for (Qt SQL ignores
a pragma the driver cannot honour):

- `journal_mode=WAL` (returned value must be `wal`), `synchronous=FULL`, `foreign_keys=ON`, `busy_timeout`.
- `fullfsync=ON` and `checkpoint_fullfsync=ON`. On Apple platforms `fsync` does not flush the drive cache, so without
  these a `synchronous=FULL` commit is weaker than it claims. This is the single most likely source of a real-world
  loss that a SIGKILL test can never reveal.
- `wal_autocheckpoint` left at its default; a manual `wal_checkpoint(PASSIVE)` after archive.

The journal uses `durable_sync()` from `src/platform/<os>/` (not `QFile::flush`, which does not sync): `fsync` on Linux,
`F_FULLFSYNC` on macOS and iOS, `FlushFileBuffers` on Windows. A newly created journal file also gets a sync of its
directory.

### 2.5 Journal format (DS-02)

One file per open log, `journal/<log_id>.jsonl`, opened append-only and exclusively locked (a second writer is refused,
not interleaved). One line per record:

```text
{"v":1,"seq":42,"op":"add_entry","log":"…","body":{…},"crc":"1a2b3c4d"}\n
```

- `seq` is gapless and starts at 1 for each log. `crc` is CRC-32 over the canonical JSON of every other field.
- The first record of a journal is `create_log` carrying the whole header, so a rebuild needs nothing from the database.
- Operations: `create_log`, `add_entry`, `edit_entry`, `set_deleted`, `correct_header`, `mark_shared`, `archive`, and
  `void` (cancels a named earlier `seq`; see 3.3). The draft is **not** journalled (section 2.8).
- Records are logical, not row images, and carry their own version `v`. The reader understands every version ever
  shipped, so a journal written by an older build still rebuilds after a schema migration.
- A record is complete only when it ends with `\n` and its CRC matches. Anything else is not a record (section 5.2).
- Maximum record size is bounded (notes are capped, open question 7), so a torn tail can never look like a huge valid
  line.

### 2.6 Write path

For every mutating call, in this order:

1. Validate the arguments and assign `seq = applied_seq_in_memory + 1`.
2. Append the record to the journal and `durable_sync` it. **Failure here:** return the error; nothing else happened.
3. `BEGIN IMMEDIATE`; apply the change; set `store_state.applied_seq = seq`; `COMMIT` (WAL, synchronous FULL, so the
   commit is durable on return). **Failure here:** write a `void` record for `seq` and sync it, or, if that fails too,
   truncate the journal back to its length before step 2. Return the error. The caller must not see an entry that is
   absent, and a later launch must not resurrect an entry the user was told failed.
4. Return success. Only now may Stage 3 show feedback (DS-01).

If the recommended order is rejected (question 1), steps 2 and 3 swap, the `void` logic disappears, and replay can no
longer be exercised by an ordinary crash; it would then be tested only by the corruption cases.

### 2.7 Recovery on launch (DS-03, DS-04)

`Recovery::run(location)` returns `std::expected<RecoveryReport, StorageError>` and is called before the board appears.

1. **Classify the open error before doing anything destructive.** Only a database that opens and reports corruption
   (`SQLITE_CORRUPT`, `SQLITE_NOTADB`), or is missing, or is truncated below the header size, goes down the recovery
   path. A permission error, a locked or protected file (iOS before first unlock), a read-only volume, or a full disk is
   a **different error**; it never triggers a rebuild and never touches the files.
2. If a migration marker exists, finish or undo the migration first (section 2.9).
3. `quick_check` on the database. If it fails, or on the first launch after an upgrade, run the full `integrity_check`.
   Whether the full check is on every launch is open question 6 (DS-04 says every launch; the cost is measured in 2.6).
4. Read the journal: validate each record, discard a torn tail, and stop at the first gap or mid-file bad record while
   keeping the later valid ones aside (section 5.2).
5. **Healthy database:** replay every record with `seq > applied_seq` in one transaction. Then compare the open log in
   the database with the log folded from the journal (count, ids, and a hash of the canonical form). A mismatch that
   the integrity check missed (SQLite has no page checksums) is reported and repaired from the journal.
6. **Failed or missing database:** build a new database file from the journal alone, verify it, then swap it in with an
   atomic rename. The damaged files go to `quarantine/<utc>-<n>/` and are never deleted by the app. Keep the newest
   five.
7. Return the summary. The UI (Stage 4) turns it into the message DS-04 requires.

```text
RecoveryReport {
  outcome: clean | replayed | rebuilt | partial_loss | unrecoverable
  open_log_entries            // entries in the open log after recovery
  replayed_records            // journal records applied over a healthy database
  rebuilt_records             // records used to rebuild
  discarded_tail_bytes        // torn tail removed
  discarded_records           // records skipped for a bad CRC or a gap
  voided_records              // writes that were rolled back by a void
  archived_logs_lost          // archived logs not recoverable (see 5.4)
  draft_recovered             // bool
  quarantined_paths           // paths only, no content
  integrity_message           // SQLite's own text, never user data
}
```

`describe()` renders it as one plain sentence for logs. The report holds counts and paths only, never entry text, so it
is safe in CI output.

Recovery is itself crash-safe: it works on copies and renames at the end, so a kill during recovery leaves a state the
next launch recovers again (tested, section 4.4).

### 2.8 Note draft (NOTE-03)

`DraftStore::set(text)` is debounced by 300 ms on a monotonic clock (injectable timer, so tests use a fake clock, not
sleeps). It writes the single `draft` row in its own transaction (WAL, FULL). `flush()` writes immediately and is what
Stage 5 calls on going inactive. An empty string clears the draft; committing the note clears it in the **same
transaction** as the entry, so a kill cannot leave both the entry and the draft present.

The draft is not journalled: it is a scratch value, not a committed entry. Consequence: text typed in the last 300 ms
before a kill can be lost, and a corrupt database loses the draft. The PRD says the draft survives backgrounding and
closing (NOTE-03) and no loss of the draft on crash (NFR-08). Question 3 asks the owner to accept "at most 300 ms of
typing" or to pay for journalling drafts.

### 2.9 Archive, delete, migrations

**Archive (DS-05).** `archive_log` marks the log archived, appends an `archive` record, closes the journal and moves it
to `journal/archive/`. Starting a new day or log is `archive_log` followed by `create_log`, and the pair is one unit: a
kill between them leaves either the old log open or the new one open, never none and never two. Nothing in the app
deletes an open log; there is no API for it.

**Hard delete.** `hard_delete_log(id, DeleteConfirmation)` works on archived logs only and removes the rows and the
archived journal in one operation. The confirmation type can only be built by the History screen's confirm path.

**Migrations (DS-06).** `Migrator` holds an ordered list of steps `n → n+1`. Procedure:

1. Compare `user_version` with the build's version. Equal: nothing. Higher: **refuse to open** and change nothing
   (a newer app wrote this; do not downgrade or "recover" it).
2. Checkpoint the WAL, copy the database to `backup/ej-v<from>-<utc>.db`, sync the copy and its directory, and write a
   `migration.inprogress` marker naming the copy.
3. `BEGIN IMMEDIATE`; run the steps; `foreign_key_check` and `integrity_check`; set `user_version`; `COMMIT`.
4. Remove the marker. On any failure: roll back, close, restore the copy by atomic rename, remove the marker, return the
   error, and leave the old database openable by the old schema code.

A kill between steps 2 and 4 is covered by the marker: the next launch sees it and either finds the old version (the
transaction never committed; remove the marker) or a failed check (restore the copy). SQLite schema changes are
transactional, so the copy mainly protects against a step that commits something wrong and a post-check that fails.
Only version 1 ships in Stage 2; the framework is exercised by test-only steps 1 → 2 → 3 that are not part of the
product build.

### 2.10 iOS file protection (NFR-09), for Stage 5

Stage 2 provides the `file_protection` interface and the desktop no-op, and records the requirements for the iOS
implementation in Stage 5:

- Apply `NSFileProtectionCompleteUntilFirstUnlock` to the data directory so that new files inherit it, **and** to the
  database, its `-wal` and `-shm` files, the journal, `journal/archive/`, `backup/` and `quarantine/`. The `-wal` and
  `-shm` files are created later by SQLite and are the easy ones to miss.
- Check after creation, and add a Stage 5 test that reads the attribute back on a device.
- Reads before the first unlock after a restart fail. `Recovery` must classify that as "locked", not "corrupt"
  (section 2.7 step 1), or a background launch after a reboot would rebuild a healthy database.
- Whether to exclude the files from iCloud backup is open question 8; NFR-09 says data leaves only through the share
  sheet or copy, and a device backup is another way out.

## 3. Failure modes the plan does not mention

### 3.1 Summary

| # | Failure mode | Where handled |
|---|---|---|
| F1 | Crash between the database commit and the journal fsync (or the reverse) | 3.2, test K3 to K6 |
| F2 | Torn journal tail (partial line, zero-filled extent) | 5.2, J1 and J2 |
| F3 | Database failure *after* the journal append: a record for a write the user was told failed | 2.6, test W6 |
| F4 | `fsync` is not a full flush on Apple platforms; a process kill never exposes it | 2.4, 4.2 limits |
| F5 | A stale but valid database (restored backup, deleted WAL): integrity check passes and entries are missing | C9, J10 |
| F6 | Silent bit rot: SQLite has no page checksums, so `integrity_check` can pass on wrong data | 2.7 step 5 |
| F7 | A locked or permission-denied file mistaken for corruption, followed by a "rebuild" | 2.7 step 1, C13 |
| F8 | Archived logs are not protected by the journal | 5.4 |
| F9 | Wall clock steps backwards, forwards, or the time zone changes | 3.4 |
| F10 | Recovery itself killed, or out of disk | 4.4 |
| F11 | Disk full during a write, during the journal append, or during recovery | W7, R3 |
| F12 | Two writers on one store (a second process, a future extension) | 2.5 lock, I4 |
| F13 | Launch time: an integrity check that grows with history can break NFR-04 | 2.7 step 3, P1 |
| F14 | A newer database opened by an older build | 2.9, M5 |
| F15 | The in-flight write that appears after a crash is mistaken for a bug | 1 (3), 4.2 |
| F16 | Directory entry not synced after creating the journal, so the whole file is missing after power loss | 2.4 |
| F17 | A note typed in the last 300 ms | 2.8, question 3 |

### 3.2 The order of commit and flush

Take the four possible crash points for one write, with the plan's order (database, then journal):

| Crash after | Database | Journal | Result |
|---|---|---|---|
| nothing | no | no | write never happened; fine |
| database commit | **yes** | no | entry present, journal behind. Fine while the database is healthy. If the database is also damaged in the same event, the entry is gone, though it was never acknowledged |
| journal fsync | yes | yes | fine |
| return to caller | yes | yes | acknowledged |

With the recommended order (journal, then database):

| Crash after | Journal | Database | Result |
|---|---|---|---|
| journal fsync | **yes** | no | journal ahead; replay applies it on next launch. The user never saw an acknowledgement but the entry appears, which is the safe direction for a log of record |
| database commit | yes | yes | fine |

The cost is one rule: a database failure after the journal append must be compensated (a `void` record or truncation,
section 2.6). The benefit is that the database can lose any suffix of recent commits (a stale or damaged WAL, F5) and
the journal still holds every acknowledged entry. With the plan's order, the journal cannot repair a database that
lost only its last few commits unless the journal is complete, which it is *except* for the single in-flight record,
so the practical difference is the replay path being exercised and the "database and journal damaged in the same event"
window shrinking to nothing. Both orders keep every acknowledged entry in the absence of a second fault; the
recommended order also survives a second fault of that kind.

### 3.3 Void records

When step 3 of the write path fails after the journal append, the entry must not come back. Truncating the journal
back is simplest but can fail; a `void` record is appended as the fallback and replay skips the voided `seq`. A test
forces the failure (a repository wrapper that fails the commit) and reopens.

### 3.4 Clock changes

- `Clock` is an injected interface; production uses the system clock, tests use a scripted one.
- The journal and the database order writes by `seq`, never by wall time. Replay and recovery do not read the clock.
- `captured_utc` is whatever the clock said. If the clock steps backwards, a later entry has an earlier time and
  sorts before it on the timeline (PRD requires time order). The tie-break is `seq`. Decision needed: sort by time as
  the PRD says, accept that a step backwards reorders, and rely on the review screen's time editing (REV-02). Tests
  pin the behaviour.
- The debounce uses a monotonic clock so a wall-clock jump neither fires it early nor stalls it.
- Quarantine and backup names use the UTC time plus a counter, so two in the same second or after a step backwards
  never collide or overwrite.
- A time zone change between entries: each entry keeps its own `captured_tz` (Stage 1 owns rendering).
- A date of 1970 or far in the future (device clock reset): stored as given, not rejected.

## 4. The kill harness

### 4.1 Process model

Two executables built by the test target, run by CTest with the `system` label:

- `ej_kill_worker` (child) opens a store directory with the production repository and runs a scripted workload.
- `ej_kill_driver` (parent, a Catch2 test) starts the worker with `QProcess`, reads its output, kills it, reopens the
  store in the parent through the production `Recovery` and checks the invariants.

The parent never shares memory or a database handle with the child, so nothing from the child's page cache is
"helping". Each iteration uses a fresh directory under the CTest working directory and one process start.

### 4.2 The workload and the invariants

The child derives everything from `(master_seed, iteration)`: a pseudo-random generator seeded by SplitMix64 of the pair
produces the sequence of operations, the pacing, and the kill plan. The sequence is a mixture, by weight: add error
entry (60%), add note (15%), edit (8%), soft delete and restore (7%), header correct and change (4%), mark shared (2%),
archive plus create log (1%), set draft (3%). Payloads are synthetic strings built from the fictional vocabulary (Sam
Example, UNIT, North Site, Wrong Response). Pacing is steady (default one operation every 2 ms, with bursts), so the
kill lands mid-operation as often as between operations.

**Observing an acknowledgement.** The child writes `ACK <n>\n` to its standard output with a single unbuffered write
(`write(2)` or `WriteFile`) *after* the repository call has returned success, and never before. It writes `START <n>`
before issuing operation `n`. The parent reads these through the pipe. Because the pipe write follows the real
acknowledgement, the parent's list of acknowledgements is always a subset of the real ones. The parent therefore never
demands something that was not promised, and anything it does demand really was promised. A pipe write lost to the kill
(the child acknowledged operation `n` but died before writing `ACK n`) is the harmless direction: the parent sees `n-1`
as the last acknowledgement and `START n` as in flight.

**Kill plan.** The parent picks, from the iteration seed, a target acknowledgement count `N` in `[1, 400]` and a delay
`d` in `[0, 5]` ms chosen from a mix of 0 µs to 5 ms with extra weight near 0. It reads until it has seen `ACK N`,
waits `d`, then kills. Varying `d` moves the kill through the operation's phases (journal write, journal sync, database
commit, the gap between operations).

**Kill mechanism.**

- POSIX (Linux, macOS): `QProcess::kill()`, which sends `SIGKILL`. It cannot be caught, so no destructor, no flush, no
  `atexit` runs.
- Windows: `QProcess::kill()` is `TerminateProcess`, which gives the same guarantee. The worker is also created in a
  Job Object with kill-on-close so that an aborted test run leaves no stray process. The Windows run is for the
  developer machine only, since CI keeps no Windows leg for this repository; it is still a supported configuration.
- Additionally, in a second mode the worker takes a **named crash point** (`--crash-at after_journal_append`,
  `after_journal_sync`, `before_db_commit`, `after_db_commit`, `after_ack_before_next`, `mid_checkpoint`,
  `mid_recovery`, `mid_migration`, `mid_archive_pair`) and ends itself with `std::_Exit(137)` the instant it reaches it. The
  hook is a runtime-injected callback in the repository, null in production, so there is no `#if`. This mode is exactly
  reproducible: it tests every gap in section 3.2 on every run instead of waiting for the random kill to land in one.

**Checks after the kill** (the parent opens the store with `Recovery::run`, then reads it back):

1. Recovery succeeds with `outcome` `clean` or `replayed`. (`rebuilt` and worse is a failure here: nothing was
   corrupted.)
2. Let `A` be the last acknowledged operation seen and `n_start` the last started. The recovered store must equal the
   result of applying operations `1..k` of the deterministic sequence for some `k` with `A ≤ k ≤ n_start`. The parent
   computes the expected state for `A` and for `A+1` (if in flight) and compares with the whole open log, not just the
   last entry. This catches loss, duplication, reordering and half-applied operations in one comparison.
3. The draft equals the last draft value set at least 300 ms (plus slack) before the kill, or a later one.
4. The journal and the database agree (the section 2.7 step 5 comparison finds nothing to repair).
5. `PRAGMA integrity_check` is `ok`.
6. A second `Recovery::run` changes nothing (idempotent).

On failure the iteration prints `FAIL seed=<master> iteration=<i> A=<A> k=<k>`, copies the store directory (database,
WAL, journal) to an artifact directory and exits non-zero. The data is synthetic so the artifact is safe to upload.

**What a process kill cannot test.** `SIGKILL` and `TerminateProcess` end the process, but the operating system still
holds and eventually writes what the process had already handed to it, so a missing or weak `fsync` does not show. The
kill harness proves the application's own ordering and atomicity. Loss of power, and the Apple `fsync` caveat (F4), are
covered only by (a) the `LossyFile` tests below, which model unsynced data being lost, and (b) the `fullfsync` readback
in 2.4. A device pull-the-battery run is not in Stage 2 and is listed as unverified in each pull request.

**`LossyFile` (power-loss simulation, deterministic).** A test double of the journal's file interface that keeps
written-but-unsynced bytes in a buffer, and on a simulated crash keeps a random prefix of them (sometimes cut mid-line,
sometimes extended with zeros). The same workload runs against it for many seeds with no process involved, which is
fast and gives the journal torn-tail coverage at volume. It does not cover SQLite's own file I/O, which relies on SQLite's
tested guarantees plus the readback of the pragmas.

### 4.3 Seed and reproduction

- `EJ_KILL_SEED` sets the master seed; if unset the driver draws one from `std::random_device` and **prints it first**,
  before the first iteration, in the form `ej-kill master_seed=0x… iterations=…`.
- `EJ_KILL_ITERATIONS` sets the count; `EJ_KILL_FIRST` and `EJ_KILL_LAST` select a range so that a long run can be
  sharded. Iteration `i` depends only on `(master_seed, i)`.
- Reproduce one failure with `ej_kill_driver --seed 0x… --iteration i`. The operation sequence, pacing and kill target
  are identical. Where the kill lands inside an operation depends on the operating system's scheduling, so a random-kill
  failure may not repeat on the first try; the driver re-runs the iteration up to 20 times in repro mode and reports how
  often it failed. Crash-point failures repeat every time. A failure that does not repeat still leaves the saved store
  directory, which can be recovered and inspected directly.

### 4.4 Iteration counts per tier

| Tier | Where it runs | Random-kill iterations | Notes |
|---|---|---|---|
| Local, developer | `ctest -L system` | 25 | seconds, with the crash-point set |
| PR gate | Linux debug in the ready-PR job | 25, random seed per run | plus every crash point once, `LossyFile` at 2,000 seeds; budget 90 s |
| Merge queue and push to `develop` | Linux debug and ASan/UBSan, TSan, macOS | 100 | the sanitiser legs catch memory errors on the recovery path |
| Nightly | Linux release, macOS | 1,000, split into 4 shards of 250 | fresh seed each night; seed printed and kept as an artifact |
| **Exit gate** | manual workflow run on Linux release and macOS | **10,000**, split into 10 shards of 1,000 per OS | fixed, recorded seed; must be zero failures on both |

Timing is an estimate to confirm in work package 2.8: at roughly 0.3 to 1 s per iteration, 10,000 is 1 to 3 hours of
runner time, sharded into well under 30 minutes of wall time. If an iteration is slower on the macOS runner, the shard
count rises, not the iteration count. The nightly and the exit-gate runs need CI changes (a nightly job and a manual
workflow); whether those belong in this repository or in `icl-common-infra` is open question 9.

**Recovery and migration kills.** Separate drivers kill the child during `Recovery::run` over a corrupt store, and during
a migration, 200 iterations each at the nightly tier (25 at the PR gate), and check that the next launch recovers or
completes with the same invariants.

## 5. Corruption cases

Each case is produced by a fixture builder that creates a populated store through the production code (one open log of
300 entries plus two archived logs), closes it cleanly, then damages a **copy**. All produce synthetic data. The
expected result is always: the app opens, the open log is intact, and the report says what happened.

### 5.1 Database cases

| ID | Case | How it is produced | Expected |
|---|---|---|---|
| C1 | Zero-length database | truncate to 0 bytes | `rebuilt`; open log restored from journal |
| C2 | Truncated mid-file | truncate to 60% of its size, and to a size that is not a multiple of the page size | `rebuilt` |
| C3 | Header destroyed | overwrite the first 100 bytes with zeros; separately with random bytes | `rebuilt` (not a database) |
| C4 | Damaged table page | flip 16 bytes inside an interior page of `entry` found by reading the page map | `rebuilt` (integrity check fails) |
| C5 | Damaged index page | same, on a page of an index | `rebuilt` or `replayed` after reindex; report says which |
| C6 | Missing file | delete the database, leave journal and any WAL | `rebuilt` |
| C7 | Orphan WAL | delete the database, keep a populated `-wal` | `rebuilt`; the orphan WAL is quarantined, not applied to a new file |
| C8 | Torn WAL | truncate the `-wal` mid-frame | `replayed`: SQLite drops the partial frame; journal restores the acknowledged entries it covered |
| C9 | **Stale database** | restore an older copy of the database, delete the `-wal` | `replayed`; the integrity check passes and the entries are still restored from the journal |
| C10 | Missing `-shm` | delete it | `clean`; SQLite rebuilds it |
| C11 | Zero-filled database | same size, all zeros | `rebuilt` |
| C12 | Garbage file | random bytes of the same size | `rebuilt` |
| C13 | Unreadable, not corrupt | remove read permission (POSIX) or hold an exclusive lock from another process (Windows) | **no rebuild**; a `StorageError` of kind `locked` or `permission`, files untouched |
| C14 | Wrong `application_id` | set a different value | refused as "not our database"; not overwritten; files untouched |
| C15 | Silent data damage | change a stored `value_text` in place with SQLite so every check passes | detected by the section 2.7 step 5 comparison; repaired from the journal |
| C16 | Newer schema | set `user_version` above the build's | refused; files untouched (M5) |

### 5.2 Journal cases

| ID | Case | How it is produced | Expected |
|---|---|---|---|
| J1 | Torn tail | cut the last record at a random byte | tail dropped; `discarded_tail_bytes` > 0; database already has everything acknowledged |
| J2 | Zero-filled tail | append 4,096 NUL bytes; separately a half record followed by zeros | tail dropped |
| J3 | Bad CRC mid-file | change a byte inside one record in the middle | that record skipped and counted; later valid records are kept aside, and applied only if their `seq` is consistent (see below) |
| J4 | Missing journal, healthy database | delete the journal | `clean`; a new journal is seeded from the database and the report notes it |
| J5 | Journal short, database ahead | truncate the journal at a record boundary | `clean` or journal re-synced from the database |
| J6 | Duplicate records | append a copy of the last 10 records | replay is idempotent; no duplicate entries |
| J7 | Gap in `seq` | remove one record from the middle | stop at the gap; report `discarded_records`; do not guess |
| J8 | Garbage line | insert a non-UTF-8 line | skipped and counted |
| J9 | Foreign journal | journal of another log id in place | refused for the open log; reported |
| J10 | Database far behind | database from 200 records ago, journal complete | `replayed` with 200 records |
| J11 | Voided write | journal ends with a record and its `void` | the voided entry does not appear |
| J12 | Oversize record | a 10 MB line without a newline | rejected by the size bound, not read into memory |

The rule behind J3 and J7: a log of record must not silently skip a hole and carry on as if it were whole. Records
after a gap are applied only if each is self-contained (an `add_entry` whose log exists); an `edit` or `set_deleted`
that refers to something missing is counted and skipped. The report says `partial_loss`.

### 5.3 Combined cases

| ID | Case | Expected |
|---|---|---|
| X1 | C1 or C4 together with J1 | `rebuilt` from the journal up to the torn record; only the unacknowledged write is lost |
| X2 | C12 together with J4 (database and journal both gone) | `unrecoverable` for the open log; a new empty store is created, both damaged sets are quarantined, the report says so plainly. This must not crash and must never delete anything |
| X3 | Kill during recovery of C4 | next launch recovers again with the same result |

### 5.4 Archived logs (F8)

The plan's rebuild restores the open log only. After a database loss, every archived log would be gone, which the
facilitator would reasonably call data loss. Proposal: on `archive`, keep the finished journal in `journal/archive/`
and include it in the rebuild, so history is rebuilt too. A journal is a few tens of kilobytes per day. `archived_logs_lost`
in the report is zero when that works. If the owner declines (question 2), the PRD should say archived logs are best
effort, and the Stage 2 gate stays about the open log.

## 6. Work packages (one PR each, in this order)

### 2.1 Interfaces and the in-memory repository

`LogRepository`, `StorageError`, `Clock`, `RecoveryReport`, journal record types with their encoding and CRC (pure
functions in `ej_core`), and `MemoryLogRepository`. The typed contract suite is written here and runs against the
memory version. Depends on the answers to questions 1, 4 and 5 and on Stage 1's `Log` and `Entry` types.

### 2.2 Schema v1 and `SqliteLogRepository`

Schema, pragma setup with readback, `fullfsync`, the write path of 2.6 (journal calls stubbed with a null journal for
this PR), id generation, the contract suite against SQLite, `hard_delete_log` and `DeleteConfirmation`, archive.
`docs/architecture.md` updated for the layout.

### 2.3 Platform sync, file lock and the journal

`durable_sync` and `file_lock` in `src/platform/<os>/`, `JournalWriter`, `JournalReader`, `LossyFile`, wiring into the
write path (journal before commit, `void` on failure), crash-point hooks. Unit tests for the format, the torn-tail and
CRC cases J1 to J12 at the reader level, and the `LossyFile` tests.

### 2.4 Recovery

`Recovery::run`, error classification, quick and full integrity checks, replay, the journal-versus-database comparison,
rebuild into a new file with atomic swap, quarantine, the archived-journal retention of 5.4 if accepted. All corruption
cases C1 to C16, J1 to J12, X1 to X3, plus idempotence.

### 2.5 Kill harness

`ej_kill_worker`, `ej_kill_driver`, the workload generator, seeds, crash points, failure artifacts, the PR-gate
configuration (25 iterations). Local Windows run documented. `docs/testing.md` updated (labels and the environment
variables).

### 2.6 Draft store and launch cost

`DraftStore` with the 300 ms debounce on a fake monotonic clock, commit-and-clear in one transaction, `flush()`. The
`perf`-labelled launch measurement: `quick_check`, full `integrity_check` and a journal comparison on a database with
one year of synthetic logs (about 80,000 entries), with the budget in question 6.

### 2.7 Migrations

`Migrator`, the marker file, backup copy and restore, test-only steps, future-version refusal. Tests M1 to M7.

### 2.8 CI tiers and the exit-gate run

Merge-queue count (100), nightly shards (1,000), the manual 10,000-iteration workflow with fixed seed, artifact upload on
failure, `docs/ci.md`, `.github/ci/plan.json` if a path needs a build, `tools/coverage-floors.json` for `ej_storage`.
Then the 10,000-iteration run and the written gate result in `docs/plan/stage-2.md` (an appended section).

### 2.9 Stage 5 note

No code. A short section in `docs/architecture.md` recording the section 2.10 requirements so that Stage 5 starts
from them.

2.2 to 2.4 are strictly sequential; 2.6 and 2.7 can follow 2.4 in either order; 2.5 can start after 2.3 and must be
finished before 2.4 is called done (the recovery cases are checked through the harness). Every code PR runs
`linux-debug` and `linux-asan-ubsan` locally, and says what did not run on a Mac or device.

## 7. Test list

Labels: `unit` (no files beyond a temp directory), `integration` (real SQLite and files), `system` (kill harness),
`perf`.

**Repository contract (unit, run against memory and SQLite):** create, add, list in time order with the `seq` tie-break,
edit, soft delete and restore, header Correct versus Change, archive then list, a hard delete without the confirmation
type does not compile (a `static_assert`/concept check), hard delete of an open log is rejected, ids unique, a
rejected write leaves the log unchanged.

**Write path (integration):**

- W1 acknowledged write is in the database and the journal.
- W2 write path order: instrumented file and database show the journal sync before the commit.
- W3 pragma readback fails the open when `wal` is not returned.
- W4 `applied_seq` advances with every commit.
- W5 draft cleared in the same transaction as the note.
- W6 forced commit failure after the journal append leaves a `void` (or truncation) and no entry after reopen.
- W7 simulated disk full on the journal append and on the commit: error returned, nothing acknowledged, store usable
  afterwards.

**Journal (unit):** encode and decode round-trip; CRC detects a one-bit change; gapless `seq`; torn tail and zero tail;
bound on record size; exclusive lock refuses a second writer (I4); every record version still decodes.

**Recovery (integration):** the 31 cases of section 5; recovery is idempotent (run twice, same log, report `clean` the
second time); replay twice gives the same log (the plan's idempotence test); `outcome` and counts are right for each;
no file is ever deleted (the test lists the directory before and after and asserts the damaged files are in
`quarantine/`).

**Kill (system):** random-kill iterations per tier (section 4.4); the crash-point set K1 to K9 once per run:

| ID | Crash point | Expected |
|---|---|---|
| K1 | `after_journal_append` | journal record may be unsynced; entry present or absent, whole either way |
| K2 | `after_journal_sync` | entry appears after replay |
| K3 | `before_db_commit` | same as K2 |
| K4 | `after_db_commit` | entry present, acknowledged-equivalent |
| K5 | `after_ack_before_next` | entry present |
| K6 | `mid_checkpoint` | nothing lost |
| K7 | `mid_recovery` | next launch recovers again (X3) |
| K8 | `mid_migration` | old or new version, never half (M3) |
| K9 | `mid_archive_pair` | exactly one open log |

**Migrations (integration):** M1 forward migration keeps all rows; M2 a failing step restores the previous database
byte for byte; M3 kill mid-migration then relaunch; M4 post-check failure restores; M5 newer schema refused, files
untouched; M6 backup copy exists and is synced before the first step; M7 migrated database still rebuilds from a
journal written before the migration.

**Draft (unit, fake clock):** 20 changes within 300 ms produce one write; a change at 299 ms and another at 301 ms;
`flush()` writes now; the wall clock jumping does not change the timing; the draft survives a clean reopen and the
kill harness's draft check.

**Clock (unit):** wall time stepping back between two entries; same `captured_utc` for two entries; time zone change
between entries; clock at 1970; quarantine names do not collide.

**Perf:** launch-time check on the one-year dataset, with a failing threshold set once measured (question 6).

Each new test carries its label; coverage floors for `ej_storage` start at the measured value after 2.4 and are raised
in later packages.

## 8. Exit gate

All of the following, recorded in this file with the date, master seed, commit and the CI run links:

1. 10,000 random-kill iterations on Linux and on macOS with zero failures, from one recorded master seed.
2. Every crash point K1 to K9 passes on every run of the nightly tier for the preceding seven days.
3. Every corruption case in section 5 passes (C1 to C16, J1 to J12, X1 to X3).
4. `linux-debug` and `linux-asan-ubsan` green; TSan green on the repository and journal tests.
5. Launch-time measurement within its budget on the one-year dataset.

The owner reviews and signs the gate in the pull request that records it. Until then no UI work starts.

## 9. What cannot be verified here

- The development machine has no Mac. macOS runs only on the hosted runner. Windows runs only locally.
- No test here can show behaviour after loss of power on a real iPhone, or confirm that `F_FULLFSYNC` really flushes the
  device's cache. The process-kill tests do not exercise that, and the simulation (`LossyFile`) models it for the journal only.
- File protection (NFR-09), behaviour before first unlock, and iCloud backup exclusion are verified only on a device in
  Stage 5.
- A random kill that landed in a rare window and did not fail in 10,000 iterations is evidence, not proof. The
  deterministic crash points exist so the important windows are not left to chance.

Each pull request says which parts have not run on a device.

## 10. Open questions

1. **Journal first or database first?** Recommended: journal first, then database, acknowledge after both (section 3.2).
   The plan and DS-02 read database first. This changes the wording of DS-02 and the Stage 2 text.
2. **Archived logs:** keep each archived journal and rebuild history too (recommended, section 5.4), or accept that a
   database loss costs the history and say so in the PRD?
3. **Draft durability:** accept that up to 300 ms of typing can be lost on a kill, and that a corrupt database loses
   the draft (recommended, with `flush()` on going inactive), or journal drafts at a cost of a larger journal and a
   journal write per pause in typing?
4. **A write that was in flight when the app died** may appear after relaunch although the user never saw it confirmed.
   Agree that appearing is acceptable (recommended; the alternative is losing real entries)?
5. **Error types in entries:** store a label snapshot with the type id (recommended) so an entry still renders after
   the type is removed from the configuration, or reference the configuration only?
6. **Launch cost:** is `quick_check` on every launch with the full `integrity_check` on the first launch after an
   upgrade, after any `quick_check` failure, and nightly in idle time acceptable under DS-04? Or must the full check run
   every launch (cost grows with history; budget proposed 100 ms of the 1.5 s in NFR-04)?
7. **Size limits:** maximum note length (proposed 10,000 characters) and maximum entries per log, so a journal record has
   a known size bound.
8. **iCloud and device backup:** exclude the data directory from backup to honour NFR-09, or leave it in so a replaced
   phone restores the history? (Stage 5 decision; Stage 2 only needs the answer for the interface.)
9. **Where do the nightly and exit-gate jobs live?** In this repository's workflows, or as a reusable workflow in
   `icl-common-infra`? The current ci.md lists CodeQL as the only nightly job.
10. **Clock steps backwards:** sort by time and accept the reorder (section 3.4, PRD wording), or keep entries in
    capture order by `seq` and show time separately?
11. **How many kill iterations are enough for PR gate and merge queue**, given runner cost? The proposal is 25 and 100;
    both are one environment variable.
12. **Ids:** UUID v4 or v7 for entries (v7 is time-ordered and nicer in the journal; confirm Qt 6.12 provides it, else v4).
