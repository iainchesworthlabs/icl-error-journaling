# Stage 2: storage and data safety

Task breakdown and test list for Stage 2 of the [implementation plan](../implementation-plan.md). Each work package is
one pull request into `develop`. Requirement IDs refer to the [PRD](../prd.md).

**Exit gate:** zero lost acknowledged entries across 10,000 kill iterations, and all corruption cases recover.

Losing an entry from the open log is a P1 defect, and no UI work starts until this gate has passed. This plan is
therefore written to find ways to lose data, not to show that the happy path works. Sections 3 and 5 are the core of it.

## 1. Decisions to confirm first

The implementation plan left four points open. The owner has decided them (see
[decisions](#10-decisions-and-open-questions)); they are recorded here because the rest of the plan depends on them.

1. **Order of journal and database (decided).** The plan says the journal is fsynced *after* the database
   commit. That order makes the journal never ahead of the database, so "replay the journal over the database for any
   change missing from it" could only ever fire if the database had lost committed data. Decided: **append and fsync
   the journal record first, then commit the database, and acknowledge only after both.** The journal is then always a
   superset of the database, replay has a real job, and a crash between the two steps loses nothing. Section 3.2 gives
   the argument and the cost.
2. **What "acknowledged" means.** A write is acknowledged when the repository call has returned success to its caller,
   which happens only after the database commit *and* the journal fsync have both completed (DS-01, DS-02). Anything
   later than that point must survive. Anything earlier may or may not survive.
3. **A write that was in flight when the kill landed** (not yet acknowledged) may appear after recovery or may not. It
   must appear whole or not at all.
4. **Archived logs are covered too (decided).** Each archived log's journal is kept, so a rebuild restores history.
   Section 5.4.

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
by the caller (UUID v4) before the call, which is what makes replay idempotent.

`MemoryLogRepository` implements the same interface with a map. A single typed contract test suite runs every case
against both implementations, so the memory version cannot drift from the real one.

### 2.3 Schema version 1 (and 2)

`PRAGMA application_id` is set to a fixed constant and `PRAGMA user_version` to the schema version: 1 in Stage 2, 2 since
the share rule changed (a log is unshared when it changed after its last share), which added
`day_log.changed_since_share`. Tables follow the PRD data model:

| Table | Columns |
|---|---|
| `day_log` | id, local_date, facilitator, unit_number, location_id, location_text, software_version, created_utc, last_shared_utc, archived (and, from version 2, `changed_since_share`: 0 or 1, not null, default 0) |
| `entry` | id, day_log_id, seq, kind, captured_utc, captured_tz, edited_utc, deleted |
| `error_entry` | entry_id, error_type_id, error_type_label, attached_note |
| `note_entry` | entry_id, template_id, value_text |
| `header_change_entry` | entry_id, field, old_value, new_value |
| `draft` | one row: text, updated_utc |
| `store_state` | one row: `applied_seq` (the highest journal sequence number committed to this database), `journal_generation` |

Notes: `seq` is the journal sequence number of the record that created the entry and breaks ties when two entries have
the same `captured_utc`. `error_type_label` is a snapshot, so an entry still renders after the error type is removed
from the configuration (decision 5). Foreign keys on, `ON DELETE` restricted except `hard_delete_log`, which
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
- Maximum record size is bounded (notes are capped at 10,000 characters, decision 7), so a torn tail can never look like a huge valid
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

### 2.7 Recovery on launch (DS-03, DS-04)

`Recovery::run(location)` returns `std::expected<RecoveryReport, StorageError>` and is called before the board appears.

1. **Classify the open error before doing anything destructive.** Only a database that opens and reports corruption
   (`SQLITE_CORRUPT`, `SQLITE_NOTADB`), or is missing, or is truncated below the header size, goes down the recovery
   path. A permission error, a locked or protected file (iOS before first unlock), a read-only volume, or a full disk is
   a **different error**; it never triggers a rebuild and never touches the files.
2. If a migration marker exists, finish or undo the migration first (section 2.9).
3. `quick_check` on the database. If it fails, or on the first launch after an upgrade, run the full `integrity_check`.
   The quick check runs on every launch and the full check only in those two cases (decision 6); the cost is measured in 2.6. DS-04 is reworded to say "integrity check" without requiring the full one.
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
closing (NOTE-03) and no loss of the draft on crash (NFR-08). The owner accepted "at most 300 ms of typing" (decision 3).

### 2.9 Archive, delete, migrations

**Archive (DS-05).** `archive_log` marks the log archived, appends an `archive` record, closes the journal and moves it
to `journal/archive/`. Archiving is the boundary for new entries only: past logs can still be edited, have entries deleted and
restored, and be shared (DAY-04), and each such change reopens the archived journal for appending, journals before the
database commits and syncs, exactly as for the open log (decision 13). Only the open log accepts `add_entry`. Starting a new day or log is `archive_log` followed by `create_log`, and the pair is one unit: a
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
Only version 1 shipped in Stage 2; the framework is exercised by test-only steps from the current version that are not
part of the product build. The product's first step is 1 → 2 (`ALTER TABLE day_log ADD COLUMN changed_since_share
INTEGER NOT NULL DEFAULT 0`): every log that exists gets 0, so a log that was shared reads as shared. `Recovery::run`
runs it before it opens the database; a migration that fails puts the version 1 file back, and the open then rebuilds the
database from the journals. The flag is not part of `digest_of`: it is derived from the order of the journal records, and
a database from before it existed holds none, so the launch check does not call that a difference.

### 2.10 iOS file protection (NFR-09), for Stage 5

Stage 2 provides the `file_protection` interface and the desktop no-op, and records the requirements for the iOS
implementation in Stage 5:

- Apply `NSFileProtectionCompleteUntilFirstUnlock` to the data directory so that new files inherit it, **and** to the
  database, its `-wal` and `-shm` files, the journal, `journal/archive/`, `backup/` and `quarantine/`. The `-wal` and
  `-shm` files are created later by SQLite and are the easy ones to miss.
- Check after creation, and add a Stage 5 test that reads the attribute back on a device.
- Reads before the first unlock after a restart fail. `Recovery` must classify that as "locked", not "corrupt"
  (section 2.7 step 1), or a background launch after a reboot would rebuild a healthy database.
- The files stay in the device backup (decision 8); no exclusion flag is set. NFR-09's wording ("leaves only through
  the share sheet or copy") should say that the user's own device backup is not counted.

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
| F17 | A note typed in the last 300 ms | 2.8, decision 3 |

### 3.2 The order of commit and flush

Take the four possible crash points for one write, with the plan's order (database, then journal):

| Crash after | Database | Journal | Result |
|---|---|---|---|
| nothing | no | no | write never happened; fine |
| database commit | **yes** | no | entry present, journal behind. Fine while the database is healthy. If the database is also damaged in the same event, the entry is gone, though it was never acknowledged |
| journal fsync | yes | yes | fine |
| return to caller | yes | yes | acknowledged |

With the decided order (journal, then database):

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
decided order also survives a second fault of that kind.

### 3.3 Void records

When step 3 of the write path fails after the journal append, the entry must not come back. Truncating the journal
back is simplest but can fail; a `void` record is appended as the fallback and replay skips the voided `seq`. A test
forces the failure (a repository wrapper that fails the commit) and reopens.

### 3.4 Clock changes

- `Clock` is an injected interface; production uses the system clock, tests use a scripted one.
- The journal and the database order writes by `seq`, never by wall time. Replay and recovery do not read the clock.
- `captured_utc` is whatever the clock said. If the clock steps backwards, a later entry has an earlier time and
  sorts before it on the timeline (PRD requires time order). The tie-break is `seq`. Decided (10): sort by time as the PRD
  says and accept the reorder; the review screen's time editing (REV-02) is the remedy. Tests pin the behaviour.
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
workflow); they live in this repository's workflows (decision 9).

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
facilitator would reasonably call data loss. On `archive`, keep the finished journal in `journal/archive/`
and include it in the rebuild, so history is rebuilt too. A journal is a few tens of kilobytes per day. `archived_logs_lost`
in the report is zero when that works. The owner accepted this (decision 2).

## 6. Work packages (one PR each, in this order)

### 2.1 Interfaces and the in-memory repository

`LogRepository`, `StorageError`, `Clock`, `RecoveryReport`, journal record types with their encoding and CRC (pure
functions in `ej_core`), and `MemoryLogRepository`. The typed contract suite is written here and runs against the
memory version. Depends on Stage 1's `Log` and `Entry` types.

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
rebuild into a new file with atomic swap, quarantine, the archived-journal retention of 5.4. All corruption
cases C1 to C16, J1 to J12, X1 to X3, plus idempotence.

### 2.5 Kill harness

`ej_kill_worker`, `ej_kill_driver`, the workload generator, seeds, crash points, failure artifacts, the PR-gate
configuration (25 iterations). Local Windows run documented. `docs/testing.md` updated (labels and the environment
variables).

### 2.6 Draft store and launch cost

`DraftStore` with the 300 ms debounce on a fake monotonic clock, commit-and-clear in one transaction, `flush()`. The
`perf`-labelled launch measurement: `quick_check`, full `integrity_check` and a journal comparison on a database with
one year of synthetic logs (about 80,000 entries), with a budget of 100 ms for the launch check, measured and fixed in this package.

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

**Perf:** launch-time check on the one-year dataset, with a failing threshold set once measured (100 ms proposed).

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

## 10. Decisions and open questions

The owner answered on 7 Nov 2026. The text above reflects these answers.

| # | Question | Decision |
|---|---|---|
| 1 | Journal first or database first | **Journal first**, then database, acknowledge after both (section 3.2). DS-02 is reworded in the PRD to match |
| 2 | Archived logs | **Keep each archived journal** and rebuild history too (section 5.4), so a database loss does not cost the history |
| 3 | Draft durability | **Accepted**: up to 300 ms of typing can be lost on a kill; `flush()` on going inactive |
| 4 | Unconfirmed write after a crash | **Accepted**: it may appear after relaunch |
| 5 | Error type snapshots | **Store the snapshot** (agreed 7 Nov 2026), see below |
| 6 | Launch integrity check | **Quick check on every launch**; the full check runs after a failed quick check and on the first launch after an upgrade |
| 7 | Size limits | **Accepted**: notes up to 10,000 characters; entries per log bounded in the repository |
| 8 | iCloud and device backup | **Left in backup**; the data is tied to the user's own account. Stage 5 does not set the exclusion flag |
| 9 | Where nightly and exit-gate jobs live | **This repository's workflows** |
| 10 | Clock steps backwards | **Sort by time** as the PRD says and accept the reorder; `seq` breaks ties |
| 11 | Iteration counts | **Accepted**: 25 on the PR gate, 100 on the merge queue, 1,000 nightly, 10,000 for the gate |
| 12 | Entry ids | **UUID v4**. Anything shown to the user (review screen, reports) uses a short code derived from the id, never the full UUID |
| 13 | Writes to archived logs | **Through the archived journal** (7 Nov 2026, Stage 3 open question 11, option 1): `edit_entry`, `set_entry_deleted` and `mark_shared` on an archived log append a record to its journal in `journal/archive/` before the database commit, so DS-02 holds for history too, and a rebuild restores those edits and shared times. Only the open log accepts `add_entry` |

### Error type snapshots (decision 5)

An error entry records which error type was tapped, for example "Wrong Response". The configuration owns the list of
error types and lets the facilitator rename or remove them (PRF-08). If an entry holds only a reference to the type,
then once the type is renamed or removed an old log cannot say what the entry was: an old export would change or fail.
A snapshot stores the label as it was at the moment of the tap, next to the reference, so old logs and exports always
read the way they did when written. It costs one extra text column. Decided: store the snapshot. Whether a renamed type should *also* update old
entries is a separate question for Stage 3 and the PRD.

## 11. Findings during implementation

### The draft is written at most 300 ms after it changed (work package 2.6)

Section 2.8 says `DraftStore::set` is "debounced by 300 ms", and decision 3 accepts "at most 300 ms of typing" lost on a kill.
A debounce that restarts its timer on every change would break that: someone typing without a 300 ms pause would write nothing
until they stopped. `DraftStore` therefore arms its timer at the first change that is waiting and does not restart it: changes
inside the window only replace the pending text, and when the timer expires the latest text is written. "Twenty changes
within 300 ms make one write" still holds; a change at 299 ms is covered by the first write and one at 301 ms makes the next.
The kill harness checks the bound under real kills.

### A replayed note takes its draft with it (found by the kill harness)

Section 2.8 says that committing a note clears the draft in the same transaction as the entry, "so a kill cannot leave both the
entry and the draft present". With the decided order (journal first, then the database), a kill between the journal sync and
the commit leaves the note's record, which recovery replays, and a draft that the aborted transaction would have cleared. The
draft is not journalled, so replay left it: after the relaunch the note was in the log and its text was still in the note field.
The harness found it in the crash-point test and in the random kills (one run in four, whenever a draft had been typed before a
note that was in flight). Replaying a note the database did not have now removes a draft whose last write is no later than the
note's capture time, and a rebuild decides the salvaged draft by the same rule; a draft typed after the note is the next note's
and stays. The rule rests on the clocks: a wall clock that stepped back between typing and saving could keep a draft that
should go, or drop one that should stay, and only in the window of a kill.

### Launch cost on a year of use (work package 2.6, finding F13)

The plan proposed a launch budget of 100 ms. `libs/storage/tests/perf/test_launch_cost.cpp` builds a year (365 logs, 80,300
entries, a 34 MB database, a journal each) and measures a warm launch. On the development machine (Windows, optimised build):

| What | Time |
|---|---|
| `PRAGMA quick_check` alone | 120 to 160 ms |
| `PRAGMA integrity_check` alone | about 350 ms |
| Opening the database (settings read back) | 2 ms |
| Reading the tail of 364 archived journals | 22 ms |
| **Launch: `Recovery::run`, default options** | **200 to 225 ms** |
| First launch after an upgrade: `Recovery::run` with the full check | about 2.9 s |

The measurement changed two things. The check of an archived journal's tail used to read up to 128 KiB from each file, in effect
the whole journal; it now reads 4 KiB and widens the window only for a longer last line, so the cost of a launch no longer
depends on how long the journals are. After that the launch is `quick_check` and little else, and SQLite's check of a 34 MB file
cannot be made much faster without changing the schema (a record is about 430 bytes, mostly text ids and their indexes);
memory-mapped reads made it about a third faster and were not adopted, because they change how the database file is read for
every connection.

So **the 100 ms proposal is not met** at one year, and the budget the test enforces is 400 ms for the launch (about twice the
measurement, for a shared runner) on a build without instrumentation, ten times that with sanitizers, coverage or no
optimisation. NFR-04 allows 1.5 s from cold start to a usable board, so a 200 ms check fits, but it grows with the history kept.
**The first launch after an upgrade is the problem:** about 2.9 s, almost all of it comparing every archived log with its
journal (a full check reads every journal). Decision 6 asks only for the full integrity check after an upgrade, which is 350 ms.
Open question for the owner: should that launch compare only the open log, as every launch does, and leave the other logs to a
check after the board is up? Until it is decided the test bounds that launch at 10 s so that a change for the worse is seen.

## 12. Exit gate record

Recorded 7 Nov 2026 for the owner's review (section 8). The gate is **met except for item 2**, which needs seven days of nightly
runs that cannot exist yet: the nightly workflow was merged on 7 Nov 2026 and its first run is at 16:41 UTC that day.
Item 2 can be checked from 14 Nov 2026, after seven green nights.

| | |
|---|---|
| Run | `kill-gate.yml`, [run 42371094476](https://github.com/iainchesworthlabs/icl-error-journaling/actions/runs/42371094476), all 22 jobs green |
| Commit | `43345ce2cdacb31e2af88e3869684e4d179e0f29` (`develop` after work package 2.8) |
| Master seed | `0x5EED2026110700A1` (fixed; iteration `i` depends only on this and `i`) |
| Shape | 10 shards of 1,000 random-kill iterations on Linux (gcc 16, release) and on macOS (release), 20,000 in all |
| Time | Linux shards took under 10 minutes; macOS shards 41 to 46 minutes |

### 1. 10,000 random kills on Linux and on macOS, zero failures: met

| System | Runs | Failures | Acknowledged operations checked | Killed with an operation in flight that was present afterwards | Recoveries that replayed the journal |
|---|---|---|---|---|---|
| Linux | 10,000 | 0 | 2,005,102 | 1,569 | 1,023 |
| macOS | 10,000 | 0 | 2,002,678 | 1,524 | 1,059 |

For every run: recovery reported `clean` or `replayed`, the store equalled the model after `A` or `A + 1` operations on every log
and the draft, `integrity_check` was `ok`, the next operation applied, and a second recovery changed nothing. The in-flight
operation appeared whole in about one run in six and was absent in the rest, as decision 4 allows. Each leg also ran every crash
point K1 to K6 and K9 in both ways, recovery killed at each of its crash points (K7, X3), a migration killed at each of its crash
points (K8, M3), and 60 typed-draft kills, all green; the Linux and macOS shards also ran 20 random kills of recovery and 20 of a
migration each.

### 2. K1 to K9 on every nightly run for the preceding seven days: not yet

The crash points passed in this run on both systems, in every pull request (25 random kills and every crash point on Linux
release and ASan/UBSan) and in the merge queue (Linux debug, ASan/UBSan, TSan and macOS debug). The seven nightly runs do not exist
yet; the nightly's `Report` job opens a `nightly` issue on a red night.

### 3. Every corruption case recovers: met

C1 to C16, J1 to J12 and X1 to X3 are integration tests (`test_recovery.cpp`), run in every build leg of every pull request and in
the queue, including the queue run of the last Stage 2 pull request
([run 42369032129](https://github.com/iainchesworthlabs/icl-error-journaling/actions/runs/42369032129)). K7 and X3 also run as real
process kills, which was the open point of work package 2.4.

### 4. `linux-debug` and `linux-asan-ubsan` green; TSan on the repository and journal tests: met

All legs of the queue run above are green: Linux gcc debug and release, clang ASan/UBSan and TSan, macOS debug and release, with the
tests, and the kill harness at 100 random kills on four legs. The storage tests also ran under AddressSanitizer on the
development machine (Windows, clang): 232 of 232 passed.

### 5. Launch time within its budget on the one-year dataset: met, with the budget changed

The plan's 100 ms was not met and the owner is asked to confirm the 400 ms the test enforces; the figures and the open question
about the first launch after an upgrade are in section 11. The perf test passes in every build leg.

### What the gate does not show

It shows that the application's own ordering and atomicity hold under process kills. It does not show behaviour after loss of power,
or that `F_FULLFSYNC` flushes an iPhone's cache (a process kill leaves the system to write what the process handed it), or file
protection and behaviour before first unlock (Stage 5, on a device). Nothing here has run on a device. Windows ran only on the
development machine.

### What the gate found on the way

- The kill harness found a defect in recovery on its first merge-queue run: a note replayed from the journal left the draft it
  was made from (section 11). Fixed in #44, pinned to the seed that failed in the queue
  (`0x35b5989d25a5fd6c`, iteration 5, killed after acknowledgement 364) by a test that kills at that operation every time.
- In the gate's first Linux shards, the random kills of recovery and of a migration never landed inside the run (they finish in
  milliseconds there and the delay was drawn up to 150 ms). The delay is now drawn on a log scale from 100 microseconds to 200
  milliseconds, and the results of these tests are in the run's summary. The crash-point cases, which kill recovery and a migration
  at each point exactly, were not affected. The first nightly with the change is the evidence for these two random cases.
