# Stage 3: application services and models

Task breakdown and test list for Stage 3 of the [implementation plan](../implementation-plan.md). Each work package is
one pull request into `develop`. Requirement IDs refer to the [PRD](../prd.md). The screens and the behaviour they show
are in the [design package](../design/README.md).

**Exit gate:** a headless test drives a full day (header, taps, notes, a version change, review edits, share, new day)
through the services and produces the golden export.

**Status:** approved by the owner; implemented in work packages 3.1 to 3.12. Exit-gate result: see "Exit gate record" at the end of this page.

## Scope and conventions

Stage 3 is the thin QObject layer between the core and storage on one side and QML on the other. It adds no QML, no
window and no screen; the Stage 4 components bind to what is built here. It adds no dependency (`Qt6::Test` is part of the Qt install the project already uses).

- Sources are in `apps/app/src/viewmodel/` and are built as a static library `ej_viewmodel` that `ej_app` links, so the
  tests can link it too (an executable cannot be linked). Tests are in `apps/app/tests/viewmodel/`. Both directories are
  added with `file(GLOB ... CONFIGURE_DEPENDS)`, as `libs/ios_bridge` already does, so a package adds its files without
  editing a shared `CMakeLists.txt` (this is what lets packages run in parallel, see the work order).
- `ej_viewmodel` links `ej_core`, `ej_storage`, `ej_ios_bridge` (the Qt-free `platform_services` interface only) and
  QtCore. It does not link Qt Quick or QtGui, so every test runs without a display. The one thing that needs QtGui, the
  clipboard, sits behind an interface and its implementation is in `apps/app/src/`.
- Casing. Class names are `PascalCase`. Anything QML can see is camelCase: `Q_PROPERTY` names, signals, slots,
  `Q_INVOKABLE` methods and model role names. Everything else follows AGENTS.md: the C++ `READ` and `WRITE` functions
  behind a property are `snake_case` (`entry_count()`, `set_draft()`), members are `m_PascalCase`, enumerators
  (including `Q_ENUM`) are `UPPER_SNAKE_CASE`, and files are `snake_case`. The overrides Qt forces on a model
  (`rowCount`, `data`, `roleNames`) keep Qt's names; clang-tidy exempts overrides, and if it does not here, one targeted
  `NOLINT` with that reason is used. This follows `libs/ios_bridge`, where `shareAvailable` is the property and
  `share_available()` its reader.
- A signal that only C++ code listens to is not a Qt signal. The services tell each other about changes through plain
  C++ observer interfaces (`LogObserver`, `ConfigurationObserver`), called synchronously on the GUI thread. Qt signals
  are only the ones QML uses, so every signal in a sketch below is QML-facing and camelCase.
- Recoverable errors are `std::expected` (`ej::Result`) inside the layer. What QML sees is a `bool` (accepted or not)
  from an invokable, plus a signal for a failure that arrives later. Nothing throws.
- User-visible text produced in C++ goes through `tr()` (NFR-11). The export wording is not translated: it comes from
  the export renderer only (see "One source of wording").
- Tests use Catch2 through CTest, as `docs/testing.md` and standard section 6 say. The implementation plan's phrase
  "Qt Test cases" is read as "tests of Qt classes": the cases link `Qt6::Test` for `QSignalSpy` only, and the test
  executable owns a `QCoreApplication` as `libs/ios_bridge/tests` does (open question 3). Labels: `unit` for tests on
  the in-memory repository and fakes, `integration` for the full-day test against the real store, `perf` for the
  budgets in the threading section.
- Sketches show shape, not final signatures. The Stage 1 types are the merged headers under
  `libs/core/include/error_journaling/`. The Stage 2 types are the proposals in `stage-2.md` and are marked as such
  until 2.1 merges.

## Threading and the order of feedback

Standard section 7.6 says nothing that blocks runs on the GUI thread. A write is a journal fsync plus a database commit
(`F_FULLFSYNC` twice on a phone), which can take tens of milliseconds. DS-01 (reworded, decision 2) says no confirmation
that the entry is saved is shown before the commit; the impact haptic is not such a confirmation and fires on touch-down.
Both rules are met by one structure.

```text
GUI thread                                  storage thread (one std::jthread)
----------                                  ---------------------------------
touch-down -> impact haptic (QML, Stage 5; not a service call)
tap -> validate against the mirror
     -> read clock, make ids
     -> StorageGateway::run(job) ---------> job: repository.add_entry(...)   (journal fsync, then commit)
                                                 |
     <------- queued call with the result -------+
apply the entry to the mirror
tell the observers (models update)
FeedbackSink: confirmation   <- only here, only after a successful commit (count, toast, Last bar; success haptic for a note)
```

- The repository, the draft store and the configuration store are created on the storage thread and only ever used
  there. A `QSqlDatabase` connection belongs to one thread, and TSan then has nothing to find: the two threads exchange
  values by queued calls and share no mutable state.
- Each service keeps an in-memory copy of what it shows (the mirror: a `Log` by value, a `Configuration` by value).
  Models read the mirror on the GUI thread and never wait for I/O. The mirror changes only when a commit has succeeded,
  so the screen cannot show an entry the store does not have.
- `StorageGateway` is the only way to reach the store:

  ```cpp
  struct StorageContext { LogRepository& m_Logs; ConfigStore& m_Config; };   // LogRepository: the Stage 2 proposal

  class StorageGateway
  {
  public:
      using Work = std::move_only_function<void(StorageContext&)>;   // runs on the storage thread, in submission order
      using Completion = std::move_only_function<void()>;            // runs on the GUI thread, after its Work
      virtual ~StorageGateway() = default;
      virtual void post(Work work, Completion completion) = 0;
      virtual void drain() = 0;     // blocks until the queue is empty; for quit only, never during use

      // Typed wrapper: `work` returns an R, which `done` receives on the GUI thread.
      template <typename R>
      void run(std::move_only_function<R(StorageContext&)> work, std::move_only_function<void(R)> done);
  };
  ```

  `ThreadedStorageGateway` is the production one (`std::jthread`, a mutex-guarded queue, results returned with
  `QMetaObject::invokeMethod(receiver, ..., Qt::QueuedConnection)`, as standard 7.6 describes). `ManualStorageGateway`
  is the test one: jobs wait in a queue until the test calls `run_next()` or `run_all()`, so a test can look at the
  state between "submitted" and "committed". Its completion runs in the same call, so tests need no event loop.
- One command is in flight per document at a time; the rest wait in a FIFO of at most 64. A second tap that arrives
  while the first is committing is queued and committed after it, in order. A full queue rejects the command (the
  invokable returns `false`, `writeFailed` is emitted) and nothing is dropped silently.
- The time and the ids of a command are taken on the GUI thread when the command is accepted, not when it commits, so
  an entry carries the instant of the tap even if the commit is late. Ids are generated by the caller, which is what
  makes replay idempotent (stage 2, decision 12).
- Confirmation is a rule of the C++ layer, not of QML: `FeedbackSink` (`entry_recorded()`, `note_saved()`; the production
  adapter calls `platform_services::fire_haptic` for the note's success haptic only) is called from the completion
  handler of a successful write and nowhere else. On a failed write no count changes, no toast appears, no success
  haptic fires, and `writeFailed` is emitted.
- Three things answer the finger and are left to QML, not to the services (decision 2): the tile's press animation, the
  impact haptic on a tile, which fires on touch-down as an acknowledgement of touch and so before the commit (Stage 5,
  5.2 and 5.7), and `prepareHaptics()`. The count, the toast and the "Last" bar are confirmation that the entry is saved
  and wait for the commit.
- Shutdown: `drain()` is called once from `aboutToQuit`, so a write that was queued is finished. On iOS the lifecycle
  work of Stage 5 also has to ask for background time for a commit that is in flight when the app goes inactive
  (listed under "What cannot be verified here").

## One source of wording

REV-01 requires the review screen to read exactly like the export. The services therefore never build a log line
themselves. A timeline row, the "Last" bar, the edit sheet's preview, the toast's line, Copy and Share all call the same
renderer functions with an `ExportContext` built by one function, `ExportContextBuilder`, from the configuration in
force. The sentences around the lines ("Mute Delay logged at 10:20", "7 entries") are the only text written in C++.

## Dependencies that must exist before the packages that use them

Stage 3 sits on two stages that are not finished. On `develop` today: Stage 1 has `Log`, `HeaderHistory`, the time
types, `DayPrompt` and the header-only export renderer; Stage 2 is the empty `ej_storage` placeholder.

| Needed | From | Used by | State on `develop` |
|---|---|---|---|
| `render_export`, `render_entry_line` and the full `ExportContext` (time format, totals, zones, name lookups) | Stage 1, 1.5 | 3.2, 3.7, 3.8 | skeleton: header block only |
| `Configuration`, `import_configuration`, `export_configuration` | Stage 1, 1.6 | 3.5 and most others | pull request 16 open |
| `LogRepository`, `StorageError`, `Clock`, `RecoveryReport`, `LogId`, `MemoryLogRepository` | Stage 2, 2.1 | all | not started |
| `DraftStore` | Stage 2, 2.6 | 3.3 | not started |
| `SqliteLogRepository`, `Recovery`, the journal | Stage 2, 2.2 to 2.4 | 3.11 only | not started |
| The Stage 2 exit gate | Stage 2, 2.8 | 3.11 (open question 1) | not passed |

The repository calls Stage 3 needs, so that 2.1 can be checked against this list before it merges (names are proposals
and follow `stage-2.md`; the starred ones are not in that page yet):

- `open_log()` returning the log, its id and `last_shared_utc`; `create_log(id, header)`.
- `* start_new_log(old_id, new_id, header)`: archive and create as one unit. Stage 2 says the pair is one unit; Stage 3
  needs it to be one call so that a failure between the halves is not Stage 3's problem.
- `list_logs()` returning summaries (`* LogSummary`: id, local date, facilitator, unit number, location text, live
  entry count, `last_shared_utc`), and `load_log(id)`.
- `add_entry(log_id, entry)` where a note entry also clears the draft in the same transaction (stage 2, W5);
  `edit_entry`, `set_entry_deleted`, `correct_header`, `mark_shared`; `get_draft` and the draft store's `set` and
  `flush`; `hard_delete_log(id, DeleteConfirmation)`.
- `* edit_entry`, `* set_entry_deleted` and `* mark_shared` on an archived log, with a journal story (open question 11).
- Two Stage 1 additions, both small: a snapshot of the error type's name on `ErrorEntry` that the renderer prefers
  (stage 2 decision 5 stores it; Stage 1's `ErrorEntry` has no field for it, and the renderer resolves names by id; see
  open question 7), and `LogId` in `ids.hpp`.

## Time, ids and the clock

Four small interfaces make every time-dependent rule testable without sleeping. They are in `environment.hpp` and are
passed around as one `Environment` struct of references:

```cpp
// Wall clock: the `Clock` of Stage 2 (a proposal there). now() -> UtcMillis.
class Scheduler            // monotonic timers; the undo window and nothing else in Stage 3 uses them
{
public:
    using Callback = std::move_only_function<void()>;
    virtual ~Scheduler() = default;
    virtual TimerId after(std::chrono::milliseconds delay, Callback) = 0;   // one shot, GUI thread
    virtual void cancel(TimerId) = 0;
    virtual std::chrono::milliseconds monotonic_now() const = 0;
};
class IdGenerator { public: virtual Uuid next() = 0; };         // production: two QRandomGenerator::system() words
class DeviceZone { public: virtual std::string current() const = 0; };   // IANA name; production: QTimeZone::systemTimeZoneId
struct Environment { Clock& m_Clock; Scheduler& m_Scheduler; IdGenerator& m_Ids; DeviceZone& m_Zone;
                     const TimeZoneProvider& m_Zones; };
```

The undo window is on the monotonic `Scheduler`, so a change of the wall clock neither extends nor cuts it. The tests'
`TestClock` implements `Clock`, `Scheduler` and `monotonic_now()` together; `advance(ms)` moves both and fires due
timers in order, and `step_wall_back(ms)` moves only the wall clock.
Stage 2.6's `DraftStore` has its own injectable timer; if it defines a `Scheduler`-shaped interface first, 3.1 adopts it
instead of adding a second one.

## Components

### LogSession (BRD-02, NOTE-01, HDR-07, DAY-02; also HDR-01, HDR-05, HDR-06, DAY-03, NOTE-03, NOTE-04, DS-03, DS-04)

Owns the open log. It is a QObject facade over a `LogDocument`, which is the non-QML class that does the work (see
"LogDocument" below) so that a past log opened from History can be edited by the same code.

```cpp
class LogSession : public QObject
{
    Q_OBJECT
    Q_PROPERTY(State state READ state NOTIFY stateChanged)
    // The header in force now (HeaderHistory::at(now)), for the header card. The export's header block is the start values.
    Q_PROPERTY(QString facilitator READ facilitator NOTIFY headerChanged)
    Q_PROPERTY(QString unitText READ unit_text NOTIFY headerChanged)         // "UNIT-27", or empty
    Q_PROPERTY(QString locationText READ location_text NOTIFY headerChanged) // the name, resolved from the id
    Q_PROPERTY(QString versionText READ version_text NOTIFY headerChanged)
    Q_PROPERTY(QString dateText READ date_text NOTIFY headerChanged)         // "Sat 7 Nov", QLocale English (Australia)
    Q_PROPERTY(QStringList missingFields READ missing_fields NOTIFY headerChanged)   // HDR-06 banner: keys of empty fields
    Q_PROPERTY(QString draft READ draft WRITE set_draft NOTIFY draftChanged)        // NOTE-03
    Q_PROPERTY(int pendingWrites READ pending_writes NOTIFY pendingWritesChanged)
    Q_PROPERTY(QString recoveryNotice READ recovery_notice CONSTANT)               // DS-04, empty when nothing was recovered
public:
    enum class State { LOADING, NEEDS_SETUP, OPEN, FAILED };  Q_ENUM(State)

    Q_INVOKABLE bool recordError(const QString& errorTypeId);                         // BRD-02
    Q_INVOKABLE bool addNote(const QString& text);                                    // NOTE-01, free text
    Q_INVOKABLE bool addTemplatedNote(const QString& templateId, const QString& value);   // NOTE-04
    Q_INVOKABLE QString validateHeaderValue(const QString& fieldKey, const QString& text) const;   // "" when valid
    Q_INVOKABLE bool correctHeader(const QString& fieldKey, const QString& text);     // HDR-07 Correct: no entry
    Q_INVOKABLE bool changeHeader(const QString& fieldKey, const QString& text, qint64 atUtcMs = 0);  // Change: an entry
    Q_INVOKABLE bool startNewLog();                                                   // DAY-01, DAY-02
    Q_INVOKABLE void flushDraft();                                                    // Stage 5 calls it on going inactive

signals:
    void entryRecorded(const QString& entryId);       // after the commit; BRD-07 opens the note sheet from it
    void newLogStarted();                              // Stage 4 opens Today's details
    void writeFailed(const QString& operation, const QString& message);
};
```

Field keys are the core's `header_field_key` strings (`unit_number`, `location`, `software_version`). A location value
is the location's id text (a configured location) or free text (Other), which is what `set_header_field` already
accepts.

Behaviour:

- **Loading.** The constructor takes a `StartupResult` made on the storage thread by the composition root (recovery
  has already run). `load()` asks for `open_log()`. No open log and a valid configuration: it creates today's log with
  the facilitator from the preferences and today's local date (HDR-01) and goes to `OPEN`. No open log and no valid
  configuration: `NEEDS_SETUP`, and the first log is created when `SettingsModel` becomes configured. The board shows its
  placeholder while `LOADING`, so the first frame does not wait for the store (NFR-04, standard 7.6).
- **Taps and notes** are accepted in every header state (HDR-06). `recordError` returns `false` and writes nothing for a
  type that is unknown or retired, or when the state is not `OPEN`. `addNote` trims the text; empty text is rejected and
  text over 10,000 characters is rejected (stage 2, decision 7). A templated note stores the template id and the value
  and is rendered by `render_note` only by the renderer.
- **Header.** `validateHeaderValue` is the inline check of HDR-04 and HDR-02: it calls `UnitNumber::parse`,
  `SoftwareVersion::parse` or the location rule and returns the plain message for the field's own error. `correctHeader`
  calls `ej::correct_header` on the mirror and writes `correct_header`; it adds no entry. A first fill of an empty field
  is also a Correct (Today's details). `changeHeader` calls `ej::change_header` with a new entry id and the given
  instant (default now) and writes an entry; a value equal to the one in force is rejected with `UNCHANGED`.
- **Draft.** `set_draft` updates the property at once and forwards the text to the draft store through the gateway; the
  300 ms debounce stays in `DraftStore` (stage 2, 2.8). `addNote` clears the draft in the same transaction. If the
  facilitator kept typing while the note was committing, the newer text stays. At load the draft comes from the store
  (DS-03).
- **New log.** `startNewLog` queues behind pending writes, then calls `start_new_log` with a header holding only the
  facilitator and today's local date (DAY-02, HDR-05). On success the document is replaced, observers get `REPLACED`,
  and `newLogStarted()` fires. The old log is archived, never deleted (DS-05). The draft is not touched.
- **Recovery notice.** `recovery_notice(const RecoveryReport&)` is a pure function that turns the report into one plain
  sentence (counts only, no entry text): empty for `clean`, and one wording each for `replayed`, `rebuilt`,
  `partial_loss` and `unrecoverable`.

Threading: as described above. `recordError`, `addNote` and the header invokables validate on the GUI thread, make the
entry, queue one gateway job and return at once. Nothing in this class touches the store directly.

Storage: `open_log`, `create_log`, `start_new_log`, `add_entry`, `correct_header`, `edit_entry` (header-change
correction), `get_draft`, and the draft store's `set` and `flush`. Renderer: none directly. The header card shows values,
not export lines, and the lines and the toast text come from `TimelineModel` and `UndoController`. `LogSession` also
implements `OpenLogSource` (3.1), so the models follow a new log without depending on this class.

Tests (`unit`, in-memory repository, `ManualStorageGateway`, `TestClock`):

- Load: no open log and a valid configuration creates a log with the preference facilitator, today's date in the device
  zone and three empty fields; no configuration gives `NEEDS_SETUP` and creates nothing; an existing open log is opened
  as it was, with its draft.
- Tap: before the job runs there is no entry, no count and no toast; after `run_next()` the entry exists with the time
  of the tap (the clock is advanced between tap and commit and the entry keeps the earlier time), the device zone, a
  generated id, and exactly one `entry_recorded` call, after the commit in the `EventLog`.
- A failing commit (fault injected in the repository) gives no entry, no count, no toast, one `writeFailed`, and the next tap
  works. A full queue (65 taps without running the gateway) rejects the 65th.
- Unknown and retired type ids are rejected; a tap while `LOADING` or `NEEDS_SETUP` is rejected.
- Several taps submitted before any commit are committed in order and the timeline order matches the tap order.
- Header: empty header lists three missing fields and taps still work; `1.4`, `1.4.2-beta`, `027` are refused with the
  core's message and nothing is written; Correct rewrites the value and adds no entry; Change adds an entry, the value in
  force changes, the start value does not; a Change to the value already in force is refused; a Change at an earlier
  instant than an existing entry puts the entry before it in time order.
- Notes: empty and 10,001-character text are refused; a templated note stores the template id and renders through the
  pattern; a note commit clears the draft in the same repository call (the fake records one call, not two); typing
  during the commit keeps the newer draft; every `set_draft` is forwarded and `flushDraft` forwards a flush.
- New log: archives the old log with all its entries, creates one with only the facilitator and today's date, leaves
  no moment in the fake repository where there are zero or two open logs, and keeps the draft. A failure keeps the old
  log open. The header edits of the new log do not alter the archived one.
- A date in 1970 and a time zone change between taps are stored as given.
- `recovery_notice`: one case per outcome, no entry text in any of them.

### LogDocument and the registry (internal; supports LogSession, TimelineModel, HistoryModel, ShareService)

The work of owning a log lives in a plain C++ class, so the open log and a past log opened for review behave the same.
It is not QML-facing.

```cpp
struct LogChange
{
    enum class Kind { LOADED, REPLACED, ENTRY_ADDED, ENTRY_EDITED, ENTRY_DELETED, ENTRY_RESTORED,
                      HEADER_CORRECTED, HEADER_CHANGED, SHARED };
    enum class Origin { USER, UNDO };
    Kind m_Kind;
    Origin m_Origin = Origin::USER;
    std::optional<EntryId> m_Entry;
};
class LogObserver { public: virtual void on_log_changed(const LogDocument&, const LogChange&) = 0; };

class LogDocument
{
public:
    LogId id() const;
    const Log& log() const;                                  // the mirror
    std::optional<UtcMillis> last_shared() const;
    bool archived() const;
    void add_observer(LogObserver&);   void remove_observer(LogObserver&);

    // Each call validates against the mirror, queues one gateway job and returns. `done` runs on the GUI thread
    // after the commit or the failure; observers are told before `done` and only after a commit.
    using Done = std::move_only_function<void(const Result<void, StorageError>&)>;
    Result<void, DocumentError> add_entry(Entry, Origin, Done);
    Result<void, DocumentError> edit_entry(EntryId, Entry, Done);
    Result<void, DocumentError> set_deleted(EntryId, bool deleted, Origin, Done);
    Result<void, DocumentError> correct_header(HeaderField, std::string, Done);
    Result<void, DocumentError> change_header(HeaderField, std::string, Timestamp, EntryId, Done);
    Result<void, DocumentError> correct_header_change(EntryId, std::string, Done);
    Result<void, DocumentError> mark_shared(UtcMillis, Done);
};
```

`DocumentError` is `NOT_READY`, `QUEUE_FULL` or `REJECTED` (with the core's `Log::Error` or `HeaderError` inside).
A command is validated twice. At submission it is checked against the mirror, which gives the invokable its immediate
`bool` and the inline messages. It is checked again when it reaches the front of the queue, against the mirror as it
then is, and only then is its job queued: commands run one at a time, so the mirror and the store cannot disagree. A
command that fails the second check reports through its `Done` and `writeFailed`, and the queue carries on.
`DocumentRegistry` hands out documents by id (the open log strongly held by `LogSession`, an archived log loaded on
demand through `load_log` and held weakly), so History, Review and Share work on one document per log and cannot
overwrite each other's edits. `ExportContextBuilder` builds the renderer's context from the configuration in force.
`export_text(const Log&, const ExportContext&)` wraps `render_export`, so Copy, Share and the golden test call one
function. `OpenLogSource` (an interface from 3.1, implemented by `LogSession`) gives the models the current open
document and tells them when a new log replaces it.

Tests (`unit`):

- The mirror changes only after the commit; observers are called after the mirror and never for a failed command.
- Commands commit in submission order; a validation failure of a queued command reports without blocking the queue.
- The queue limit; `Done` runs exactly once; a document destroyed with jobs in flight does not call back (guard).
- `REPLACED` and `LOADED` are one notification each, however many entries the log has.
- The registry returns one document per id, reloads after the last owner lets go, and refuses an unknown id.
- `ExportContextBuilder`: name lookups resolve live, retired and renamed types, locations and templates; an unknown id
  yields the empty view the renderer reports as an error; the unit prefix and the time format come from the
  configuration; building the context twice from an unchanged configuration renders identical text.

### UndoController (BRD-04, REV-03)

A five-second window after the last action, a repeat count for the same tile, and undo of the most recent action only.
It watches every live `LogDocument` through `LogObserver`. There is one toast slot for the whole app: a new action
replaces the toast, and the earlier action can then no longer be undone (it stays committed).

```cpp
class UndoController : public QObject
{
    Q_OBJECT
    Q_PROPERTY(bool active READ active NOTIFY toastChanged)
    Q_PROPERTY(QString text READ text NOTIFY toastChanged)         // "Mute Delay logged ×2 at 10:20"
    Q_PROPERTY(int repeatCount READ repeat_count NOTIFY toastChanged)   // 1 when not a repeat
    Q_PROPERTY(bool undoing READ undoing NOTIFY toastChanged)      // a press is being committed
    Q_PROPERTY(int generation READ generation NOTIFY toastChanged) // +1 whenever the window restarts, so QML restarts its bar
    Q_PROPERTY(int windowMs READ window_ms CONSTANT)               // 5000
public:
    Q_INVOKABLE bool undo();
    Q_INVOKABLE void dismiss();
    void observe(LogDocument&);   void stop_observing(LogDocument&);
    void set_note_undo_target(std::move_only_function<void(const std::string&)>);   // returns note text to the composer
};
```

Rules, taken from BRD-04 and the approved prototype (`Main.dc.html`):

- A committed error tap (origin `USER`) opens or refreshes the toast. If the toast is still showing and the previous
  action was a tap on the same error type in the same document, the repeat count goes up by one; otherwise it is 1.
  The text is `"<name> logged at <time>"`, or `"<name> logged ×N at <time>"` for N above 1, with the time of the latest
  tap in the entry's own zone and the user's time format, from the renderer's time formatting.
- Each commit restarts the five-second window on the monotonic scheduler. It starts when the commit completes (when the
  confirmation is shown), not when the finger landed.
- `undo()` soft-deletes the entry of the most recent tap (`set_deleted(id, true)`, origin `UNDO`) and only that one.
  The repeat count does not step down: the toast closes when the undo commits (open question 5). An undo commit is not
  itself undoable and does not open a toast. A second press while `undoing` is ignored.
- A committed delete from the review screen (REV-03) opens the toast `"Entry deleted"`; `undo()` restores it
  (`set_deleted(id, false)`). The entry is committed as deleted at once and stays deleted after the window; it is
  never purged (open question 6).
- A committed note opens `"Note added at <time>"` and its undo soft-deletes the note and hands the text back to the
  composer through the note-undo target (open question 4).
- When the window ends, or `dismiss()` is called, or the document is `REPLACED` (a new log started), the toast closes
  and the entry is no longer undoable.
- If the undo commit fails, `writeFailed` is emitted by the document, the toast stays until its window ends, and the
  press can be repeated.

Threading: GUI thread only. The write goes through the document's gateway. Storage: `set_deleted` only.

Tests (`unit`, `TestClock`):

- Window: a tap, then undo at 4,999 ms removes it; at 5,000 ms the toast has closed and `undo()` returns `false`.
  A slow commit (the clock advances 300 ms between tap and commit) starts the window at the commit.
- Wall clock: stepping the wall clock back or forward by an hour inside the window changes nothing.
- Repeat: A, A at 1 s gives ×2; a third A at 5.9 s (4.9 s after the second) gives ×3 because the window slid; a fourth A
  at 5 s idle gives ×1; A then B gives B ×1 and A is no longer undoable; A, B, A gives ×1; a note between two taps on
  the same type resets the count; the same type in two different documents does not count as a repeat.
- Undo of ×3 removes only the most recent entry (count 2 remains), closes the toast, and the next tap shows ×1.
- Taps queued behind a slow commit show ×2 only when the second commit completes, in order.
- Undo pressed twice quickly commits once; undo racing the end of the window either commits (pressed before) or
  does nothing (after), never both; an undo commit that fails keeps the toast and allows a retry.
- The text uses the time format preference (12-hour with seconds, lower-case meridiem) and the entry's own zone.
- Review delete: toast, undo restores and the count returns; no undo after expiry and the entry is still deleted.
- A new log (`REPLACED`) closes the toast; undo of an archived-log edit works inside its window the same way.
- The toast opens only after the commit: with the gateway held back, `active` is false.
- Undo entries never produce a toast, and no haptic is fired for an undo.

### BoardModel (BRD-01, BRD-05, BRD-08; BRD-06 through the picker model below)

A list model of exactly 12 rows: positions 1 to 11 and then Other. The row count never changes, so the grid cannot jump.

```cpp
class BoardModel : public QAbstractListModel     // observes the open LogDocument and the configuration
{
    Q_OBJECT
    Q_PROPERTY(int totalCount READ total_count NOTIFY countsChanged)    // live error entries in the open log
public:
    enum Role { TYPE_ID = Qt::UserRole + 1, LABEL, COLOUR_SLOT, COUNT, HAS_COUNT, IS_OTHER, IS_EMPTY, ACCESSIBLE_NAME };
    // roleNames(): typeId, label, colourSlot, count, hasCount, isOther, isEmpty, accessibleName
    int rowCount(const QModelIndex& = {}) const override;      // 12
    QVariant data(const QModelIndex&, int role) const override;
    QHash<int, QByteArray> roleNames() const override;
};
```

- **Rows.** Row `p-1` shows the live error type whose board position is `p`. A position with no type (fewer than 11 are
  configured, or its type was retired) is an empty tile: `isEmpty` true, empty label, count 0, and `recordError` is never
  called for it. Row 11 is Other: `isOther` true, `typeId` empty, neutral colour.
- **Colour.** A colour belongs to the type, not the position (BRD-08, `tokens.json`). The model exposes `colourSlot`
  1 to 11 and 0 for Other; the Stage 4 token singleton turns a slot into a colour for light and dark mode. The slot is
  the type's index in the master list (retired types keep their index) modulo 11, plus one, which is the token rule
  "types beyond 11 reuse slots 1 to 11 in order". Stage 1.6 dropped the stored colour, so this is derived (open
  question 8). Reordering the board therefore never changes a type's colour.
- **Counts.** `count` is `Log::count_for(type)` for the open log, deleted entries excluded. Other's count is every live
  error entry whose type is not currently on the board, which includes retired types and types moved off the board
  (BRD-05). A badge shows when `count > 0`. `accessibleName` is `"<label>, <n> today"` or just the label, as the design's
  aria text does.
- **Updates.** One commit changes at most the tile of its type and Other, so the model emits one `dataChanged` over the
  smallest row range that holds them, roles `COUNT` and `HAS_COUNT` and `ACCESSIBLE_NAME`. A new log, a day rollover or
  a configuration change emits one `dataChanged` over rows 0 to 11 with every role (standard 7.6: one signal per batch).
  The model never resets, so a running press animation on a tile is not interrupted.

Threading: GUI thread, no I/O. Storage: none; it reads the mirror. Renderer: none.

Tests (`unit`):

- 12 rows always; the roles of each row for the sample configuration; empty tiles for a configuration with 9 types.
- A tap on tile 3 raises its count and leaves Other at 0; a tap on a type not on the board raises Other only; Other
  counts a retired type that had entries; a type moved off the board moves its entries into Other in one signal.
- Undo (soft delete) lowers the count; restore raises it; an edit that changes an entry's type moves one count from one
  row to another with one signal.
- Reordering the board changes `typeId`, `label` and `count` at the positions but not the `colourSlot` of any type;
  a 12th and 13th type reuse slots 1 and 2; Other is always slot 0.
- Signal counts: one `dataChanged` per commit, one for a new log, one for a configuration change, none for a failed
  write; no `modelReset` or `layoutChanged` ever.
- A different log type (the archived log of a past day) is not counted: only the open document drives the board.

#### ErrorPickerModel (BRD-06; an addition to the table)

The table has no home for the Other sheet, so it is a second small model in the same package. The sheet lists every
live (not retired) error type in the master list with a search field, in two sections: "Not on your board" and "On your
board", with today's counts.

```cpp
class ErrorPickerModel : public QAbstractListModel
{
    Q_OBJECT
    Q_PROPERTY(QString filter READ filter WRITE set_filter NOTIFY filterChanged)
public:
    enum Role { TYPE_ID = Qt::UserRole + 1, LABEL, SECTION, COUNT, COLOUR_SLOT };   // typeId, label, section, count, colourSlot
    Q_INVOKABLE QString addType(const QString& name);    // returns the new type's id, or "" when refused; PRF-04
};
```

- The filter is a case-insensitive substring match on the label (`QString::toCaseFolded`), applied in the model; a change
  rebuilds the list with one `modelReset`. `section` is `OFF_BOARD` or `ON_BOARD`.
- `addType` forwards to `SettingsModel` (master list, saved); the new type appears under "Not on your board". It does not
  record an entry: the facilitator taps it as for any other type (open question 12).
- Picking a type is `LogSession::recordError(typeId)`, the same path as a tile.

Tests (`unit`): sections and counts for the sample configuration; the filter (case, accents, empty result, a filter that
matches only an on-board type); retired types are absent; a name that is empty, duplicate (case-insensitive) or over
200 characters is refused; `addType` shows the type at once after the settings write commits; one `modelReset` per filter
change; counts follow commits.

### TimelineModel (REV-01, REV-02, REV-03, REV-04)

Entries of one log in time order, for the "Last" bar and the review screen. The same class serves the open log and a
past log opened from History; `bind(document)` chooses which. Every string it shows comes from the export renderer.

```cpp
class TimelineModel : public QAbstractListModel
{
    Q_OBJECT
    Q_PROPERTY(int entryCount READ entry_count NOTIFY summaryChanged)        // live entries: "7 today"
    Q_PROPERTY(QString lastLine READ last_line NOTIFY summaryChanged)        // render_entry_line of the latest entry
    Q_PROPERTY(QString headerText READ header_text NOTIFY summaryChanged)    // render_header: the start values
    Q_PROPERTY(QString titleText READ title_text NOTIFY summaryChanged)      // "Sat 7 Nov · 7 entries"
    Q_PROPERTY(bool ready READ ready NOTIFY summaryChanged)
public:
    enum Role { ENTRY_ID = Qt::UserRole + 1, TIME_TEXT, LINE_TEXT, FULL_LINE, KIND };   // entryId, timeText, lineText, fullLine, kind
    Q_INVOKABLE bool copyLine(int row);                    // REV-04: puts fullLine on the clipboard
    Q_INVOKABLE bool copyAll();                            // REV-04: puts the whole export on the clipboard
    Q_INVOKABLE bool deleteEntry(const QString& entryId);  // REV-03: soft delete; UndoController shows the toast
    Q_INVOKABLE QVariantMap entryForEdit(const QString& entryId) const;           // REV-02: what the sheet shows
    Q_INVOKABLE QString previewLine(const QString& entryId, const QVariantMap& edit) const;   // "In the shared log"
    Q_INVOKABLE bool saveEntry(const QString& entryId, const QVariantMap& edit);  // REV-02 Save
    Q_INVOKABLE int rowOf(const QString& entryId) const;
};
```

- **Rows.** One row per live entry in `Log::sorted_entries()` order (UTC time, ties by insertion). `fullLine` is
  `render_entry_line`; `timeText` is `format_time` of the entry's local time, and `lineText` is the rest of the line. The
  model checks `timeText + " " + lineText == fullLine` when it builds a row, so the two columns of the design cannot drift
  from the export. `kind` (`error`, `note`, `header_change`) is for routing the edit sheet only: REV-01 says no colour
  coding, and nothing here supplies one.
- **Edit.** `entryForEdit` returns the fields of the sheet: `kind`, `errorTypeId`, `noteText` (the free text, or the
  template value for a templated note, with `templateId`), `attachedNote`, `localSeconds` (seconds since local midnight
  of the entry's date) and `zone`. `saveEntry` takes the same keys. The time becomes a UTC instant through
  `TimeZoneProvider::to_utc` in the entry's own zone, so an edit inside a spring-forward gap or a repeated hour behaves as
  Stage 1 defined. A header-change entry can have its time and its new value edited (`correct_header_change`). The write is
  `Log::replace` semantics: the id and the deleted flag stay, `m_EditedUtc` is set from the clock, and the row moves if
  the time moved. `previewLine` renders the edited entry on a scratch copy of the log with the same renderer and writes
  nothing.
- **Delete.** `deleteEntry` is `set_deleted(id, true)` with origin `USER`; the row leaves the model after the commit and
  `UndoController` opens the toast.
- **Copy.** `copyLine` and `copyAll` call the injected `Clipboard`. Neither marks the log as shared (open question 10).
  `copyAll` is the same function as the text `ShareService` sends (`export_text`), so the two cannot differ.
- **Updates.** `ENTRY_ADDED` inserts one row (`beginInsertRows`); `ENTRY_DELETED` removes one; `ENTRY_EDITED` is a
  `dataChanged` on the row, or a `beginMoveRows` when the time changed its place; `HEADER_*` is one `dataChanged` over every
  row (a correction rewrites the "old" side of later change lines); `LOADED` and `REPLACED` are one `modelReset`. A
  configuration change that affects wording (time format, unit prefix, an error type name) is one `dataChanged` over every
  row, with the strings re-rendered first. Rendered strings are cached per row, so `data()` does no formatting.

Threading: GUI thread, no I/O. Rendering a few hundred lines is microseconds of CPU work; if the `perf` test shows a
long log (the 500-entry cap in stage 2) over its budget, rendering moves to the storage thread's result batch without
changing this interface. Storage: edits and deletes through the document. Renderer: `render_entry_line`, `render_header`,
`render_export`.

Tests (`unit`):

- Rows follow time order; an entry edited to an earlier time moves with one `rowsMoved`; ties keep insertion order;
  deleted entries are absent and come back on restore at their place.
- Every row satisfies `timeText + " " + lineText == fullLine`, in 24-hour, 12-hour upper and lower case, and with
  seconds; `fullLine` equals the matching line of `render_export` for every entry of the golden scenario.
- `lastLine` is the last row and returns to the previous line after an undo; empty log gives an empty `lastLine` and
  `entryCount` 0; `headerText` equals `render_header`; the start values stay when a change is made.
- Edit: change the error type, the note, the attached note (set and clear), and the time; each re-renders and re-sorts; a
  time edited into the missing hour of 2026-10-04 and into the repeated hour of 2026-04-05 (Melbourne) renders as in the
  Stage 1 goldens; `previewLine` matches the saved line exactly and writes nothing; saving an unchanged sheet changes
  nothing and is not an edit.
- Correcting a header-change entry updates its line and the "old" side of the next change line; the header block does not
  change.
- Delete and restore; deleting an unknown id returns `false`; deleting while the queue is full returns `false`.
- Copy: `copyLine(3)` and `copyAll()` put exactly the expected bytes on the fake clipboard; `copyAll` equals
  `ShareService`'s text for the same log; copying changes nothing in the store.
- Signals: one `rowsInserted` for a tap, one `modelReset` for a loaded 500-entry log, one `dataChanged` for a time-format
  change; a failed write emits none.
- A bound archived document shows its own entries and is unaffected by the open log's taps.

### HistoryModel (DAY-04, DAY-05)

Archived logs, newest first, grouped by date, with their shared state. Opening one binds a second `TimelineModel` to it,
so the review screen, edit, copy and share work on a past log without a second implementation.

```cpp
class HistoryModel : public QAbstractListModel
{
    Q_OBJECT
    Q_PROPERTY(bool loading READ loading NOTIFY loadingChanged)
    Q_PROPERTY(TimelineModel* reviewModel READ review_model NOTIFY reviewModelChanged)   // the opened past log, or null
public:
    enum Role { LOG_ID = Qt::UserRole + 1, DATE_TEXT, SECTION, UNIT_TEXT, LOCATION_TEXT, ENTRY_COUNT, SHARED, SHARED_AT_TEXT };
    Q_INVOKABLE void refresh();                       // asynchronous; one reset when the list arrives
    Q_INVOKABLE void openLog(const QString& logId);   // asynchronous; reviewModel changes when loaded
    Q_INVOKABLE void closeLog();
    Q_INVOKABLE QString requestDelete(const QString& logId);    // returns a confirmation token, "" for the open log
    Q_INVOKABLE bool confirmDelete(const QString& token);       // DAY-05: only this path builds a DeleteConfirmation
signals:
    void logOpenFailed(const QString& message);
    void deleteFinished(const QString& logId, bool ok);
};
```

- `refresh` runs `list_logs()` on the storage thread and delivers the summaries as one batch; the model resets once.
  Summaries only: History never loads every entry of a year.
- `section` is the local date text (`"7 Nov 2026"`), so two logs on the same date (a move to another unit or site) sit
  under one heading, newest first. `shared` is true when `last_shared_utc` is set and the log has not changed since that
  share (`is_unshared` is false); `changedSinceShare` is true for a log that was shared and has changed since (the row
  shows the ring and "Changed since sharing"); `sharedAtText` is the time of the last share in the user's format. Only
  archived logs are listed; the open log is the Log and Review tabs.
- `openLog` takes the document from the registry and binds `reviewModel` to it. Edits to it go through the same
  `TimelineModel` invokables and the same journalled path (open question 11).
- Delete: `requestDelete` returns a token for an archived log and nothing for the open one; `confirmDelete(token)` is the
  only code that constructs the `DeleteConfirmation` type (stage 2, DS-05) and calls `hard_delete_log`. A token is
  single use and expires when History is left.
- Sharing a past log is `ShareService::shareLog(logId)`; the row's `shared` updates when the share is recorded.

Threading: all reads and the delete are gateway jobs; the model changes on the GUI thread when results arrive. Storage:
`list_logs`, `load_log`, `hard_delete_log`. Renderer: through the bound `TimelineModel`.

Tests (`unit`):

- Newest first; grouping of two logs on one date under one section; a log dated yesterday and one from last year; shared
  and unshared rows; the open log is not listed.
- `refresh` emits one reset; a second refresh while loading does not duplicate rows; a store error leaves the previous
  list and reports.
- `openLog` binds a timeline with the right lines; closing unbinds; opening an unknown id reports `logOpenFailed`.
- Editing and deleting an entry of a past log persists and re-renders; an undo toast works there too.
- Delete: no token for the open log; deleting needs `requestDelete` then `confirmDelete`; a token cannot be used twice or
  after `closeLog`; after the delete the row is gone and `list_logs()` no longer has it; a failed delete keeps the row.
  A compile-time check (as stage 2 specifies) that `hard_delete_log` cannot be called from this layer without the type.
- Sharing a past log marks it shared and the row updates in place with one `dataChanged`.
- Display strings follow the time-format preference, including for logs written before it was changed (PRF-07).

### DayWatcher (DAY-01)

Checks for a later calendar day on launch and on return to the foreground and raises the new-day prompt, with the flag
that the open log is unshared (never shared, or changed since its last share).

```cpp
class DayWatcher : public QObject
{
    Q_OBJECT
    Q_PROPERTY(bool promptVisible READ prompt_visible NOTIFY promptChanged)
    Q_PROPERTY(QString newDateText READ new_date_text NOTIFY promptChanged)       // "Sunday 8 November"
    Q_PROPERTY(QString openLogDayText READ open_log_day_text NOTIFY promptChanged)   // "Saturday"
    Q_PROPERTY(int openLogEntryCount READ open_log_entry_count NOTIFY promptChanged)
    Q_PROPERTY(bool unshared READ unshared NOTIFY promptChanged)
    Q_PROPERTY(bool changedSinceShare READ changed_since_share NOTIFY promptChanged)  // unshared, and shared before
public slots:
    void check();                      // Stage 5 connects it to launch and Qt::ApplicationActive
public:
    Q_INVOKABLE bool acceptNewLog();   // "Start a new log": LogSession::startNewLog
    Q_INVOKABLE void keepOpen();       // "Keep Saturday's log open": hides the prompt, changes nothing
    Q_INVOKABLE bool shareFirst();     // "Share Saturday's log first": ShareService::shareOpenLog, prompt stays
};
```

- `check()` reads the clock and the device zone, then calls the core's `day_prompt(open_log_date, now, zones)`. There is
  no timer: the prompt is raised by launch and by returning to the foreground only, so a session that is still open at
  midnight keeps logging into the open log (DAY-01). A prompt that was kept open is raised again by the next `check()`.
- `unshared` is `is_unshared(meta, live entries)`: the open log has at least one live entry (an empty log has nothing to
  share) and either has no `last_shared_utc` or has changed since it (an entry added, edited, deleted or restored, or a
  header corrected so that the exported text differs). Sharing can happen several times a day, so a share is not the end
  of the day. It is computed from the open document, so it turns false as soon as a share is recorded, while the prompt is
  up, and true again as soon as an entry is added past midnight. `changedSinceShare` is `unshared` for a log that was
  shared before, so the words can say "changed since it was last shared".
- `check()` before the log has loaded does nothing and runs again when the session becomes `OPEN`. Calling it again while
  the prompt is up changes nothing and emits nothing. A failure to convert the time (unknown zone) leaves the prompt
  hidden and reports once.
- `acceptNewLog` returns `false` and keeps the prompt if `startNewLog` is rejected; on success the prompt hides.

Threading: GUI thread; no I/O of its own. Storage: through `LogSession` and `ShareService`.

Tests (`unit`, `TestClock`, fake zone provider and, in one case, the Qt provider):

- Rollover across midnight: 23:59:59.999 on the log's date gives no prompt; 00:00:00 the next day gives the prompt.
  Several days later gives the prompt naming the new date. The clock set back gives none.
- A session past midnight: an entry at 00:10 is added to the open log, `check()` raises the prompt, `keepOpen()` hides it,
  the log keeps the entry, and the next `check()` raises it again; accepting archives the log and the 00:10 entry is in
  the archived one.
- Daylight saving: the 23-hour day (2026-10-04) and the 25-hour day (2026-04-05) in Melbourne roll over at local
  midnight, not at 24 hours of elapsed time. The traveller: a log opened in Melbourne, `now` in Perth at 22:30 local on
  the same UTC day gives no prompt, at 00:30 it does.
- The unshared flag: true for an unshared log with entries; false for an empty log; false after a share is recorded
  (the property changes while the prompt is visible); true again after a share the facilitator cancelled; it is
  per open log, so it is true again for a new log once that has entries. After a share: an added entry, an edit, a delete
  and a restore each make it true again (`changedSinceShare` too), and a second share clears it; a share with no change
  after it leaves it false.
- The prompt on a gap of several days (the facilitator works on some days only): the open log's date is compared, not
  "yesterday", so a check three days later raises the prompt naming that day, unshared or not as the rule says.
- `shareFirst` leaves the prompt up, and `acceptNewLog` afterwards starts the log; `acceptNewLog` while a write fails
  keeps the prompt.
- `check()` before load does nothing and fires on `OPEN`; repeated checks do not repeat `promptChanged`.
- The text names the new date and the old day (`"Sunday 8 November"`, `"Saturday"`) from `QLocale` English (Australia).

### SettingsModel (PRF-01 to PRF-10)

The preferences and the three master lists, and their import and export. It holds the `Configuration` of Stage 1 as its
mirror and saves it through a `ConfigStore`.

```cpp
class SettingsModel : public QObject, public ConfigurationSource
{
    Q_OBJECT
    Q_PROPERTY(bool configured READ configured NOTIFY configurationChanged)          // PRF-09: first launch shows setup
    Q_PROPERTY(QString facilitator READ facilitator NOTIFY configurationChanged)       // PRF-01
    Q_PROPERTY(QString unitPrefix READ unit_prefix NOTIFY configurationChanged)        // PRF-02
    Q_PROPERTY(QString timeStyle READ time_style NOTIFY configurationChanged)          // PRF-07: "24h" or "12h"
    Q_PROPERTY(bool timeSeconds READ time_seconds NOTIFY configurationChanged)
    Q_PROPERTY(QString timeMeridiem READ time_meridiem NOTIFY configurationChanged)    // "upper" or "lower"
    Q_PROPERTY(bool includeTotals READ include_totals NOTIFY configurationChanged)     // PRF-10
    Q_PROPERTY(QAbstractItemModel* errorTypes READ error_types CONSTANT)               // PRF-04
    Q_PROPERTY(QAbstractItemModel* locations READ locations CONSTANT)                  // PRF-03
    Q_PROPERTY(QAbstractItemModel* templates READ templates CONSTANT)                  // PRF-06
    Q_PROPERTY(QAbstractItemModel* boardLayout READ board_layout CONSTANT)             // PRF-05
public:
    Q_INVOKABLE QString setFacilitator(const QString&);        // each returns "" or the plain message of the refusal
    Q_INVOKABLE QString setUnitPrefix(const QString&);
    Q_INVOKABLE QString setTimeFormat(const QString& style, bool seconds, const QString& meridiem);
    Q_INVOKABLE QString setIncludeTotals(bool);
    Q_INVOKABLE QString addLocation(const QString& name);    Q_INVOKABLE QString renameLocation(const QString& id, const QString&);
    Q_INVOKABLE QString moveLocation(int from, int to);      Q_INVOKABLE QString removeLocation(const QString& id);
    Q_INVOKABLE QString addErrorType(const QString& name);   Q_INVOKABLE QString renameErrorType(const QString& id, const QString&);
    Q_INVOKABLE QString retireErrorType(const QString& id);
    Q_INVOKABLE QString moveBoardTile(int fromPosition, int toPosition);       // hold and drag
    Q_INVOKABLE QString assignBoardTile(int position, const QString& errorTypeId);   // tap to swap in another type
    Q_INVOKABLE QString addTemplate(const QString& name, const QString& prompt, const QString& pattern);   // PRF-06
    Q_INVOKABLE QString validatePattern(const QString& pattern) const;   // live message; renameTemplate etc. follow the same shape
    Q_INVOKABLE void loadImport(const QString& path);          // reads and validates; nothing changes yet
    Q_INVOKABLE void applyImport();   Q_INVOKABLE void discardImport();
    Q_INVOKABLE void exportTo(const QString& path);
signals:
    void importPrepared(int errorTypes, int locations, int templates);   // a summary for the confirmation
    void importFailed(const QStringList& messages);                       // the core's plain-English messages, in file order
    void exportFinished(bool ok, const QString& message);
    void writeFailed(const QString& operation, const QString& message);
};
```

- **Where it saves.** The Stage 2 schema has no tables for the preferences or the lists, although the PRD data model
  lists them. Stage 3 saves the configuration as the same JSON file the user imports and exports (`export_configuration`),
  written by `FileConfigStore` to a temporary file and renamed over the old one (`QSaveFile`), on the storage thread.
  One representation serves storage, import and export, and Stage 1's validation guards the file when it is read back
  at launch. If the file is missing or does not validate, the app starts `NEEDS_SETUP` with the broken file kept
  beside it, never deleted. A loss here costs a re-import, not a log entry. The alternative is tables in schema 1,
  which is cheap only while Stage 2 is unmerged (open question 9).
- **How a change is applied.** Each invokable validates (the core rules: non-empty and unique names, a prefix with no `-`
  or `#`, patterns through `validate_pattern`, at most 11 board positions, the 200 and 500 limits), makes a changed copy of
  the configuration and saves it; on success the mirror is replaced and `ConfigurationObserver`s and
  `configurationChanged` fire. Nothing changes if the save fails. A refusal returns the message; QML shows it inline.
- **Effects.** A new facilitator name applies to logs created afterwards; the open log keeps the one it started with. A new
  unit prefix and time format re-render everything, including past days, because neither is stored in a log
  (PRF-07). Retiring a type removes it from the pickers and the board and keeps it on past entries; "remove" for a location
  is the same (retire). A retired type keeps its position out of the layout.
- **Board layout.** `boardLayout` is a 12-row model for the miniature board (the same shape as `BoardModel` without
  counts): `moveBoardTile` is the drag, `assignBoardTile` the swap; both are one save and one signal.
- **Import.** `loadImport(path)` reads the file on the storage thread (a size limit of 1 MiB applies before parsing) and
  runs `import_configuration`. Errors are reported in file order and nothing changes; success holds the result aside and
  emits `importPrepared`, and `applyImport` replaces the configuration in one save. This is the two-step the first-launch
  and Settings screens need (PRF-08, PRF-09). Handing the file to the app from AirDrop and Files is Stage 5.
- **Export.** `exportTo(path)` writes `export_configuration` and reports; the Stage 5 document picker supplies the path.

Threading: all file reads and writes are gateway jobs; the mirror is read on the GUI thread. Storage repository: not
used; `ConfigStore` is its own small interface (`load`, `save`, `read_file`, `write_file`) beside it on the storage
thread. Renderer: provides the `ExportContext` inputs (prefix, time format, totals, name lookups) through
`ConfigurationSource`.

Tests (`unit`, fake `ConfigStore`; two cases against `FileConfigStore` in a temporary directory, label `integration`):

- Each setting changes the mirror only after the save succeeds; a failing save changes nothing and reports.
- Facilitator and prefix: empty, over-length, prefix with `-` or `#` refused with the core's message; the open log keeps
  its facilitator and the next new log takes the new one.
- Time format: all eight style, seconds and meridiem combinations; changing it re-renders a timeline, the toast text and
  a past log in History.
- Locations: add, rename, reorder, remove (retire); duplicates refused; a retired location still renders on a past log.
- Error types: add, rename, retire; a retired type disappears from `ErrorPickerModel` and the board and still renders on
  past entries; a rename follows open question 7; the 11-position limit on the board; a type beyond 11 gets a colour slot
  by the rule above.
- Board layout: a drag moves the position, the colour slot of the type does not change, one `dataChanged` on `BoardModel`;
  assigning a type that is on the board elsewhere swaps the two.
- Templates: name, prompt and pattern; `{value}` required; `{name}` refused; the live validation message.
- Totals option reaches the export context.
- Import: the sample configuration (`config/sample-config.json`) loads and applies; a malformed file, a future schema
  version ("update the app"), duplicate ids and a 2 MiB file are refused with messages in file order and no change; cancel
  (`discardImport`) changes nothing; import then export round-trips byte for byte.
- First launch: `configured` is false with no file, true after a valid import or a manual setup of facilitator and
  prefix; a corrupt file at launch gives `NEEDS_SETUP` and is kept on disk.
- `FileConfigStore`: a save killed between the temporary file and the rename leaves the old file readable (simulated by
  failing the rename); a read of a file that disappears reports; an unwritable directory reports.
- Fictional values only: the tests read the sample file and use Sam Example, UNIT and North Site.

### ShareService (REV-05; DAY-01 share first; DAY-04 re-share)

Builds the export text, hands it to the platform share sheet and records the share time.

```cpp
class ShareService : public QObject
{
    Q_OBJECT
    Q_PROPERTY(bool available READ available CONSTANT)     // false on the desktop stub: QML hides the control
    Q_PROPERTY(bool sharing READ sharing NOTIFY sharingChanged)
public:
    Q_INVOKABLE bool shareOpenLog();                        // the Share log button
    Q_INVOKABLE bool shareLog(const QString& logId);        // History: re-share a past log
signals:
    void shareFinished(const QString& logId, const QString& outcome);   // "shared", "cancelled", "unavailable", "failed"
    void shareRecorded(const QString& logId);               // the share time is committed
    void writeFailed(const QString& operation, const QString& message);
};
```

- Flow. Take the document from the registry (the open one is already in memory; a past log is loaded through the
  gateway). Render `export_text` on the GUI thread; a `RenderError` stops here with `shareFinished(.., "failed")` and no
  sheet. Call `platform_services::present_share_sheet(text, done)`. When `done` runs: outcome `shared` records the time
  through `document.mark_shared(now)`; `cancelled`, `unavailable` and `failed` record nothing. `shareFinished` is emitted
  for every outcome; `shareRecorded` after the commit.
- The completion may run before `present_share_sheet` returns (the desktop stub) or after (iOS); both are handled. A
  completion that arrives after the service is destroyed is ignored (`QPointer`, as the bridge does).
- One sheet at a time: a second call while `sharing` returns `false`.
- "Shared" means the platform reported `shared`. On iOS that includes the sheet's own Copy action, and not a dismissed
  sheet. Whether Slack reports as shared is outside our control and is checked on a device. The in-app Copy buttons do
  not mark a log shared (open question 10).
- If recording the time fails after the text was shared, `writeFailed` is emitted and the log stays unshared, which errs
  on the side of one extra prompt. Sharing again records it.
- Re-sharing a past log updates its `last_shared_utc` and, through the document, the History row.

Threading: rendering is CPU work on the GUI thread; the sheet call and its completion are on the GUI thread (a bridge
guarantee); the commit is a gateway job. Storage: `mark_shared` (and `load_log` for a past log). Renderer:
`export_text` (declared with `ExportContextBuilder` in 3.2, so `TimelineModel::copyAll` and this service call the same
function), which wraps `render_export`.

Tests (`unit`, fake `platform_services`, `TestClock`):

- The text handed to the sheet equals `export_text` and equals the golden file for the golden scenario (`prd-example.txt`),
  and equals what `TimelineModel::copyAll` puts on the clipboard.
- `shared` records the time of the completion (the clock is advanced during the sheet) and only after the commit;
  `cancelled`, `failed` and `unavailable` record nothing and leave the log unshared.
- A completion before the call returns (the stub) and a completion after it both work; a completion after destruction
  does not crash.
- A second call during a share returns `false`; after the outcome it works again.
- A render error (an unknown location id) shows no sheet and reports `failed`.
- Sharing a past log loads it, shares its text (byte-identical to the original golden), records the time and updates the
  History row; sharing an unknown id reports and shows no sheet.
- A failing mark-shared commit emits `writeFailed` and leaves the flag unset; a later share records it.
- The text is what the screen showed: an edit that has committed is in it, an edit still waiting in the queue is not.

## Threading tests

These run in the `integration` label with `ThreadedStorageGateway` and a repository wrapper, and under TSan in CI.

- **No blocking.** The wrapper sleeps 40 ms inside every commit. A `QTimer` ticks every 4 ms on the GUI thread while 20
  taps are made. The longest gap between ticks stays under 16 ms (one frame at 60 Hz; the final budget is set from
  measurement and written to `perf-budget.json`, standard 6.5), and `recordError` itself returns in under 1 ms at the 95th
  percentile. All 20 entries are present afterwards, in tap order.
- **Where the calls run.** The wrapper records the thread of every repository, draft and configuration call. None is the GUI
  thread; all are the same thread.
- **Order and shutdown.** 1,000 jobs run in submission order; `drain()` returns only when the queue is empty; destroying
  the gateway with jobs queued finishes them; a result for a destroyed receiver is dropped without a call.
- **First frame.** With the gateway held, `LogSession::state` is `LOADING` and every model is empty but valid; releasing it
  gives `OPEN` (this is the unit-level half of the start-up rule; the cold-start time itself is Stage 5).
- **Confirmation follows the commit.** With the commit held back inside the wrapper, no toast, count, "Last" bar or
  timeline row appears (the touch-down haptic has already fired); releasing it shows all four. The success haptic of a
  note fires only after its commit.

## The exit-gate test: a full day, headless

One test function drives the day through the public interfaces of the services only: the invokables and properties QML
would use, with no access to private state. It is written once as `full_day(Rig&)` and run twice:

- `unit`: in-memory repository, fake configuration store, `ManualStorageGateway`, `TestClock`, the fixed-offset zone
  provider (`Australia/Melbourne` at +11:00, which is right for 7 and 8 November 2026), a fake platform and clipboard.
  `settle()` runs the gateway until it is idle.
- `integration`: `SqliteLogRepository` and `FileConfigStore` in a temporary directory, `ThreadedStorageGateway`, the Qt
  time zone provider, and the same fakes for the platform and clipboard. `settle()` spins the event loop until
  `pendingWrites` is 0. The restart in step 9 closes the store and runs `Recovery::run` before reopening.

The `Rig` holds the clock, the fakes, an `EventLog` (every commit, haptic, toast change and share in order) and all the
services built as `main.cpp` will build them. `rig.restart()` destroys every service and the gateway and builds them
again on the same store, as a relaunch does. The rig's tap helper stands in for the QML press: it fires the fake
impact haptic (the touch-down) and then calls the tap invokable, so the `EventLog` shows the haptic before the commit.
The data is the sample configuration (`config/sample-config.json`) and the
fictional names only. The final text is compared with `libs/core/tests/golden/prd-example.txt` itself through
`ej::test::check_golden`, not a copy, so the PRD example stays the one contract.

| Step | Time (local) | Action | Must hold afterwards |
|---|---|---|---|
| 1 Start | Sat 7 Nov 08:55 | Empty store. `loadImport` and `applyImport` of the sample configuration. `LogSession` loads. | `configured`; state `OPEN`; facilitator Sam Example; `dateText` "Sat 7 Nov"; `missingFields` has all three keys; timeline empty; 12 board rows, all counts 0; no haptic. |
| 2 Tap before the header, then undo | 08:58 | Tap Wrong Response with the commit held (the manual gateway in `unit`, a latch in the repository wrapper in `integration`). Release it. After 2 s call `undo()`. | Held: the touch-down impact haptic has fired once; no entry, count or toast. Released: count 1, toast "Wrong Response logged at 08:58", still one impact haptic (it precedes the commit; the confirmation follows it). After undo: count 0, toast closed, the entry exists in the store as deleted, no second haptic. |
| 3 Header | 09:00 | `validateHeaderValue` and `correctHeader` for version `1.4` (refused). Unit number `28`, then Correct to `27`. Location North Site. Version `1.4.2`. | Refused call writes nothing (the write count is unchanged). `missingFields` empty; `unitText` "UNIT-27". Correct added no entry: timeline `entryCount` 0 and no header-change entry in the store. |
| 4 Single tap | 10:05 | Tap Wrong Response. Advance 6 s. | Count 1. Toast gone after the window; `undo()` returns `false`. |
| 5 Double tap | 10:20:00 and 10:20:01 | Tap Mute Delay twice. Advance 5 s. | After the second: toast "Mute Delay logged ×2 at 10:20", `repeatCount` 2, count 2. After 5 s: toast closed. |
| 6 Draft and note | 10:29 and 10:30:05 | `set_draft("Played Morning Medly")`, advance 400 ms, then `addNote` with that text. | Draft in the store before the note; after the commit the draft is empty in the store and in `session.draft`, entry at 10:30, one success haptic. Toast "Note added at 10:30". |
| 7 Other sheet | 10:45 | `picker.addType("Wrong Greeting")`; record it; undo 3 s later. | The type is listed under "Not on your board"; Other's count goes 0, 1, 0; its entry is soft-deleted; the type stays in the master list. |
| 8 Tap | 11:02:10 | Tap Wrong Track Played. | Count 1; timeline `entryCount` 5. |
| 9 Restart mid-day | 11:30 | `set_draft("Visitor from head office")`, `flushDraft()`, `settle()`, then `rig.restart()`. | Same header, same five timeline lines as before the restart, same board counts, `draft` restored (DS-03, NOTE-03), no toast. Then the draft is cleared. |
| 10 Version change | 13:15 | `changeHeader` to `1.4.2` (refused: unchanged). `changeHeader` to `1.5.1` (a typo). | Refused call writes nothing. One new entry, line "13:15 Software Version changed: 1.4.2 → 1.5.1"; `versionText` "1.5.1"; the timeline's `headerText` still shows `1.4.2`. |
| 11 Wrong tile | 13:41:30 | Tap Track Delay. | Count 1. |
| 12 Review edits | 17:00 | a) `saveEntry` the 10:30 note with text "Played Morning Medley". b) `previewLine`, then `saveEntry` attaching "asked for Evening Song" to the 11:02 error. c) `saveEntry` the 13:15 change with new value `1.5.0`. d) `saveEntry` the 13:41:30 entry to error type Speech Delay at 13:40:00. e) `deleteEntry` the 10:20:01 tap, `undo()`, delete again, advance 5 s. | a) Line "10:30 (Free Text) Played Morning Medley". b) Preview equals the saved line "11:02 T\| Wrong Track Played – asked for Evening Song". c) Line "13:15 Software Version changed: 1.4.2 → 1.5.0", `versionText` "1.5.0", header block unchanged. d) Line "13:40 T\| Speech Delay" still last; Track Delay count 0, Speech Delay 1. e) Toast "Entry deleted"; undo restores (Mute Delay count 2); second delete expires; count 1; `undo()` returns `false`; the entry stays deleted in the store. Edits set `m_EditedUtc`; no haptic fired for any review action. |
| 13 Golden check 1 | 17:00 | `copyAll()` on the open log's timeline; read the fake clipboard. | `entryCount` 6; `lastLine` "13:40 T\| Speech Delay"; `headerText` is the first five golden lines; the whole text equals `prd-example.txt` byte for byte. |
| 14 Share, cancelled | 17:30 | `shareOpenLog()`; the fake sheet reports `cancelled`. Then `day.check()`. | `shareFinished` "cancelled"; no `last_shared` in the store; no prompt (same date). |
| 15 New day | Sun 8 Nov 08:05 | `day.check()` (a foreground). | Prompt visible; `newDateText` "Sunday 8 November"; `openLogDayText` "Saturday"; `openLogEntryCount` 6; `unshared` true. |
| 16 Share first | 08:05:20 | `day.shareFirst()`; the sheet reports `shared` after 20 s. `copyAll()`. | Text given to the sheet equals `prd-example.txt` (golden check 2); `last_shared` is 08:05:20 local, committed after the sheet completed; `unshared` false while the prompt is still visible; the clipboard text equals the same bytes. |
| 17 Start the new log | 08:06 | `day.acceptNewLog()`. | Prompt hidden; `newLogStarted` once; the store has the old log archived and exactly one open log; new header has Sam Example and "8 Nov 2026" and three empty fields; timeline empty; board counts all 0; no toast; its export is exactly `Facilitator: Sam Example`, `Date: 8 Nov 2026`, `UNIT #:`, `Location:`, `Software Version:`. |
| 18 History | 08:10 | `history.refresh()`, `openLog`, `share.shareLog(id)` with the sheet reporting `shared`. | One row: section "7 Nov 2026", "UNIT-27", "North Site", 6 entries, shared at "08:05"; `reviewModel` lines equal the six golden lines; the text shared is `prd-example.txt` byte for byte (golden check 3, the last step); `last_shared` moves to the new time and the row updates. |
| 19 Closing checks | | Read the `EventLog` and the store. | Every impact haptic precedes the commit of the entry it acknowledges, and the confirmation (count, toast, "Last" bar) follows that commit; every success haptic directly follows the commit of its note; there is no haptic for an undo, a review edit, a delete, a header Correct or a share; every commit has the expected kind and order; the gateway is idle; no `writeFailed` was emitted; in `integration`, no repository call ran on the GUI thread; all three golden comparisons passed. |

Steps 14 to 16 are the unshared flag end to end: cancelled leaves it set, the prompt shows it, a recorded share clears
it. Steps 4, 5 and 12e are the undo window and the repeat count with the fake clock. Step 15 is the rollover across
midnight (the boundary and past-midnight cases themselves are in the `DayWatcher` tests). Step 9 is persistence.

## Work order

Twelve packages, one pull request each. The files each one adds are disjoint from every other package's, which is what
allows the parallel waves: all new sources and tests are picked up by the `GLOB` that 3.1 puts in place, so no package
edits a shared `CMakeLists.txt`, and the shared documentation pages are touched only by 3.1 and 3.11.

| Step | Package | Adds (under `apps/app/src/viewmodel/` and `apps/app/tests/viewmodel/` unless noted) | Needs merged first |
|---|---|---|---|
| 3.1 | Foundations | `environment.hpp`, `storage_gateway.*` (interface, `ThreadedStorageGateway`), `feedback_sink.hpp`, `clipboard.hpp`, `configuration_source.hpp` (read and observer interfaces, and the one `ConfigurationEditor` write the picker needs), `log_observer.hpp`, `open_log_source.hpp`; test support (`TestClock`, `EventLog`, `RecordingRepository`, `ManualStorageGateway`, `FakePlatform`, `FakeClipboard`, `SequentialIds`, fakes for the sources); `apps/app/CMakeLists.txt` (the `ej_viewmodel` library, the globs, the test executable); `libs/core/tests` golden and fake-zone helpers made available to the new tests; `docs/architecture.md`, `docs/testing.md` | Stage 2: 2.1 |
| 3.2 | `LogDocument`, registry, `ExportContextBuilder` | `log_document.*`, `document_registry.*`, `export_context_builder.*` (with `export_text`) and their tests | 3.1; Stage 1: 1.5 |
| 3.3 | `SettingsModel` and `ConfigStore` | `config_store.hpp`, `file_config_store.cpp`, `settings_model.*`, the four list models (`error_type_list_model.*`, `location_list_model.*`, `template_list_model.*`, `board_layout_model.*`) and tests | 3.1; Stage 1: 1.6 |
| 3.4 | `LogSession` | `log_session.*`, `recovery_notice.*` and tests | 3.2 |
| 3.5 | `UndoController` | `undo_controller.*` and tests | 3.2 |
| 3.6 | `BoardModel`, `ErrorPickerModel` | `board_model.*`, `error_picker_model.*` and tests | 3.2 |
| 3.7 | `TimelineModel` | `timeline_model.*` and tests | 3.2 |
| 3.8 | `ShareService` | `share_service.*` and tests | 3.2 |
| 3.9 | `DayWatcher` | `day_watcher.*` and tests | 3.4, 3.8 |
| 3.10 | `HistoryModel` | `history_model.*` and tests | 3.7, 3.8 |
| 3.11 | Composition and the full-day test | `apps/app/src/app_root.*`, `apps/app/src/qt_clipboard.*`, the changes to `apps/app/src/main.cpp` and `apps/app/CMakeLists.txt`, `apps/app/tests/full_day.cpp`, the threading tests, `Rig`, `docs/architecture.md` | 3.3 to 3.10; for the `integration` variant, Stage 2 complete and its gate passed |
| 3.12 | Exit-gate pass | `tools/coverage-floors.json`, the result recorded at the end of this file | 3.11 |

**Waves.**

1. 3.1 alone.
2. 3.2 and 3.3 in parallel: one is the log pipeline, the other is the configuration; they share no file and no type
   except what 3.1 defined.
3. 3.4, 3.5, 3.6, 3.7 and 3.8 in parallel, all after 3.2. Each reads its neighbours through the interfaces and fakes of
   3.1, not through each other's classes, so no one waits for another and no two touch the same file. (`3.6` and `3.7` need
   configuration only through `ConfigurationSource`, so they do not wait for 3.3.)
4. 3.9 and 3.10 in parallel, once the packages they name have merged.
5. 3.11, then 3.12.

Where two packages could want the same edit (for example both need a role or a helper in a common header), the helper goes
in 3.1 or the package that owns the type, and the other package waits or reads it through the interface. A package that
finds it must edit another package's file stops and says so in its pull request instead of editing it.

Stage 2 and Stage 3 overlap only in the repository interface: 3.1 starts when 2.1 has merged, and every package up to
3.10 uses `MemoryLogRepository`. 3.11 is the first to need the real store. Whether any of this may start before the
Stage 2 gate has passed was decided in question 1: 3.1 to 3.10 start now, 3.11 waits for the gate.

Each package runs `linux-debug` and `linux-asan-ubsan` locally before its pull request, as AGENTS.md says, and TSan in CI.
The pull request says which parts have not run on a device.

## Cross-cutting

- **Documentation.** `docs/architecture.md`: the layout and targets (3.1) and the final wiring (3.11). `docs/testing.md`:
  the fakes, the `Rig` and the label use (3.1). No CMake option or preset changes, so `docs/building.md` is unchanged.
  Nothing is visible to the facilitator yet, so there is no `changelog.d/` fragment; the first one comes with Stage 4.
- **Licence headers.** Every new source file carries the SPDX header copied from an existing file.
- **No platform `#if`.** Nothing in the layer differs by platform. The differences (clock, device zone, clipboard,
  haptics, share sheet) are behind the interfaces above and chosen in the composition root.
- **Fictional data only (NFR-10).** Every name in code, tests and fixtures is Sam Example, UNIT, North Site, Wrong Response
  and the other proposed board types, or comes from `config/sample-config.json`.
- **Coverage and baselines.** `tools/preprocessor-baseline.json` must not rise (the `Q_OBJECT` and `Q_PROPERTY` macros are
  not preprocessor directives). 3.12 gives the viewmodel code its own coverage floor if the gate can key on a
  subdirectory (proposed 90 line, 70 branch); otherwise it raises `apps/app`.
- **Error messages for the screen.** A write failure shows the facilitator something (a banner, a toast) and the design
  package has no such state yet. Stage 3 supplies `writeFailed(operation, message)` with a plain message; the wording and
  the look are a Stage 4 design item, listed there with the recovery message.
- **Strings.** User-visible text made in C++ uses `tr()` with `%1`-style placeholders and no concatenation, so Stage 4 can
  add translation catalogues without touching the code.

## What cannot be verified here

The development machine has no Mac and no phone, and the tests run on a desktop with a fake platform. These are checked
on an iPhone 17 (in Stage 5, or in the first TestFlight build that contains the services) and are listed in each pull
request as not run:

- **Commit-to-feedback latency.** The tap takes a journal fsync and a database commit, each a full flush on Apple
  hardware, before the haptic. The impact haptic fires on touch-down (decision 2), so NFR-03's 100 ms does not depend on this; the latency of the
  confirmation is still measured with Instruments, to see that it is not slow enough to look broken.
- **Frame drops while a commit is in flight.** The tests prove the GUI thread is not blocked, by timer gaps on a desktop; a
  dropped frame on a ProMotion display needs the device.
- **Share sheet outcomes.** That Copy, Mail, Files and Slack report `shared`, that a dismissed sheet reports `cancelled`,
  and that returning from the sheet raises a foreground event that runs `check()` once.
- **Foreground and background events.** That `check()` runs on launch and on `Qt::ApplicationActive`, and `flushDraft()`
  on going inactive. Stage 3 provides the calls; Stage 5 connects them, and iOS suspends the app, including the storage
  thread, so a commit that is in flight when the app goes inactive needs background time requested for it.
- **The device time zone and midnight.** `QTimeZone::systemTimeZoneId()` on iOS, a change of zone while travelling, and a
  real midnight, as opposed to the fake clock.
- **Clipboard on iOS.** `QClipboard` from Qt on a phone, including the system's paste notice.
- **Real store speed.** The `integration` variant runs the SQLite store on a Linux runner; its speed and behaviour on
  the phone's storage are not known from it.
- **Accessibility names.** The roles carry `accessibleName` text; whether VoiceOver reads it well is a Stage 4 device check.

## Decisions (decided by the owner, 2026-11-07)

The owner accepted every recommendation on this page on 2026-11-07. The numbering of the former open questions is kept,
so references such as "open question 11" elsewhere on the page and in other pages still resolve to the decision with
that number.

**Accepted defaults** (formerly "Proposed in this draft"; accepted by the owner, 2026-11-07)

- The undo window is five seconds from the moment the commit finishes, slides with every new action, and is measured on
  the monotonic clock. Undo is one soft delete of the most recent action; a tap's undo and a review delete are soft
  deletes, and no entry is ever physically removed in v1 (see also decision 6). (owner, 2026-11-07)
- One toast slot for the app. A new action replaces the toast and the earlier action stays committed. (owner, 2026-11-07)
- The new-day check runs on launch and on returning to the foreground only, with no timer, so a phone left open over
  midnight keeps logging into the open log until the next foreground (DAY-01). (owner, 2026-11-07)
- An empty open log from an earlier day still gets the prompt, is archived like any other (DS-05), and is never called
  unshared. (owner, 2026-11-07)
- Empty board positions are empty tiles that cannot be tapped; Other is always the twelfth. (owner, 2026-11-07)
- A change of facilitator name applies to logs created afterwards. A change of unit prefix or time format applies to
  every log, past days included, because neither is stored in a log. (owner, 2026-11-07)
- A location "remove" is a retire, as for error types, so past logs still render the name. (owner, 2026-11-07)
- At most 64 commands wait per log; the 65th is refused with a message instead of being dropped. (owner, 2026-11-07)
- Tests are Catch2 (standard section 6), with `QSignalSpy` from `Qt6::Test` (decision 3). (owner, 2026-11-07)

**Decided**

1. **Stage 3 is not "UI work" under the Stage 2 gate.** Packages 3.1 to 3.10 start now on the in-memory repository
   (2.1 has merged); 3.11, which makes the app use the real store and runs the full-day test against it, waits for the
   Stage 2 exit gate. (owner, 2026-11-07)
2. **The impact haptic fires on touch-down.** It is an acknowledgement of touch, not a confirmation that the entry is
   saved; the count, the toast and the "Last" bar wait for the commit. DS-01 is reworded to "before any confirmation that
   the entry is saved is shown" (the same wording as Stage 5, decision 1). The note's success haptic still follows its
   commit. (owner, 2026-11-07)
3. **Tests are Catch2,** with `QSignalSpy` and a `QCoreApplication`, as `libs/ios_bridge/tests` already does.
   (owner, 2026-11-07)
4. **A note gets an undo toast,** as in the approved prototype: the undo is a soft delete and the text goes back to the
   composer. NOTE-02 is reworded to say "after the undo window". (owner, 2026-11-07)
5. **Undo of a repeated tap closes the toast.** `×3` undone leaves two taps and no toast; the rest is fixed on the
   review screen. (owner, 2026-11-07)
6. **Deleted entries are never purged,** except by deleting the whole log from History. They stay in the store flagged
   deleted, invisible and uncounted everywhere. (owner, 2026-11-07)
7. **The name snapshot wins.** A rename of an error type affects only later taps, and every past export reads as it did
   when posted; the edit sheet can still change an entry's type, which takes a new snapshot. This needs a snapshot field
   on `ErrorEntry` and a renderer that prefers it, and is done as part of Stage 3. (owner, 2026-11-07)
8. **A tile's colour is the type's position in the master list modulo 11,** the design tokens' rule, so it follows the
   type when the board is reordered. No colour is stored in the configuration. (owner, 2026-11-07)
9. **The configuration is stored as the JSON file** the user imports and exports, replaced atomically on every change,
   not as tables in schema 1. (owner, 2026-11-07)
10. **What counts as shared** (reconciled with Stage 5, decision 2): any share-sheet activity that completes, including
    the sheet's own Copy action, sets the shared time. The in-app Copy buttons ("Copy all", a single line) do not.
    An empty log is never unshared. (owner, 2026-11-07) **Changed 2026-11-09 (owner):** sharing can happen several times a
    day, so a share is not the end of the day. A log is unshared when it has entries that were added, edited, deleted or
    restored since the last share (or a header correction that changes the exported text), or has entries and was never
    shared. Adding, editing or deleting an entry after a share makes the log unshared again until the next share. The
    new-day prompt offers to share first only for an unshared log. It was "an edit after sharing does not clear the
    shared time", which hid work added after a mid-day share. The change is tracked by the order of the journal records,
    not by clocks (`LogMeta::m_ChangedSinceShare`, `day_log.changed_since_share`, schema version 2).
11. **Archived journals are reopened** to append edit, delete and shared records, so DS-02 holds for history. Stage 2's
    2.3 and 2.4 adapt to this. (owner, 2026-11-07)
12. **Adding a type from the Other sheet does not record it.** The new type appears in the list and the facilitator taps
    it, so a typo in a new name cannot create an entry she did not mean to log. (owner, 2026-11-07)

## Addendum: five additions for Stage 4 (decided by the owner, 2026-11-07)

Stage 4 (`stage-4.md`, decision 4) found five things the approved screens need that this page does not give, and the owner
decided to add them here rather than do the work in QML. The packages named are still unmerged, so each is added to that
package's scope, tests and sketch. QML-facing names are camelCase and the C++ readers follow the casing rules above.

1. **UTC from local time (3.4, `LogSession`).** The version-change sheet has a time field. Add
   `Q_INVOKABLE bool changeHeader(const QString& fieldKey, const QString& text, int localSeconds)`: `localSeconds` is seconds
   since local midnight of the log's date, resolved in the log's zone with `TimeZoneProvider::to_utc` exactly as
   `TimelineModel::saveEntry` does for the edit sheet, so an instant in the missing hour or the repeated hour behaves as
   Stage 1 defined. The existing `atUtcMs` overload stays for tests. Add `Q_PROPERTY(int nowLocalSeconds READ now_local_seconds)`
   (the clock in the log's zone, for the "now" default). Tests: a change at a local time before and after the Melbourne
   changes of 2026-10-04 and 2026-04-05, the entry's instant and zone, an unknown or out-of-range value refused, and the
   default equals the clock.
2. **Preview of a header change (3.4, `LogSession`).** Add `Q_INVOKABLE QString previewHeaderChange(const QString& fieldKey, const QString& text, int localSeconds) const`:
   it applies the change to a scratch copy of the mirror and returns `render_entry_line` of the new entry, or the plain
   refusal message of `validateHeaderValue` or for a value equal to the one in force. It writes nothing. Tests: the preview
   equals the line of the entry `changeHeader` then saves, byte for byte; an unchanged or invalid value returns its message;
   no repository call is made.
3. **Entry point for a new log (3.4 `LogSession`, with the design in Stage 4 package 4.2).** `startNewLog()` already exists;
   the addition is the property the confirmation shows, `Q_PROPERTY(int liveEntryCount READ live_entry_count NOTIFY headerChanged)`
   of the open log, so the confirmation can say what is being archived. The control's place (a row at the bottom of Today's
   details, behind a confirmation) is a Stage 4 design decision. Test: the count follows commits and undo.
4. **Time formatting for the screens (3.7, `TimelineModel`).** Add `Q_INVOKABLE QString formatLocalSeconds(int localSeconds) const`,
   which formats with the user's time format through the renderer's formatter, so the edit sheet, the version sheet and the
   time tumbler never format a time in QML (PRF-07). The preference-change test of `TimelineModel` covers it.
5. **Roles for the template chips and value prompt (3.3, `template_list_model`; the role names are also a design item in 4.2).**
   `SettingsModel::templates` names its roles, so the Log screen's chips and prompt can bind: `templateId`, `name`, `prompt`
   (the text asking for the value), `pattern` and `inputHint` (`text`, or `digits` where a pattern's value is numeric, as a
   hint only). The same applies to the other list models (`errorTypes`: `typeId`, `label`, `retired`; `locations`:
   `locationId`, `name`; `boardLayout`: the roles of `BoardModel` without counts), so 3.3 lists every role name in its sketch.
   Test: each model's `roleNames()` equals the documented list.

Items 1, 2 and 3 are in the pull request of 3.4, item 4 in 3.7 and item 5 in 3.3. Stage 4 package 4.9 needs items 1 and 2,
4.10 needs item 4, and 4.9, 4.12 and 4.13 need item 5.

## Exit gate record

Recorded 7 Nov 2026 (work package 3.12). The gate is **met**. Nothing failed, and nothing in the gate has run on a device.

| | |
|---|---|
| Run | `CI`, push to `develop`, [run 42387774736](https://github.com/iainchesworthlabs/icl-error-journaling/actions/runs/42387774736), all jobs green (the dependency scan is skipped on a push) |
| Commit | `e220e5414dea518979b4fddffce0433f1c9071b0` (`develop` after work package 3.11, #60) |
| Legs | Linux gcc debug and release, clang ASan/UBSan and TSan, coverage; macOS debug and release; macOS with Xcode 27; the kill harness in the queue shape on four legs |
| Same code, again | A dispatch of the same workflow on a branch with the same sources and a probe floor for the view-model component ([run 42388052670](https://github.com/iainchesworthlabs/icl-error-journaling/actions/runs/42388052670)), green on every leg. Its coverage job gave the view-model figures below. |

### 1. A full day, headless, in both variants: met

`full_day(Rig&)` ran twice on every leg above, and both runs passed on all of them:

| Variant | Test case | Label | Store and gateway |
|---|---|---|---|
| unit | "A full day, headless, on the in-memory store" | `unit` | in-memory repository, `ManualStorageGateway`, `TestClock` |
| integration | "A full day, headless, on the real store and a storage thread" | `integration` | `SqliteLogRepository`, `FileConfigStore`, `ThreadedStorageGateway`, restart through `Recovery::run` |

Each run goes through the 19 steps of "The exit-gate test" and ends with the closing checks of step 19, including that exactly three
golden comparisons were made (`CHECK(m_Goldens == 3)`).

### 2. The three golden comparisons: met

Each is made against `libs/core/tests/golden/prd-example.txt` itself through `check_golden`, byte for byte, in both variants:

| | Step | Text compared |
|---|---|---|
| Golden 1 | 13 | the clipboard after `copyAll()` on the open log's timeline |
| Golden 2 | 16 | the text handed to the share sheet by `day.shareFirst()` |
| Golden 3 | 18 | the text handed to the share sheet by `share.shareLog(id)` from History |

### 3. Test counts

The view-model tests are found by a glob, and all of them passed on all seven test legs:

| Part | Cases | Result |
|---|---|---|
| `unit` (`apps/app/tests/viewmodel/test_*.cpp`) | 431 | all passed |
| `integration` (`apps/app/tests/viewmodel/integration/*.cpp`) | 30 | all passed, 5 of them the threading tests and 11 the gateway tests |

The whole `ctest` run of a test leg is 916 tests in the `unit`, `integration` and `system` phases (the label summary shows 713, 221
and 8, which add up to more than 916), then the 2 `ui` tests and the 1 `perf` test: 100% passed on `linux-gcc-debug`,
`linux-gcc-release`, `linux-clang-asan-ubsan`, `linux-clang-tsan`, `linux-coverage`, `macos-debug`, `macos-release` and
`macos-xcode27-release`. There are no `perf`-label view-model tests; the budgets of the threading section are checks inside the
`integration` cases.

### 4. Threading tests under TSan: met, with the limits of the check stated

All 30 `integration` cases, the threading cases among them, ran in the `linux-clang-tsan` leg and reported nothing. Those cases are:
taps during a 40 ms commit do not block the GUI thread and arrive in tap order; every repository call is on the one storage thread; the
first frame is `LOADING` with empty models and then `OPEN`; confirmation follows the commit; draining at quit finishes queued writes;
and the gateway cases (order of 1,000 jobs, `drain()`, destruction with jobs queued, a result for a destroyed receiver dropped).

Three things the page above should not be read to say:

- **The budgets in the test are looser than the plan's sketch.** The test enforces a longest tick gap under 30 ms and a 95th
  percentile of `recordError` under 5 ms (constants in `test_threading.cpp`), not 16 ms and 1 ms, because a loaded runner and TSan delay a
  timer by a few milliseconds. The plan said the final budget is written to `perf-budget.json` from measurement; that file does not exist
  and the measured figures are not printed on a pass, so this record has no measured gap. The device measurement is Stage 5.
- **TSan does not see inside Qt.** `tools/sanitizers/tsan.supp` drops reports whose stacks pass through the prebuilt Qt libraries, which
  are not instrumented. Code in this repository is instrumented.
- **One run per leg.** The gate is one green run; there is no repeat count for the timing cases.

### 5. Sanitiser legs: met

`linux-clang-asan-ubsan` and `linux-clang-tsan` ran all 916 tests, the 2 `ui` tests and the `perf` test, with no AddressSanitizer,
LeakSanitizer, UBSan or ThreadSanitizer report in any job log. The kill harness (queue shape, 100 random kills and the crash-point
cases) was green on `linux-gcc-debug`, `linux-clang-asan-ubsan`, `linux-clang-tsan` and `macos-debug`. This package changes no code and was not built
locally.

### 6. Coverage and the floor: met, floor raised

The coverage job (`linux-coverage`, gcovr with throw and unreachable branches excluded) measured, with the view-model code as its own
component:

| Component | Statement | Branch | Floor before | Floor after |
|---|---|---|---|---|
| `apps/app/src/viewmodel` (`ej_viewmodel`) | 93.8% | 82.0% | none of its own (inside `apps/app`, 60 / 30) | **93 / 82** (proposed 90 / 70) |
| `apps/app` (the rest: `main.cpp`, `app_root`, `qt_clipboard`) | 72.8% | 58.9% | 60 / 30 | 60 / 30, unchanged |
| `libs/core` | 96.7% | 92.1% | 94 / 90 | unchanged |
| `libs/storage` | 90.8% | 77.3% | 90 / 75 | unchanged |
| `libs/ios_bridge` | 95.9% | 85.5% | 90 / 50 | unchanged |

On the push run, where the view-model code is not split off, `apps/app` as a whole measured 92.6% statements and 80.8% branches. The gate
keys on a subdirectory (the longest matching prefix wins), so `tools/coverage-floors.json` gained `apps/app/src/viewmodel`. The floors
are the measured figures rounded down; only the one new floor is set, and no floor was lowered. The preprocessor baseline did not rise
(`tools/ci/precheck.py` passes).

### What the gate does not show

Nothing in Stage 3 has run on a device, and the development machine has no Mac. The Linux and macOS legs ran in CI only. The list in "What cannot be verified here" stands in full for an iPhone 17 in Stage 5 or the first TestFlight
build with the services: commit-to-feedback latency with a real `F_FULLFSYNC`, frame drops on a ProMotion display, share sheet outcomes
and the foreground event after returning from the sheet, foreground and background calls (`check()`, `flushDraft()`) and a commit in
flight when iOS suspends the app, the device time zone and a real midnight, the clipboard and its paste notice on iOS, the speed of the
store on the phone, and the accessibility names. The full-day test uses a fake clock, a fake platform and a fake clipboard throughout.
