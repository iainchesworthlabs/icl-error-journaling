# Stage 5: iOS bridge and lifecycle

Task breakdown, test list and device checklist for Stage 5 of the [implementation plan](../implementation-plan.md). Each
work package is one pull request into `develop`. Requirement IDs refer to the [PRD](../prd.md).

**Exit gate:** on an iPhone 17, read from the app's own timing line on a TestFlight build (there is no Mac, so no
Instruments): cold start to a usable board under 1.5 s and tap to haptic under 100 ms. The share sheet posts to Slack, and a configuration file AirDropped from a Mac imports cleanly.

**Status:** draft for owner review. No code is written until the owner approves this page.

Nothing in this stage can be proved on the development machine. It has no Mac, CI only compiles the Objective-C++ and
runs the desktop stub, and every statement below about how iOS behaves is Apple's documented API or Qt's source as
remembered, until the checklist at the end has been run. The design therefore keeps the Objective-C++ thin and puts every
decision in portable C++ that CI can test, and it lists the assumptions that only a device can confirm
([Assumptions](#assumptions-only-a-device-can-confirm)).

## What the stage covers

| Plan row | PRD | Section | Work package |
|---|---|---|---|
| Share sheet | REV-05, DAY-01, PRF-08 (export), NFR-09 | [Share sheet](#share-sheet) | 5.1 |
| Haptics | BRD-03, NFR-03, NOTE-01 | [Haptics](#haptics) | 5.2 |
| (measurement for the exit gate) | NFR-03, NFR-04 | [Timing marks](#timing-marks-and-the-device-build) | 5.3 |
| Data protection | NFR-09, NFR-08, DS-04 | [Data protection](#data-protection) | 5.4 |
| Configuration import | PRF-08, PRF-09, NFR-10 | [Configuration import](#configuration-import) | 5.5 |
| Lifecycle | NOTE-03, DAY-01, NFR-08, NFR-04 | [Lifecycle](#lifecycle) | 5.6 |
| (QML wiring) | BRD-03, REV-05, PRF-08, PRF-09 | [Work packages](#work-packages) | 5.7 |
| Launch screen | NFR-04, NFR-05, NFR-06 | [Launch screen](#launch-screen) | 5.8 |
| Exit gate | NFR-03, NFR-04, REV-05, PRF-08 | [Device checklist](#device-checklist-the-exit-gate) | 5.9 |

## Scope and conventions

- Code lives in `libs/ios_bridge/` (the bridge and its QML-facing wrapper), `libs/storage/src/platform/` (file
  protection, the interface Stage 2 left for this stage), `apps/app/` (wiring, lifecycle, launch screen) and
  `apps/app/packaging/` (property list, entitlements, asset catalogue, `verify_app.sh`).
- **Platform differences.** One header declares each interface; one implementation per platform sits in
  `src/platform/<os>/` with the same file stem: `ios/<name>.mm` and `desktop/<name>.cpp` for the bridge. CMake picks the
  directory (`libs/ios_bridge/CMakeLists.txt` already does) and no source file tests for the platform. No `#if`, no
  `#ifdef`. The `.mm` files are compiled with `-fobjc-arc`; the existing `set_source_files_properties` covers every file
  the glob finds, so a new file gets it without an edit. A policy check keeps the pairs in step (see
  [CI changes](#ci-changes-outside-the-test-suites)).
- **Thin Objective-C++, thick C++.** An `.mm` file does what only UIKit or Foundation can do: call the API, translate the
  result to a plain C++ struct, and nothing else. Every decision (when to prepare, what counts as shared, what to delete,
  which files to protect, what to run on becoming active) is portable C++ against the interface, tested with a fake. The
  `.mm` files cannot be tested in CI, so the less they hold the better.
- **Threads.** Every bridge call is made on the main thread and every callback is delivered on it (the Qt event loop is the
  main thread on iOS). The QML-facing wrapper always delivers a result on a queued call, so a caller sees one order on
  every platform; the spike's stub called back before returning and the phone after.
- **Errors.** Bridge calls report an outcome, never an exception. An Objective-C exception never crosses into C++.
  Portable code returns `std::expected` as in the earlier stages.
- **Naming.** The spike's bridge types are `snake_case` (`platform_services`, `share_result`, namespace
  `error_journaling::bridge`), against the naming rules in `AGENTS.md`. Work package 5.1 renames them mechanically to
  `PascalCase` types, `UPPER_SNAKE_CASE` enumerators and namespace `ej::bridge`, in the same PR that reshapes the
  interface. Sketches below use the target names.
- **Test labels.** `unit` for fakes and pure logic, `integration` for real files, `system` for the kill harness, `ui` for
  the offscreen app, `perf` for budgets. A test that needs an iPhone is a row of the device checklist and is not
  labelled.
- No new dependency. New system frameworks on iOS: `UniformTypeIdentifiers` (document picker), `Foundation` for storage.
  `verify_app.sh` already fails on a link outside the system.
- Every new source file carries the SPDX header. Every user-visible behaviour adds a `changelog.d/` fragment. Fictional
  stand-ins only in tests, fixtures and the checklist (NFR-10).

## What is already built

The Stage 0 spikes ([spikes.md](spikes.md)) left a Qt-free `platform_services` with `present_share_sheet(text, done)`,
`prepare_haptics()` and `fire_haptic(kind)`, an iOS implementation in `src/platform/ios/ios_bridge.mm`, a desktop stub,
and `qml_bridge`, a QObject over it. The spike's findings carry into this stage unchanged: walk
`connectedScenes` for the presenter, report cancelled when `completed` is false with no error, prepare the generators
again after every fire, fire from the press and not the release. Stage 5 turns them into the real features.

## Assumptions only a device can confirm

Each is an assumption this plan depends on, how the plan finds out, and what changes if it is wrong.

| # | Assumption | Checked by | If wrong |
|---|---|---|---|
| A1 | Qt's iOS plugin turns `application:openURL:options:` into a `QFileOpenEvent` for a file opened from AirDrop or Files | First task of 5.5: log the event from a device build; checklist C1 | The bridge gets an `OpenUrlSource` interface with an `.mm` implementation; the portable queue after it is unchanged |
| A2 | `applicationWillResignActive` reaches Qt as `Qt::ApplicationInactive` and `applicationDidEnterBackground` as `Qt::ApplicationSuspended` | L1 logs the sequence | The coordinator listens to the bridge instead of `QGuiApplication` (a small `AppState` interface) |
| A3 | Slack's share extension reports `completed == YES` after a post | S3 records the outcome and activity type | A rule on activity type, decided from the recorded value (open question 2) |
| A4 | `UILaunchScreen` with `UIImageName` shows the image at its natural size, so a 402 x 874 pt image fills an iPhone 17 | K1 | Storyboard instead of an image (open question 4) |
| A5 | QML can read the touch's event timestamp (`point.pressTimestamp` of the pointer handler) and it shares a time base with `NSProcessInfo.systemUptime` | First task of 5.3 | Only the software interval is measured, which is the larger part (see M2) |
| A6 | New files inherit the data-protection class of their directory, and `setAttributes` with `NSFileProtectionKey` sticks | D1 reads every class back, so this is verified whichever way it falls | None: the explicit call after creation is there for this reason |
| A7 | The app's own clock is good enough for the gate: process start from `sysctl`, touch age from the uptime clock (with A5) | M1 and M2 read on the phone | Only the software interval is read and the page says so |
| A8 | `completionWithItemsHandler` also runs when the sheet is dismissed by a tap outside it | S1 | The sheet gets a `presentationControllerDidDismiss` delegate in the `.mm` |

## Interfaces

The bridge stops being one class. One small interface per capability, each with its own header, its own `.mm` and its own
stub, so work packages touch different files and each fake is a few lines.

```text
libs/ios_bridge/include/error_journaling/
  share.hpp  haptics.hpp  document_picker.hpp  perf_marks.hpp  background_tasks.hpp   interfaces and value types
  accessibility_settings.hpp                  text scale and Reduce Motion; owned by Stage 4, package 4.3
  document_types.hpp                          the exported type's identifier and extension, one constant each
  platform_services.hpp                       struct of unique_ptr to the capabilities; make_platform_services()
  qml_bridge.hpp                              the QObject QML sees (Q_INVOKABLE and signals), over the capabilities
libs/ios_bridge/src/platform/ios/             share.mm  haptics.mm  document_picker.mm  perf_marks.mm  background_tasks.mm  accessibility_settings.mm (4.3)
                                              (+ top_view_controller.mm/.hpp, internal, shared by share and picker)
libs/ios_bridge/src/platform/desktop/         share.cpp haptics.cpp document_picker.cpp perf_marks.cpp background_tasks.cpp  accessibility_settings.cpp (4.3)
libs/ios_bridge/src/platform_services.cpp     portable: calls make_share(), make_haptics(), ...; one line per capability
libs/storage/src/platform/<ios dir>/          file_protection.mm (Stage 2 declared the interface and the desktop no-op)
```

`make_platform_services()` is the only place a package edits that another package also edits, and each edit is one line.
The qml_bridge file is shared in the same way: each package adds its own section and the second to merge rebases.
`AccessibilitySettings` (text scale from the content size category, Reduce Motion, and their change signals) is a capability
of this bridge that Stage 4 owns as its package 4.3, because NFR-07 needs Dynamic Type and no Stage 5 package covers it
(Stage 4, decision 5). It follows the same pattern and merges after 5.1.

## Share sheet

**Plan row:** `UIActivityViewController` with the export text, presented from the Qt window's root view controller;
completion reports whether it was shared. **PRD:** REV-05, DAY-01 (the unshared flag), PRF-08 (exporting the setup as a
file), NFR-09 (the share sheet is the only way off the phone).

```cpp
namespace ej::bridge
{

enum class ShareOutcome { SHARED, CANCELLED, UNAVAILABLE, FAILED };

struct ShareRequest
{
    std::string m_Text;                  // the export text, UTF-8; empty when sharing a file
    std::filesystem::path m_File;        // a file to share (the configuration export); empty when sharing text
};                                       // exactly one of the two is set

struct ShareResult
{
    ShareOutcome m_Outcome = ShareOutcome::UNAVAILABLE;
    std::string m_ActivityType;          // what the user chose, e.g. "com.apple.UIKit.activity.CopyToPasteboard"
    std::string m_Detail;                // NSError domain and code only, never text or paths; for logs
};

class Share
{
public:
    virtual ~Share() = default;
    // `done` is called exactly once, on the main thread, after present() has returned.
    virtual void present(ShareRequest request, std::function<void(const ShareResult&)> done) = 0;
};
std::unique_ptr<Share> make_share();     // ios/share.mm or desktop/share.cpp
}
```

What changes from the spike:

- The request carries the real text (Stage 3's `ShareService` builds it with `render_export`) or a file. A file is an
  `NSURL` item, so Mail, Files and AirDrop get a document and not a string.
- The `.mm` still walks `connectedScenes` (single window in v1), maps `completed`, `error` and `activityType` as the spike
  did, and sets the popover anchor so an iPad cannot raise. It additionally checks that the text is valid UTF-8 before
  building the `NSString` (an invalid sequence makes `initWithBytes` return nil), but the portable wrapper does that check
  first so the `.mm` never sees bad bytes.
- **One sheet at a time.** `QmlBridge` keeps a flag while a request is in flight and answers a second request with
  `FAILED` without calling the bridge. QML disables the Share button while `sharing` is true.
- **A file is temporary and deleted.** The configuration export writes `tmp/<name>.ejconfig`, protected like the store
  (see [Data protection](#data-protection)), shares it, and removes it on every outcome. Any file left in that directory
  is removed at launch.
- **What marks a log shared.** Stage 3's `ShareService` records the share time when, and only when, the outcome is
  `SHARED`. Which activities count was decided in question 2 (owner, 2026-11-07): every activity that completes, Copy
  included, because the PRD lists Copy as a share target and the sheet cannot tell a paste from no paste.
- Each outcome is logged as one line, `share: shared com.apple.UIKit.activity.CopyToPasteboard` (outcome and activity type,
  no content), so that what Slack, Mail and Files report is known after the first device pass. The dynamic parts use the
  public log specifier, because a release build redacts dynamic strings from the log by default.

**Tests that run in CI**

- `unit` `share.stub`: the desktop stub reports `UNAVAILABLE` and calls back exactly once.
- `unit` `share.wrapper`: every outcome maps to the QML strings `shared`, `cancelled`, `unavailable`, `failed` and the
  activity type passes through; the result is delivered on a queued call on both a synchronous and a deferred fake; a
  completion after the bridge is destroyed does nothing (the spike's test, kept).
- `unit` `share.single_flight`: a second request while one is in flight is answered `FAILED` and never reaches the
  fake; the flag clears after the first completes, including on `FAILED`.
- `unit` `share.bytes`: the PRD example from `libs/core/tests/golden/prd-example.txt` reaches the fake `Share` as
  byte-identical text, `–` and `→` included; text containing a lone invalid UTF-8 byte gives `FAILED` and no call; text
  and file both set, or neither, gives `FAILED`.
- `integration` `share.config_file`: the configuration export writes a file under a temp directory, the fake receives its
  path, the file is applied the protection call, and it is gone after `SHARED`, after `CANCELLED` and after `FAILED`; a
  stale file in the directory is removed by the launch purge.
- Stage 3's `ShareService` test (not here) asserts that `mark_shared` is called on `SHARED` only. 5.1 adapts that service
  to the new interface if Stage 3 has merged it.

## Haptics

**Plan row:** `UIImpactFeedbackGenerator` prepared on touch-down so the tap fires within the latency budget; a success
haptic for notes. **PRD:** BRD-03, NFR-03, NOTE-01.

The plan's wording and the spike differ, and the spike is right. A generator that is prepared *at* the touch-down and
fired *at* the touch-down has had no time to prepare. The generator must be prepared **ahead** of the touch and fired
**on** the touch-down. So the rule is:

- **Fire** from the press of a tile (`onPressed`, or the pressed change of a `TapHandler`), never from `onClicked` or the
  release.
- **Prepare** when the board becomes visible, when the application becomes active, immediately after every fire, and, if
  the device pass shows the engine idles out between taps minutes apart, on a repeating keep-alive. The engine returns
  to idle a few seconds after a prepare (Apple gives no figure). The keep-alive is one constant in the portable code
  (`HAPTIC_KEEPALIVE`, initially off); M2 decides whether it is switched on and at what interval.
- **Success** fires for a saved note, after the commit has been acknowledged (so it also respects DS-01).

```cpp
enum class HapticKind { IMPACT, SUCCESS };

class Haptics
{
public:
    virtual ~Haptics() = default;
    virtual void prepare() = 0;                // wakes both generators
    virtual void fire(HapticKind) = 0;         // impactOccurred / notificationOccurred(success), then prepare again
};
std::unique_ptr<Haptics> make_haptics();       // ios/haptics.mm or desktop/haptics.cpp (does nothing)
```

`fire` calls `impactOccurred` first and prepares afterwards, so the prepare never sits in the latency path.
`QmlBridge` exposes `prepareHaptics()`, `hapticImpact(pressTimestamp)` and `hapticSuccess()`. The optional argument is
the touch's event timestamp, used only for the timing marks of 5.3.

**The conflict with DS-01.** DS-01 says an entry is committed "before any feedback is shown", and BRD-03 says each tap gives
"a haptic, a tile press animation and the new entry appearing". A haptic at touch-down comes before the commit; the
journal and database writes include full syncs on Apple platforms and may not fit the 100 ms budget with the haptic
behind them. The decision (owner, 2026-11-07) is that the impact haptic and the press animation are feedback that the *touch* landed, the
strip, badge and toast are the feedback that the *entry* is saved and appear only after the commit, and a failed commit
shows an error (and fires nothing). DS-01 therefore says "any confirmation that an entry is saved" instead of "any
feedback". M3 in the checklist measures press-to-commit on the device; should the haptic ever be moved after the commit,
the change is the one line that calls `hapticImpact`. This is decision 1.

**Tests that run in CI**

- `unit` `haptics.stub`: the desktop stub does nothing and does not throw for either kind.
- `unit` `haptics.wrapper`: `hapticImpact` makes exactly one `fire(IMPACT)` and no `prepare` of its own on the wrapper
  (the `.mm` prepares after firing); `hapticSuccess` one `fire(SUCCESS)`; `prepareHaptics` one `prepare`.
- `unit` `haptics.policy`: the portable `HapticPolicy` calls `prepare` on board visible, on application active, and on
  the keep-alive tick when enabled, and on none of them when the application is inactive or the board is hidden. A fake
  clock drives the keep-alive; there are no sleeps.
- `ui` `haptics.on_press` (written in Stage 4, 4.5 and 4.7, against the real Tile): a synthetic press with no release, in the offscreen
  app, reaches the fake `Haptics` once, and a release alone reaches it never.
- `perf` `haptics.press_path` (Stage 4, 4.5 and 4.7): from the synthetic press to the fake's `fire`, p95 over 200 presses under 16 ms on
  the CI runner. This is not the device figure; it fails if someone puts synchronous work (a write, a model update) in the
  press handler ahead of the haptic.
- A source check in the `ui` suite: QML calls `hapticImpact` only from a press handler.

## Configuration import

**Plan row:** an exported document type for the configuration file so it opens from AirDrop and Files, plus a document
picker in Settings. **PRD:** PRF-08, PRF-09, NFR-10.

**The type.** The application declares one exported type:

| Key | Value |
|---|---|
| identifier | `com.iainchesworthlabs.errorjournaling.config` (the bundle identifier plus `.config`) |
| conforms to | `public.json` |
| filename extension | `ejconfig` |
| description | CS Bingo Board setup |

and one document type that names it, role Viewer, handler rank Owner. A dedicated extension means the application is the
only thing the phone offers for the file, instead of appearing in the "Open in" list for every JSON file. It also means
the owner renames the prepared file from `.json` to `.ejconfig` before AirDropping it. Because that is easy to forget, the
plan also registers `public.json` as a second document type with rank Alternate, so a plain `.json` file offers the app
as one choice (open question 3). The identifier and extension are constants in `document_types.hpp`; the property list
check reads the same constants.

**Flow.**

```text
AirDrop / Files "Open in"  -->  Qt  -->  QFileOpenEvent  -->  OpenFileFilter  -+
Settings > Import / first launch -->  DocumentPicker  -->  PickResult ---------+--> ConfigImporter --> SettingsModel
                                                                                    (read, validate, show, delete the copy)
```

```cpp
enum class PickOutcome { PICKED, CANCELLED, UNAVAILABLE, FAILED };
struct PickResult { PickOutcome m_Outcome = PickOutcome::UNAVAILABLE; std::filesystem::path m_Path; };  // a private copy

class DocumentPicker
{
public:
    virtual ~DocumentPicker() = default;
    virtual void pick_configuration(std::function<void(const PickResult&)> done) = 0;   // once, main thread, after return
};
std::unique_ptr<DocumentPicker> make_document_picker();   // ios/document_picker.mm or desktop/document_picker.cpp
```

- **iOS picker:** `UIDocumentPickerViewController` for the exported type and `UTTypeJSON`, `asCopy: YES`, so the system
  hands over a private copy and no security-scoped access is needed. The delegate is held strongly until it answers (the
  controller's `delegate` is weak). Presented from the shared `top_view_controller`.
- **Desktop stub:** `UNAVAILABLE`. The macOS development build uses Qt's own file dialog from QML through the same
  `ConfigImporter`; that is wiring in 5.7.
- **`OpenFileFilter`** (portable QObject, installed on the application before the engine loads): takes `QEvent::FileOpen`,
  ignores anything that is not a local file, and holds the path until the importer says it is ready, so a file that
  launched the app cold is not lost to the start-up order.
- **`ConfigImporter`** (portable): checks the size before reading (the Stage 1 limit of 1 MiB), reads the bytes, calls
  `SettingsModel::import_configuration` (Stage 3), returns the Stage 1 error list for the UI to show, and **deletes the
  file on every outcome**. The imported file holds work-specific values; a copy left in `Documents/Inbox` would be backed
  up and would outlive the import. `purge_import_leftovers()` runs at launch and clears `Documents/Inbox` and the picker's
  temp directory.
- The application never opens a file in place: `LSSupportsOpeningDocumentsInPlace` is `false`, so the system copies the
  file into the sandbox, where the importer owns it. `UIFileSharingEnabled` stays unset, because setting it would list the
  application's Documents folder, and the `Inbox` copy of an imported file in it, in Files and Finder.
- Whether an import replaces the whole setup, and the confirmation sheet before it, are Stage 3 and Stage 4 (the PRD says
  "imported", and the earlier plans treat it as whole-file). Stage 5 delivers the file and the errors.
- **Export** (the other direction of PRF-08) is the share sheet with a file item ([Share sheet](#share-sheet)), writing
  `cs-bingo-board-setup.ejconfig`. It is not in the Stage 5 table, but PRF-08 needs it and it costs one call; it is
  included in Stage 5 (decision 9).

The repository side: `.gitignore` and `tools/ci/check_config_files.py` gain `*.ejconfig`, so a real setup file renamed for
AirDrop cannot be committed (Stage 0.3 only knew `*-config.json`). The CI test copies `config/sample-config.json` to a
temporary `.ejconfig` where it needs one.

**Tests that run in CI**

- `unit` `picker.stub`: the stub reports `UNAVAILABLE` once.
- `unit` `open_file.queue`: a `QFileOpenEvent` posted before `ready()` is delivered once, after it; a second event is
  delivered after the first is handled; a non-file URL, an empty URL and a directory are ignored.
- `integration` `importer.sample`: `config/sample-config.json` copied to a `.ejconfig` and to a `.json` goes through the
  importer to a fake `SettingsModel` and arrives byte for byte; the file is gone afterwards.
- `integration` `importer.errors`: a malformed file, a `schema_version: 2` file and a file over 1 MiB each give the Stage 1
  errors (or the size error without reading the file), leave the fake model untouched, and are deleted.
- `integration` `importer.purge`: files left in a fake `Inbox` and picker directory are removed at launch and nothing
  else is.
- `unit` `importer.picker`: a `PICKED` result is imported and deleted, `CANCELLED` is silent, `FAILED` gives one message.
- `system` `app.open_file`: the offscreen application started with a `QFileOpenEvent` already queued imports the sample
  once the first frame is up.
- Policy `check_ios_plist` (below) checks the declarations; `check_config_files.py` has new unit tests for `*.ejconfig`.

## Data protection

**Plan row:** database and journal files set to complete-until-first-unlock protection. **PRD:** NFR-09, with NFR-08 and
DS-04 (a locked store is not a corrupt one). Stage 2 section 2.10 lists the requirements; this is how they are met.

**The name.** The Foundation constant for the class the plan calls complete-until-first-unlock is
`NSFileProtectionCompleteUntilFirstUserAuthentication`. (`Complete` locks the file whenever the phone locks, which can make a write fail and SQLite's companion files fail to
open while the phone is locked; this class is open from the first unlock after a restart.) The plan and Stage 2 should carry that name.

**Where it is applied.** Stage 2 declared the interface; its shape (the final names follow what Stage 2 merged) is:

```cpp
enum class ProtectionClass { NONE, COMPLETE, COMPLETE_UNLESS_OPEN, COMPLETE_UNTIL_FIRST_USER_AUTHENTICATION, UNSUPPORTED };

class FileProtection
{
public:
    virtual ~FileProtection() = default;
    virtual std::expected<void, StorageError> apply(const std::filesystem::path&, ProtectionClass) = 0;
    virtual std::expected<ProtectionClass, StorageError> read_back(const std::filesystem::path&) = 0;
};
```

The iOS implementation is `file_protection.mm`: `setAttributes:` with `NSFileProtectionKey` and
`attributesOfItemAtPath:` to read it back. The desktop no-op applies nothing and reads back `UNSUPPORTED`.

The portable part is `protect_store(StorageLocation, FileProtection&)` in `ej_storage`, which decides **what**:

1. The data directory and `journal/`, `journal/archive/`, `backup/`, `quarantine/` and the import temp directory, when they
   are created (so new files start in the right class).
2. After the database is opened: the database file, `-wal` and `-shm`. These two are created by SQLite after the open, on
   the first write, and are the easy ones to miss, so the call runs again after the first successful write in a session.
3. A journal file as soon as `create_log` has created it and synced it, and again when it moves to `journal/archive/`.
4. A sweep of every file under the data directory on the first launch after an upgrade, tracked by a marker in
   `store_state`. An every-launch sweep would grow with history and eat into the 1.5 s (NFR-04); the every-launch part is
   the directories, the database, its two companions and the open log's journal, which is a handful of files and is timed
   by 5.3.
5. Then it reads each class back and logs one line (counts and paths only, never content): `protection: 14 files checked,
   14 complete-until-first-user-authentication`. This is what the checklist reads on a device, TestFlight build included.

A failure to apply or read back is **not fatal**. Refusing to open the store because a protection call failed would turn a
privacy hardening into a risk to the open log. It is returned as a warning list and logged, and is visible in Stage 4's diagnostics text
if it has one; it is not shown to the user (decision 11).

**Locked is not corrupt.** Stage 2 classifies a permission or protected-file error as `locked`, never as corruption, so
nothing rebuilds a healthy database. Nothing the application declares (no background mode, notification or push) launches
it before the first unlock, so the case is not reachable by a person on a device and cannot be tested there; Stage 2's
case C13 covers the classification on the desktop. 5.4 adds one table test of the error codes the iOS implementation can
return mapped to `locked`. The codes iOS gives for a protected file are not documented, so the table is a starting
point and is extended if a device ever shows another.

**Property list and entitlement.** An entitlements file, `apps/app/packaging/ios/error-journaling.entitlements`, with
`com.apple.developer.default-data-protection` set to `NSFileProtectionCompleteUntilFirstUserAuthentication`, referenced
from `ej_configure_ios_app` through `XCODE_ATTRIBUTE_CODE_SIGN_ENTITLEMENTS`. This is the default class for new files of
the application, in case the system default ever changes and so that the declaration is in the signed binary and not
only in code. It needs the Data Protection capability on the App ID and a regenerated provisioning profile, which are
owner actions (O1); the entitlement lands in its own pull request, 5.4b, after the owner has done them, because a signed
build with an entitlement the profile lacks fails to sign. Without the entitlement the operating system default for
third-party data is this same class (Apple's platform security documentation says so, and D1 confirms it on the device),
and the per-file calls and the read-back still apply, so the entitlement is
defence in depth and its cost is one owner action (open question 5). No other entitlement is added: no iCloud, no app
group (which also means the suspended-while-locked-file crash, `0xdead10cc`, that affects shared containers does not
apply), no associated domain.

The files stay in the device backup (Stage 2 decision 8); no exclusion flag is set.

**Tests that run in CI**

- `unit` `protect.plan`: against a temporary directory laid out as Stage 2 lays it out, with a fake `FileProtection`
  recording calls, `protect_store` touches exactly: the five directories, the database, the `-wal` and `-shm` once they
  exist, and every journal; not a file outside the data directory.
- `unit` `protect.order`: a journal created by the fake repository is protected before `create_log` returns; a journal
  moved to `journal/archive/` is protected at its new path.
- `unit` `protect.warning`: a fake whose `apply` fails does not fail the open; the warning is in the result; a failing
  `read_back` is a warning too.
- `unit` `protect.sweep`: the full sweep runs once after an upgrade marker and not on the next launch, and the
  every-launch set does not grow with the number of archived journals (1,000 archived journals, the same number of calls).
- `unit` `protect.locked_codes`: each error code in the table maps to `locked` and never to `corrupt`.
- `unit` `protect.stub`: the desktop no-op reports `UNSUPPORTED` and applies nothing.
- `ios-build.yml`: compiles `file_protection.mm` with ARC; `plutil -lint` on the entitlements file; the generated Xcode
  project references it. The signed release job runs `verify_app.sh --signed` with the new entitlement check (below).

## Lifecycle

**Plan row:** on going inactive, flush the note draft and the journal; on becoming active, run the day check. **PRD:**
NOTE-03, DAY-01, NFR-08, with NFR-04 (returning from the background shows the board immediately).

```cpp
enum class LifecycleEvent { BECAME_INACTIVE, BECAME_ACTIVE, ABOUT_TO_QUIT };

class LifecycleCoordinator    // portable; in the Stage 3 services library, or apps/app/src if there is none yet
{
public:
    LifecycleCoordinator(Flushable& storage, DayChecker& days, BackgroundTasks& tasks, HapticPolicy& policy);
    void on_event(LifecycleEvent);
    void storage_ready();       // the day check waits for this
};
```

An adapter connects `QGuiApplication::applicationStateChanged` (`Inactive`, `Suspended` and `Hidden` all raise
`BECAME_INACTIVE`, `Active` raises `BECAME_ACTIVE`) and `aboutToQuit` to it. The adapter is the only Qt-signal code; the
coordinator is plain C++ and runs against fakes.

- **On `BECAME_INACTIVE`:** take a background task (so the flush finishes even if the phone is already going to the
  background), write the pending draft now (`DraftStore::flush()`, Stage 2 section 2.8, which skips the 300 ms debounce),
  wait for any write in flight (`Flushable::drain()`), end the task. Nothing else is buffered: Stage 2 section 2.6
  fsyncs the journal record and commits the database before a write is acknowledged, so "flush the journal" means
  waiting for the one write that may be mid-flight, not syncing something that is pending. If Stage 3 runs the repository
  on the GUI thread `drain()` is a no-op; if on a worker it waits for the queue. Calling it twice in a row (Inactive, then
  Suspended) writes nothing the second time.
- **On `BECAME_ACTIVE`:** tell the `HapticPolicy` (it prepares the haptics); if storage is ready, run the day check (`DayWatcher::check()`, Stage 3); if it
  is not (a cold launch, recovery still running) remember to run it on `storage_ready()`. The day check must be
  idempotent, because Active is also delivered at launch: two events in a row raise one prompt, and a prompt already on
  screen is not raised again. If the new-day prompt is raised, the unshared flag comes from `last_shared_utc` as in
  Stage 3.
- **On `ABOUT_TO_QUIT`:** the same flush as inactive, best effort.
- The day check reads the device's current zone each time, not a zone cached at launch. Qt's `QTimeZone::systemTimeZone`
  is expected to re-query; L5 shows whether it does.
- A share sheet or system dialog that makes the application inactive for a moment causes one cheap flush and nothing else.
- Each event is logged by name (`lifecycle: inactive`) so that the checklist can show the sequence Qt delivers (L1).

```cpp
class BackgroundTasks
{
public:
    using Token = std::uint64_t;
    virtual ~BackgroundTasks() = default;
    virtual Token begin(std::string_view name, std::function<void()> on_expiry) = 0;   // beginBackgroundTaskWithName
    virtual void end(Token) = 0;                                                       // endBackgroundTask
};
std::unique_ptr<BackgroundTasks> make_background_tasks();   // ios/background_tasks.mm or desktop (returns tokens, does nothing)
```

If the task expires before the flush finishes, the expiry handler ends the task and logs; recovery (Stage 2) is the
backstop, as it is for a kill.

**Tests that run in CI**

- `unit` `lifecycle.flush_on_inactive`: `BECAME_INACTIVE` begins one background task, calls `flush` and `drain` in that order,
  and ends the task once; a second `BECAME_INACTIVE` flushes again and writes nothing (fake storage counts writes).
- `unit` `lifecycle.task_expiry`: a task that expires mid-flush ends exactly once and the later flush completion does not
  end it again.
- `unit` `lifecycle.day_check_on_active`: `BECAME_ACTIVE` before `storage_ready` runs the check once, on `storage_ready`;
  after it, on every active; two actives raise one prompt (against the real `DayWatcher` with a fake clock and zone
  provider, the Melbourne cases from Stage 1).
- `unit` `lifecycle.order`: at a cold launch, recovery completes, then storage is ready, then the day check runs; never
  the reverse.
- `unit` `lifecycle.adapter`: the offscreen `QGuiApplication` emits `applicationStateChanged(Inactive)`,
  `(Suspended)`, `(Active)` and `aboutToQuit`, and the coordinator sees the matching events (this does not prove A2, only
  the mapping).
- `unit` `lifecycle.quit`: `ABOUT_TO_QUIT` flushes.
- `system` `draft.inactive_then_kill` (added to Stage 2's kill harness): a child types into the draft, receives an inactive
  event within 100 ms, and is killed with `SIGKILL` at random points after it; the parent reopens and finds the draft
  every time, 100 iterations on the merge queue. This is the CI proof of NOTE-03 for the "type and switch away" case, and
  it is not the same as Stage 2's debounce test.
- `system` `journal.inactive_then_kill`: the same shape with entries; every acknowledged entry is present.

## Launch screen

**Plan row:** matches the board layout so a cold start feels immediate. **PRD:** NFR-04, NFR-05, NFR-06.

The property list already has a `UILaunchScreen` with a colour (`LaunchBackground` in the asset catalogue). The colour is
a placeholder (`#FFC93C`). The real screen is the **Log screen's skeleton**: the background, the header card, the note
composer, the "Last" bar, the 12 tiles in their 3 by 4 grid, and the tab bar, as plain shapes in neutral surface colours,
with no text, no badges and no colour per type (the board order and colours are the facilitator's configuration, which
the launch screen cannot know). The design package gives the frame (402 x 874 pt, a 59 pt top safe area, a 34 pt bottom
inset), the 8 pt tile gap, the 16 pt radius and the colours (`docs/design/tokens.json`); the dark values are designed in
Stage 4.

**Approach (open question 4).** A generated image, not a storyboard: `LaunchBoard` in the asset catalogue, light and
dark variants, 1206 x 2622 px (402 x 874 pt at 3x), opaque, named by `UIImageName` with `UIImageRespectsSafeAreaInsets`
false so it fills the screen under the status bar, over a `UIColorName` background that is the same colour as the board
(`LaunchBackground`, light and dark variants, replacing the placeholder). It is exact on the iPhone 17, which is the v1
device, and it can be checked in CI. On a different iPhone size the image would be centred over the background colour and
the handover would show a small shift; a storyboard with constraints would adapt, at the cost of hand-written layout in
two places. The plan recommends the image for v1 and a storyboard if other iPhone sizes need to be exact.

**How "matches the board" is kept true.** The image is generated from the same QML as the app, so it cannot drift:

1. Stage 4's Log screen builds from shared geometry components (a board grid, the header card, the tab bar). The skeleton,
   `LaunchSkeleton.qml`, lays out the same components in a `skeleton` mode: shapes only, neutral colours.
2. A small desktop tool, `ej_launch_image`, renders the skeleton offscreen at 402 x 874 pt, device pixel ratio 3, with the
   safe-area insets of the design package (59 pt at the top, 34 pt at the bottom), in light and dark, and writes the PNGs.
   They are committed to the asset catalogue. K1 confirms the insets against the device; if they differ, the tokens change
   and the images are regenerated.
3. CI regenerates them and compares with the committed files; a difference fails the build with the instruction to run
   the tool. Software rendering of shapes without text is deterministic, so the tolerance is zero for the tile rectangles
   and a channel difference of 2 elsewhere.
4. A second test loads the **real** Log screen at the same size and insets and checks that the 12 tile rectangles, the
   header card and the tab bar are within 0.5 pt of the skeleton's. So the PNG equals the skeleton, and the skeleton
   equals the real screen.

The first Qt frame must start from the same background and show the board geometry at once: the window colour is the
background token from the first frame, tiles are drawn at once with counts hidden until the log loads, and they accept
taps only when storage is ready. That is the "lightweight placeholder" rule of the standard (section 7.6, first practice).
The launch screen is shown by the system until the first frame, so the handover is two pictures of the same geometry.

iOS caches launch screens: after changing the image, delete the app (or restart the phone) before judging it; a
TestFlight update can keep showing the old one.

**Property list.** `UILaunchScreen` becomes `UIColorName` `LaunchBackground`, `UIImageName` `LaunchBoard`,
`UIImageRespectsSafeAreaInsets` `false`. The `Contents.json` of the colour set gains a dark appearance; a new
`LaunchBoard.imageset` holds the two PNGs.

**Tests that run in CI**

- `ui` `launch.image_current`: the regenerated PNGs equal the committed ones (above), light and dark.
- `ui` `launch.matches_screen`: the skeleton and the real Log screen agree on the geometry of the tiles, header card and
  tab bar within 0.5 pt, at the iPhone 17 size.
- Policy `check_ios_plist`: `UILaunchScreen` names a colour and an image that exist in the asset catalogue, each with
  light and dark variants; the PNGs are 1206 x 2622, opaque; the colour equals the `background` token in both modes.
- `ios-build.yml` / `verify_app.sh`: the built bundle's `Assets.car` contains `LaunchBoard` and `LaunchBackground`
  (`assetutil --info`).
- The look on the device is checklist K1 to K3.

## Timing marks and the device build

**Not a plan row, but the exit gate needs it.** The owner decided (2026-11-07) that there is no Mac, so Instruments is out:
the gate is read from the application's own timing, on a TestFlight build on the iPhone 17. The application therefore
measures M1 and M2 itself and shows them on the phone, and the marks below are also emitted for Instruments in case a
Mac is ever used. Everything is on the monotonic clock (`NSProcessInfo.systemUptime` for taps; the process start time from
`sysctl` for launch), and none of it needs a development-signed build.

**What the measurement is, and is not.** M2 runs from the touch being received to the haptic call returning. It stops at the
call and does **not** include the Taptic Engine's own delay, which is Apple's and is not documented; the feel test covers
that part. M1 runs from process creation to the first frame in which the board is usable.

**The readout.** `QmlBridge.timingSummary` is one line, `launch 812 ms; tap 41 ms (last of 12), median 38 ms, worst
77 ms`, kept by `TimingStats` over the last 50 taps. The shell shows it behind a discreet affordance (five quick taps on
the version text of the spike window; the real shell chooses its own), so it is not in the facilitator's way. There is no
shareable diagnostics text yet; when Stage 4 adds one it includes this line. The same figures go to the log (`launch:` line,
Console) and to Points of Interest.

Two things in the repository remain from the first plan and are harmless:

- A CMake cache variable `ERROR_JOURNALING_IOS_SIGNING_IDENTITY`, default `Apple Distribution` (what `ios.cmake` hard-codes
  now), so the owner can configure `Apple Development` with a development profile. Documented in `docs/building.md`.
- No harness code in the app. The test data is made on the desktop by a small tool, `ej_dataset`, that uses Stage 2's
  one-year generator (about 80,000 entries, fictional) through the real repository, and writes a data directory. **Open
  question for the owner:** with no Mac, "Replace Container" is not available, so there is no way yet to put the
  365-day dataset on the phone. Until one is chosen, M1 is read on the datasets that real use builds (D1, D2) and the
  365-day case is not measured.

```cpp
enum class Mark { PROCESS_START, STORAGE_READY, QML_LOADED, FIRST_FRAME, BOARD_USABLE, TAP_TO_HAPTIC, TOUCH_AGE, TAP_TO_COMMIT };

class PerfMarks
{
public:
    virtual ~PerfMarks() = default;
    virtual void begin(Mark) = 0;
    virtual void end(Mark) = 0;
    virtual void event(Mark, double milliseconds) = 0;    // an instant, with a value
    virtual double ms_since_process_start() = 0;
};
std::unique_ptr<PerfMarks> make_perf_marks();            // ios/perf_marks.mm (os_signpost) or desktop (no-op, steady clock)
```

The iOS implementation emits `os_signpost` intervals and events in the Points of Interest category of a log whose
subsystem is the bundle identifier, so they show in Instruments with no configuration if one is ever used. It also writes the line
`launch: usable at 812 ms (storage 140, qml 380, first frame 610)` with `os_log` when the board becomes usable, using the
time since the process was created (`sysctl` start time of the process). The same figures are on the phone
(the timing readout above), which is how the gate is read. It holds timings only: no content, no network, no
analytics (NFR-09), and it is formatted with the public specifier for the same reason as the share line. The
`protection:` and `lifecycle:` lines are too.

A portable `LaunchTracker` decides when `BOARD_USABLE` fires: on the first `frameSwapped` after storage is ready and the
board is enabled. It is called from `main.cpp` (process start, QML loaded, first frame) and the services (storage ready).
The same tracker times the file protection step of 5.4.

**Tests that run in CI**

- `unit` `launch_tracker.order`: with a fake clock and fake `PerfMarks`, marks arrive in order and `BOARD_USABLE` fires
  exactly once, not before storage ready and the first frame, and not again on later frames.
- `unit` `perf_marks.stub`: the desktop version never throws, and `ms_since_process_start` is non-decreasing.
- `perf` `launch.offscreen`: the offscreen application reaches `BOARD_USABLE` within a budget (1.5 s on the Linux
  release runner, in `perf-budget.json` if the file exists by then). It is a regression guard for work added to the start
  path, and says nothing about the iPhone.
- A CMake script test that the signing identity is `Apple Distribution` when the variable is not given.

## Info.plist and entitlement changes

Everything goes in `apps/app/packaging/ios/Info.plist.in` and the new entitlements file. The property list is a CMake
template: no comment in it may contain a dollar sign followed by a brace, or an at sign pair, which CMake would expand.

| Change | Key | Value | Package |
|---|---|---|---|
| Exported type | `UTExportedTypeDeclarations` | identifier `com.iainchesworthlabs.errorjournaling.config`, conforms to `public.json`, extension `ejconfig`, description "CS Bingo Board setup" | 5.5 |
| Document types | `CFBundleDocumentTypes` | (1) the exported type, role Viewer, `LSHandlerRank` Owner; (2) `public.json`, role Viewer, `LSHandlerRank` Alternate (open question 3) | 5.5 |
| Copy, do not open in place | `LSSupportsOpeningDocumentsInPlace` | `false` | 5.5 |
| Launch screen | `UILaunchScreen` | `UIColorName` `LaunchBackground`, `UIImageName` `LaunchBoard`, `UIImageRespectsSafeAreaInsets` `false` | 5.8 |
| Asset catalogue | `LaunchBackground.colorset`, `LaunchBoard.imageset` | light and dark variants | 5.8 |
| Default data protection | entitlement `com.apple.developer.default-data-protection` | `NSFileProtectionCompleteUntilFirstUserAuthentication` | 5.4b |
| Signing entitlements | `XCODE_ATTRIBUTE_CODE_SIGN_ENTITLEMENTS` in `ios.cmake` | path to the entitlements file | 5.4b |
| Signing identity | `ERROR_JOURNALING_IOS_SIGNING_IDENTITY` in `ios.cmake` | default `Apple Distribution` | 5.3 |

Deliberately **not** added: any usage-description key (the share sheet, haptics and the document picker need none, and
`verify_app.sh` refuses the keys it lists), `UIFileSharingEnabled`, `UISupportsDocumentBrowser`, background modes, iCloud,
app groups, `NSAppTransportSecurity` (no network). The existing keys (encryption declaration, portrait only, full
screen) do not change.

## CI changes outside the test suites

- **`tools/ci/check_ios_plist.py`** (Policy job, runs on Linux): parses `Info.plist.in` with the CMake placeholders
  stubbed and checks the table above: the exported type's identifier and extension equal the constants in
  `document_types.hpp`; the document types name it; `LSSupportsOpeningDocumentsInPlace` is false; `UIFileSharingEnabled`
  is absent or false; no usage-description key; the launch screen's colour and image exist in the asset catalogue with
  light and dark variants; the entitlements file has exactly the one key and value. It has its own unit tests under
  `tools/tests/` with good and bad plists.
- **`tools/ci/check_platform_layout.py`** (Policy job): every `src/platform/ios/<name>.mm` has a `<name>.cpp` in every
  sibling platform directory (and the reverse), no `.mm` exists outside a `src/platform/ios` directory, and the
  CMake glob that adds `-fobjc-arc` covers that directory. It is the mechanical form of the rule in
  `docs/architecture.md`.
- **`verify_app.sh`** gains, for the built bundle: `UTExportedTypeDeclarations` and `CFBundleDocumentTypes` present and
  consistent, `LSSupportsOpeningDocumentsInPlace` false, `UIFileSharingEnabled` not true, the launch image and colour in
  `Assets.car`; and with `--signed`: the code signature's entitlements (`codesign -d --entitlements`) carry
  `com.apple.developer.default-data-protection` with the exact value and no iCloud, app-group or associated-domain
  entitlement.
- `tools/ci/check_config_files.py`, `.gitignore` and the confidentiality scan: `*.ejconfig`.
- `.github/ci/plan.json`: `tools/ci/**` is already "machinery"; `libs/**` and `apps/**` already trigger `ios-build.yml`.
  No change expected; checked in 5.1.
- `tools/coverage-floors.json`: raised for `libs/ios_bridge` and `libs/storage` to what the new portable code reaches.
  The `.mm` files are not compiled on Linux and are not in the figure.
- `tools/preprocessor-baseline.json` does not rise.

## Device checklist (the exit gate)

Run in work package 5.9 on an iPhone 17, with the owner. Nothing here has been run. The results are recorded in a
"Device results" section appended to this file in the 5.9 pull request (as Stage 2 does for its gate), and the Stage 0
spike checklist in `spikes.md` is ticked off from the same run.

### Before starting

| # | Item |
|---|---|
| P1 | Record: iPhone model, iOS version, commit, build number. |
| P2 | iPhone: Developer Mode on; Settings > Sounds & Haptics > System Haptics on; Accessibility > Touch > Vibration on (either off silences every haptic and would fail the haptic rows for no fault of the app); Low Power Mode off; battery above 50 %; the phone not warm. |
| P3 | **Build B:** the TestFlight build of the commit under test. Every row, including M1 and M2, is read from it. |
| P4 | Data, all fictional: **D1** a fresh install with `config/sample-config.json` imported; **D2** a 30-day dataset built by use; **D3** the 365-day dataset (about 80,000 entries) from `ej_dataset`, **not available** until the owner chooses a way to put it on the phone (see Timing marks). |
| P5 | Slack on the phone, signed in, with a private test channel the owner has made for this; the log sent is the fictional one. |
| P6 | A Mac on the same network with AirDrop on, the iPhone set to receive from it. |

### M1: cold start to a usable board (NFR-04), the app's timing line

**"Usable"** means the first frame in which all 12 tiles are drawn and enabled and the open log has been loaded
(recovery finished): the `BOARD_USABLE` mark. **Start** is process creation, as Apple's launch measurements count it. The
home-screen icon animation and the time from the finger to the process starting are outside this figure.

1. Install build B on D1 (and D2 when it exists).
2. Restart the phone. Unlock it. Wait 60 seconds. Open nothing.
3. Open the app from the Home screen. When the board is up, show the timing readout and write down the `launch` figure.
4. Repeat 10 times: quit the app from the app switcher, wait 10 seconds, launch again. The run after the restart is reported
   separately.
5. If a Mac and Console are ever to hand, the `launch:` line in the log gives the same figure with where the time went
   (storage, QML, first frame).

**Pass:** the slowest of the 10 launches is under 1.5 s. Report the median, the run after restart, and each dataset. The
365-day case is open until it can be put on the phone.

### M2: tap to haptic (NFR-03, BRD-03), the app's timing line

The figure is the time from the touch being received to the haptic call returning: `TOUCH_AGE` (the touch's event
timestamp to the press handler: A5) plus the call itself, on the monotonic clock. **It stops at the call returning and does
not include the Taptic Engine's own delay**, which is Apple's and is not documented; the feel test below is how that
part is judged.

1. Build B on D1. Show the timing readout.
2. On the first tile, tap with one finger, lift, wait one second; 50 taps. The readout shows the last, the median and the
   worst of the last 50.
3. Repeat for four conditions: (a) the first tap after a cold launch, (b) after 10 seconds idle, (c) after 60 seconds idle,
   (d) taps every half second. Write down the worst of each. Condition (c) decides whether the keep-alive prepare is needed;
   if (c) fails, turn on `HAPTIC_KEEPALIVE`, make a new build and repeat (c).
4. **Feel test, 10 trials:** rest a finger on the tile and press. The haptic is felt at the moment of contact, not on lift.
   The note's success haptic feels different from the tile's and fires once per saved note.
5. Not gated, but recorded: `TAP_TO_COMMIT`, the interval from the press handler to the acknowledged commit (M3).

**Pass:** the worst of the last 50 taps is under 100 ms in all four conditions (the readout shows the worst, which is
stricter than the 95th percentile the first plan asked for), and the feel test is clean. If `TOUCH_AGE` cannot be read
(A5), the call alone is the figure and the page says so.

### M3: press to commit (informative)

From M2: median and worst of `TAP_TO_COMMIT`. The touch-down haptic stays (decision 1) whatever
the figure; it is recorded for information.

### Functional rows

| # | Check | Pass |
|---|---|---|
| S1 | Review > Share log. The sheet appears over the app with the log text. Swipe it down; tap outside it | No crash; the app logs `share: cancelled` for both (A8) |
| S2 | Share again, choose Copy. Paste into a text file on the Mac (Universal Clipboard) and diff it with the expected export text | `share: shared com.apple.UIKit.activity.CopyToPasteboard`; the diff is empty, `T|` lines, `–` and `→` intact |
| S3 | Share, choose Slack, choose the test channel, post | The message arrives with every line intact; record the outcome and activity type the app logged (A3). The new-day prompt (L4) no longer says the log is unshared |
| S4 | With the sheet up: open the app switcher and return; lock and unlock | No crash; the sheet is either still there or gone, and the Share button works again |
| S5 | Settings > Export setup > AirDrop to the Mac | A `.ejconfig` file arrives; it opens in an editor as JSON; nothing is left in the app's temp folder |
| C1 | The app closed. On the Mac, AirDrop `sample.ejconfig` (the fictional sample, renamed) to the phone and open it | The app launches and offers the import; the setup appears in Settings; no error (A1) |
| C2 | The same with the app open, then with it in the background | Same |
| C3 | AirDrop the same file named `sample.json` | Record what the phone offers; the app is one of the choices (open question 3) and imports |
| C4 | Save the file to Files; tap it in Files. Then Settings > Import setup and pick it in the document picker | Both import |
| C5 | Import a malformed JSON file, a file with `schema_version` 2, and a file over 1 MiB | A plain-English message for each; the existing setup is unchanged |
| C6 | After C1 to C5: Xcode > Window > Devices and Simulators > the app > Download Container (B1) | `Documents/Inbox` and the picker's temp folder are empty |
| C7 | A fresh install: the first-launch offer to import (PRF-09) | The picker imports the sample |
| D1 | Console, `protection:` line, after launch and again after tapping a tile and waiting 5 minutes (B1 and B2) | Every file checked, including `-wal` and `-shm`, reads back complete-until-first-user-authentication |
| D2 | Restart the phone. Leave it locked for 2 minutes. Unlock and open the app within 10 seconds | Opens normally; no recovery message; the entry count is what it was before |
| D3 | Files app > On My iPhone | No CS Bingo Board folder (`UIFileSharingEnabled` is not set, so Documents is not exposed) |
| L1 | B1 with the lifecycle log on: swipe Home; return; pull Control Center; lock; unlock; open the app switcher | The log shows `inactive`/`suspended`/`active` as expected; record the sequence for each (A2) |
| L2 | Type a note draft. Within a second press the side button. Press Stop in Xcode. Relaunch. Repeat 5 times | The draft is there every time |
| L3 | Type a draft, swipe Home, swipe the app away in the switcher, relaunch | The draft is there |
| L4 | Settings > General > Date & Time: turn Set Automatically off, set tomorrow's date; return to the app | The new-day prompt names the new date and says whether the log is unshared; Keep open keeps the log. Put the date back |
| L5 | Same place: set the time zone to Perth; return; tap a tile; open Review | The new entry shows Perth time and the earlier entries still show Melbourne time; the day check uses the new zone. Restore it |
| L6 | Screen-record returning from the app switcher | The board is interactive in the first frame (NFR-04) |
| K1 | Delete the app and reinstall. Start a screen recording. Launch. Step the recording frame by frame around the handover | No visible shift or flash between the launch screen and the first frame; the tile rectangles line up to within 1 pt |
| K2 | The same in dark mode | Same |
| K3 | Open the app from the switcher and from a notification-free cold start | Same background colour as the board at every frame |

Record each row as: pass or fail, the figure where there is one, anything unexpected. A row that fails becomes a fix
before the gate, or an entry in the decisions list with an owner decision.

## Work packages

One pull request each. The plan says Stage 5 may start alongside Stage 4 once Stage 3 is done; 5.1 to 5.4 depend only on
Stages 0 to 2 and could start earlier. 5.7 and 5.8 are UI work and so wait for the Stage 2 gate through Stage 4 (`AGENTS.md`); the other packages are not UI
work.

| # | Package | After | Runs in parallel with Stage 4 |
|---|---|---|---|
| 5.1 | Bridge shape and the share sheet | none | Yes |
| 5.2 | Haptics | 5.1 | Yes |
| 5.3 | Timing marks and the device build | 5.1 | Yes |
| 5.4 | File protection (5.4b: the entitlement) | Stage 2 | Yes (5.4b after owner action O1) |
| 5.5 | Configuration import | 5.1; Stage 3 `SettingsModel` | Yes |
| 5.6 | Lifecycle | Stage 2 `DraftStore`; Stage 3 `DayWatcher` | Yes |
| 5.7 | Launch tracker, picker hookup and the device pass of the wiring | 5.2, 5.5, 5.6; the Stage 4 shell (4.15) | No: needs the screens |
| 5.8 | Launch screen | 5.7; Stage 4 Log screen and dark tokens | No: needs the final geometry and tokens |
| 5.9 | Device pass and the gate record | 5.1 to 5.8; owner actions | No |

After 5.1 the packages 5.2, 5.3, 5.5 and 5.6 touch different files (their own interface, `.mm`, stub and tests), and the
only shared lines are one in `platform_services.cpp` and one section in `qml_bridge`; they can go in any order and the
second to merge rebases. 5.4 touches `libs/storage` and `ios.cmake`, so it conflicts with none of them. `main.cpp` is
touched by Stage 3, Stage 4 and 5.3, 5.5 and 5.6: only one pull request that edits it is open at a time.

### 5.1 Bridge shape and the share sheet

Split the spike's `platform_services` into per-capability headers: `Share` (reshaped here) and `Haptics` (moved, behaviour
unchanged); `DocumentPicker`, `PerfMarks` and `BackgroundTasks` arrive in their own packages. Also the
`PlatformServices` struct and factory, the rename to `ej::bridge` and the naming rules, the file and text request, the
single-flight guard, UTF-8 validation, the queued delivery, the configuration export file path and its cleanup, the
`check_platform_layout.py` policy check, and the update of Stage 3's `ShareService` if it exists. `docs/architecture.md`
for the layout. Tests: those under [Share sheet](#share-sheet).

### 5.2 Haptics

`Haptics` with its `.mm` and stub, `HapticPolicy`, `QmlBridge` methods, the keep-alive constant (off). Tests: those under
[Haptics](#haptics) except the two that need the Tile.

### 5.3 Timing marks and the device build

`PerfMarks`, `LaunchTracker`, the `launch:` log line, `ERROR_JOURNALING_IOS_SIGNING_IDENTITY`, the `ej_dataset` tool, and
the A5 check (the first task: can QML read the touch timestamp), and the on-screen timing readout (`TimingStats`).
`docs/building.md`: the variable, and how to read the timings. Tests: those under [Timing marks](#timing-marks-and-the-device-build).

### 5.4 File protection, and 5.4b the entitlement

`file_protection.mm` and the stub's `read_back`, `protect_store`, the launch log line, the locked-code table. 5.4b:
the entitlements file, the `ios.cmake` reference, the `verify_app.sh --signed` check and the `docs/releasing.md` note on
the Data Protection capability. **5.4b merges only after owner action O1**, or the signed release job fails.

### 5.5 Configuration import

`DocumentPicker` with its `.mm`, `OpenFileFilter`, `ConfigImporter`, the purge, the property list keys of the table,
`check_ios_plist.py` (its first version, covering this package), `*.ejconfig` in `.gitignore` and
`check_config_files.py`. The first task is A1: log the open-file event from a device build. Tests: those under
[Configuration import](#configuration-import).

### 5.6 Lifecycle

`BackgroundTasks` with its `.mm` and stub, `LifecycleCoordinator`, its Qt adapter, the idempotence requirement on
`DayWatcher`, the two kill-harness scenarios. If Stage 3 runs the repository off the GUI thread, a `drain()` is added to
its interface here. Tests: those under [Lifecycle](#lifecycle).

### 5.7 QML wiring

Stage 4 decided (decision 6, owner, 2026-11-07) to write the haptic, share and import call lines itself, in the screens it
builds, against the `qml_bridge` methods that exist from the spike: the tile's press handler calls `hapticImpact`, the
board calls `prepareHaptics` when visible, the note's saved signal calls `hapticSuccess`, the Review screen's Share button
calls the share and shows the result, and Settings' export and import rows and the first-launch offer emit
`importRequested()` and `exportRequested()`. The `ui` tests that need the real Tile (`haptics.on_press` and the
`perf` `haptics.press_path`) are written in Stage 4, packages 4.5 and 4.7.

5.7 keeps what only Stage 5 can do: the **launch tracker** (the window's first frame and enabled board feed
`LaunchTracker`), the **picker hookup** (the shell answers `importRequested()` and `exportRequested()` with the iOS document
picker and the importer; the desktop build uses Qt's file dialog through the same importer), and the **device pass** of the
haptic, share and import rows of the checklist.

### 5.8 Launch screen

`LaunchSkeleton.qml`, `ej_launch_image`, the PNGs and colour sets, the `UILaunchScreen` change, the CI comparisons, the
plist and `verify_app.sh` checks for the launch screen. If Stage 4 did not leave the Log screen's geometry in shared
components, this package makes that small refactor, coordinated so Stage 4 has no other open change in those files.

**As built.** Stage 4 left the geometry in shared components (`HeaderCard`, `NoteComposer`, `LastEntryBar`, `BoardGrid`,
`TabBar`, `SafeFrame`, all in the `Theme` tokens), so the refactor was not needed and `LogScreen.qml` is unchanged:
`screens/launch/LaunchSkeleton.qml` repeats its placing (the 16 pt margins and 12 pt gaps, five lines) and
`launch.matches_screen` is what holds the two to the same rectangles. The skeleton uses the `skeleton` modes that Stage 4
gave each component, with two additions so that the picture does not depend on the font of
the machine that draws it (the plan assumed shapes without text are deterministic, but the placeholder shapes took their
heights and widths from the font's metrics, which differ between Windows, Linux and macOS): `HeaderCard.nominalMetrics` takes
the four lines at 1.2 times their size (`FontRole.nominalPitch`, the pitch of the phone's font) and the right column at 40% of
the card, and the skeleton `TabBar` takes its label's height the same way. The Log screen's own loading placeholder is
unchanged. `launch.same_in_any_font` renders the skeleton in a sans serif, a monospace and a serif font and requires the same
picture. `launch.matches_screen` compares the skeleton with the real shell, filled in as in the design, under the iPhone 17 insets.
The one thing in the real screen that depends on the font is the header card, which is at least 64 pt and as tall as its words,
and the font of a test machine is not the phone's: the case measures how far the real card is from the skeleton's (65.6 pt, the
phone's font at the default size) and requires the card's top and sides, and everything under it, to be where the skeleton has
them once moved by that difference, within half a point. On the phone's font the difference is within half a point, and K1
confirms it. Offscreen Qt has only one font on some machines, so `launch.same_in_any_font` can only compare fonts where the
machine has several.
`launch.image_current` is a `ctest` that runs `ej_launch_image --check`; its comparison is exact inside the tiles (corners
apart) and allows 2 levels of a channel elsewhere. The PNGs are `launch-light.png` and `launch-dark.png` in
`LaunchBoard.imageset`.

### 5.9 Device pass and the gate record

Run the checklist, append "Device results" to this file with the figures, fix or log what fails, tick off the spike
checklist in `spikes.md`, and amend the plan and PRD wording this page changes (below). The owner signs the gate in the
pull request.

## Needed from the earlier stages

| From | Needed | Why |
|---|---|---|
| Stage 2 | `DraftStore::flush()`; the `FileProtection` interface and its desktop no-op; `StorageLocation`'s directory layout; `StorageError::locked`; the kill harness; the one-year dataset generator in a place the `ej_dataset` tool can use | Lifecycle, protection, the device data |
| Stage 3 | `ShareService` over the `Share` interface; `SettingsModel::import_configuration(std::string_view)` returning the Stage 1 errors; an idempotent `DayWatcher::check()`; a storage-ready signal; whether the repository runs off the GUI thread | Share, import, lifecycle |
| Stage 4 | Tile using a press handler; the board visible signal; the Log screen built from shared geometry components with its safe-area insets settable by a test; `Tokens` light and dark; the Settings import and export rows and the first-launch screen; the window colour from the first frame | Haptics, launch screen, import |

## Owner actions

| # | Action | Needed for | When |
|---|---|---|---|
| O1 | Apple Developer portal: on the App ID `com.iainchesworthlabs.errorjournaling`, enable the Data Protection capability set to Complete Until First User Authentication; regenerate the App Store profile; update the repository secret `IOS_PROVISIONING_PROFILE_BASE64` | 5.4b, and every signed build after it | Before 5.4b merges |
| O2 | (Dropped, 2026-11-07: there is no Mac, so no Instruments, no development profile and no registered device.) | | |
| O3 | A private Slack test channel and the Slack app signed in on the iPhone 17. Only the fictional log is posted | S3 | Before 5.9 |
| O4 | A Mac and AirDrop between it and the phone. The test file is the fictional sample renamed, not the facilitator's real setup, which is used in Stage 6 | C1 to C3 | Before 5.9 |
| O5 | Answered on 2026-11-07: all twelve decisions below are accepted as recommended. Nothing further is needed, except O1 for decision 5 | 5.7, 5.5, 5.8, 5.4b | Done |
| O6 | Run, or be present for, the checklist; sign the gate | 5.9 | After 5.8 |
| O7 | A TestFlight build of the final commit (Stage 0.6 pipeline, `SIGN_IOS_UPLOAD` already true) | B | Before 5.9 |

The agent does none of the Apple portal, secret or signing steps (`AGENTS.md`).

## Cross-cutting

- **Documentation:** `docs/architecture.md` (layout and the capability headers, in 5.1), `docs/building.md` (the signing
  identity variable and reading the timings, in 5.3), `docs/releasing.md` (the property list keys, the entitlement
  and the Data Protection capability, in 5.4b and 5.5), `docs/ci.md` (the two policy checks), `docs/testing.md` (the
  device rows are not labelled tests, and where they are recorded), `docs/plan/spikes.md` (results, in 5.9).
- **Changelog fragments:** share log, haptics, setup import and export, protected files, draft kept when the app is closed,
  new-day check on return, launch screen. One per user-visible package.
- **Licence headers:** every new file. **Fictional data:** every fixture and checklist input.
- **No platform `#if`:** the policy checks and `check_no_preprocessor` enforce it; `tools/preprocessor-baseline.json` does
  not rise.
- **Sanitisers:** `linux-debug` and `linux-asan-ubsan` locally before each pull request; the portable lifecycle and
  protection code is in TSan's scope when it runs off the GUI thread.

## What cannot be verified here

- The development machine has no Mac. The `.mm` files are compiled on the hosted macOS runner (`ios-build.yml`) and never
  run in CI. Every row of the device checklist is unrun until 5.9.
- Behaviour of UIKit, the Taptic Engine, Qt's iOS plugin, Slack's extension and the launch-screen cache is taken from
  documentation and memory; the assumptions table lists the ones the plan depends on.
- Before-first-unlock behaviour of the store cannot be produced on a device (nothing launches the application in that
  state); Stage 2's classification test is the only evidence.
- The actuator delay of the haptic is not measurable at all by the app; the timing stops at the call returning, and the feel test covers the rest.
- Each pull request says what has not run on a device.

## Decisions (decided by the owner, 2026-11-07)

**Decided by the plan** (accepted by the owner, 2026-11-07)

- The Stage 0 spike's recommendation stands: a Qt-free bridge created once and owned by a thin QObject, haptics fired from
  the press and prepared ahead, tests against fakes.
- The bridge is one interface per capability, renamed to the project's naming rules in 5.1.
- Decisions live in portable C++ and the `.mm` files stay thin.
- The gate figures come from the app's own timing line on a TestFlight build; there is no Mac and no Instruments.
- The cold-start gate is the slowest of 10 launches on the one-year dataset.
- A failure to apply file protection warns and does not stop the store opening.
- The files stay in the device backup (Stage 2 decision 8).

**Wording this page changes** (accepted by the owner on 2026-11-07; applied in 5.9)

- Implementation plan, Stage 5, Haptics: "prepared on touch-down" becomes "prepared ahead of the touch and fired on the
  touch-down".
- Implementation plan and Stage 2: the constant is `NSFileProtectionCompleteUntilFirstUserAuthentication`.
- Implementation plan exit gate: "measured with Instruments" becomes "read from the app's own timing line on a TestFlight build".
- PRD DS-01: "before any feedback is shown" becomes "before any confirmation that the entry is saved is shown" (decision 1).
- PRD PRF-08: the file has the extension `.ejconfig` and a plain `.json` is also accepted (decision 3).

**Decided** (the owner accepted every recommendation, 2026-11-07; numbering kept so "open question N" references
resolve to decision N)

1. **Haptic before the commit (DS-01).** The impact haptic fires on touch-down, before the entry is committed, and the
   commit confirmation (strip, badge, toast) follows. DS-01 is reworded to speak of confirmation. This unblocks the wiring
   in 5.7, not 5.2. For Stage 3 and 4: record on press, so that a finger sliding off cannot fire a haptic without an
   entry. (owner, 2026-11-07)
2. **What counts as shared.** Any activity that completes, Copy included, because the sheet cannot tell a paste from
   none and the PRD lists Copy as a share target. If Slack reports cancelled although the post arrives (A3), the fix is
   a rule on activity type, and S3 gives the value to base it on. (owner, 2026-11-07)
3. **The file type.** A dedicated extension `.ejconfig` that only this app handles, with plain `.json` accepted as an
   alternate choice, so forgetting to rename the prepared file does not stop the phone offering the app. This unblocks
   5.5. (owner, 2026-11-07)
4. **Launch screen technique.** A generated image, exact on the iPhone 17 and checked in CI, not a storyboard. This
   unblocks 5.8. (owner, 2026-11-07)
5. **The default-data-protection entitlement.** Add it, for the declaration in the signed binary. It needs owner action
   O1 before 5.4b and before any signed build with it. (owner, 2026-11-07)
6. **What the 100 ms figure is.** Touch to the haptic call returning, read from the app's timing line, plus the feel test. A
   physical measurement of the actuator is not in this plan. (owner, 2026-11-07)
7. **Cold-start dataset.** The 365-day dataset is the gate; the 30-day and empty cases are reported. (owner, 2026-11-07)
8. **Timing line on TestFlight.** The figures come from the app's own timing line, read from a TestFlight build on the
   iPhone 17; Instruments and a development-signed build are dropped. (owner, 2026-11-07)
9. **Configuration export.** Included in Stage 5 as a share with a file item, because PRF-08 needs both directions.
   (owner, 2026-11-07)
10. **App switcher snapshot.** No blur or privacy screen is planned; the inactive handler is where one would go if it is
    wanted later. (owner, 2026-11-07)
11. **Protection warnings.** A failure to protect a file is logged only, and is visible in the diagnostics text if Stage 4
    has one; it is not shown to the user. (owner, 2026-11-07)
12. **Order against Stage 3.** Stage 5 follows Stage 3, as the plan has it, and 5.1 adapts `ShareService`.
    (owner, 2026-11-07)
