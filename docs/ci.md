# Continuous integration

The workflows in `.github/workflows/` call reusable workflows in `icl-common-infra`, pinned by
commit SHA. Dependabot proposes the SHA bump; the pull request also needs
`tools/ci/sync_cmake.py` run, because the CMake modules are vendored.

## What runs

| Event | Jobs |
|---|---|
| Draft pull request | Policy, Plan, Reuse, workflow analysis, dependency scan |
| Ready pull request | Above, plus Linux release and ASan/UBSan builds, and the screenshots of every screen and the store screenshots at the stores' exact sizes (the `screenshots` artifact, 14 days, with the store sets in `store/`; `tools/ej_screenshots`, `docs/testing.md`). The Screenshots job is not in CI Status and cannot fail the run. Coverage runs too when the pull request carries `deep`; otherwise the merge queue runs it |
| Merge queue | Above, plus Linux debug and TSan, and the kill harness at 100 random kills on four legs (below). The macOS builds run too when the entry touches Apple paths or its pull request carries `deep` (below) |
| Push to `develop` | Everything: the groups above, the macOS desktop builds (debug and release, with the tests), Xcode 27 as a leg that cannot fail the run, and the kill harness. Then Report red and Mark green |
| Change to what the iOS build reads, or a manual run | iOS compile check for a device (`ios-build.yml`, not required). A pull request runs it only for Apple paths; the push to `develop` runs it for any change to `libs/`, `apps/` or the build files |
| Change to what the Android build reads, or a manual run | Android build (`android-build.yml`, not required): the unsigned APK and bundle of `android-release`, checked by `tools/ci/check_android_package.py`. A pull request runs it for the Android paths; the push to `develop` runs it for any change to `libs/`, `apps/` or the build files |
| Push to `develop`, a manual run, or a pull request with the label `android-emulator` | Android emulator smoke test (`android-emulator.yml`, not required, below): the x86_64 build starts on an API 36 emulator and survives Home and a return |
| Change to the documentation site (`docs/`, `mkdocs.yml`, `tools/docs/`, `config/sample-config.json`) | The site build (`docs.yml`). The push to `main` publishes it. The hook `tools/docs/demo_setup_hook.py` adds `demo/demo-setup.ejconfig`, a copy of the sample setup, for App Review (Stage 11, 11.3) |
| Nightly | CodeQL; the kill harness at 1,000 random kills (`kill-nightly.yml`); the weekly CI metrics (`ci-metrics.yml`, Mondays 06:00 UTC) |
| Hourly | The Actions cache prune (`cache-prune.yml`, below) |
| Every 15 minutes | Cancel the runs of merge-queue entries that are gone (`cancel-orphan-queue-runs.yml`, below) |
| Manual run of the Stage 2 exit gate | The kill harness at 10,000 random kills on Linux and macOS (`kill-gate.yml`) |
| Tag `v*` | Release: the source archive, and the signed iOS build for TestFlight |
| Manual run of Store metadata | `store-metadata.yml` uploads the listing in `store/` to App Store Connect or the Play Console with fastlane (`fastlane/Fastfile`, pinned in `Gemfile.lock`). It never uploads a build, submits for review, releases or starts a rollout; `tools/tests/test_store_upload_guard.py` fails if it would. With `screenshots_run` (a push-to-`develop` `ci.yml` run) it downloads that run's `screenshots` artifact and stages the iPhone and iPad sets for deliver (`tools/store/stage_appstore_screenshots.py`); the job has `actions: read` for that. Only from `develop` or a tag (Stage 11, 11.7) |

The Plan job reads `.github/ci/plan.json`. A change to documentation only builds nothing. A path
the plan does not recognise builds everything.

## Where the macOS builds run, and CI reuse

The owner's decisions of 2026-11-07 (`docs/ci-many-agents.md` in `icl-common-infra`, section 9):

- **macOS and iOS are gated after the merge.** The `build-macos` group (debug and release) runs on the push to `develop` and on a
  manual run, not on a pull request. A queue entry runs it too when the **Place the macOS groups** step of the Plan job finds
  that the entry promotes it: a path in `groups.build-macos.promote` of `.github/ci/plan.json` (`platform/macos`,
  `platform/ios`, any `.mm` file, `libs/ios_bridge`, `apps/app/packaging/ios`), or the `deep` label on the pull request
  (read from the API; if that fails, the macOS builds run). A change that breaks only macOS can therefore land, and the push
  run finds it with the merge named. `kill-queue` still has a macOS leg in the queue (the Stage 2 design); that is a separate
  decision.
- **A red `develop` is reported, not blocked.** After a push run, **Report red** opens or updates one `main-red: <group>`
  issue per failed group, naming the pull requests merged since `refs/ci/green/<group>`, and comments once on each. **Mark
  green** then moves `refs/ci/green/<group>` forward for each group that passed. Neither reverts anything or fails the run.
  Those refs live outside `refs/heads` and `refs/tags`; a failure to create one is a warning.
- **Reuse is on.** The **Reuse** job (`find-proof`) looks, in every run, for an earlier run of `ci.yml`
  (a pull request run, a manual run, or an earlier queue entry of the same pull request) whose groups passed on a tree that
  differs only in paths the group does not read. It never fails. The repository variable `CI_REUSE_MODE` is `on` (set on 10 November 2026): a
  group with a valid proof is skipped, `reused` names the run and `CI Status` accepts it. Unset or `shadow` only writes what it
  **would** reuse to the job summary and runs every group; `off` skips the lookup. To revert, `gh variable set CI_REUSE_MODE
  --body shadow`. No reuse for fork pull requests.
- **Proofs.** The last steps of `CI Status` upload the `ci-proof` artifact (14 days): which groups passed and the object ids
  of the barrier paths at the tested commit. A pull request run's tested commit is only known from it.

The groups and the paths they do not read are the `groups` table of `.github/ci/plan.json`; `reuse.barrier` lists the paths
that refuse reuse whenever they differ (workflows, actions, `.github/ci`, the toolchain pins, CMake, the presets, `vcpkg`
files, `tools/ci`, `tools/checks`, `tools/sanitizers`, the coverage floors). A new job name or leg in a group changes `jobs`
there. `ci-metrics.yml` writes the weekly numbers (runner-minutes by event and OS, queue time, runner wait, the age of each
green ref) to its job summary.

To try a change on macOS before it is queued, run `gh workflow run ci.yml --ref <branch>` (every group, once) or, for the
iOS compile, `gh workflow run ios-build.yml --ref <branch>`; label the pull request `deep` to have the queue run macOS.

## The kill harness tiers

The Stage 2 kill harness (`libs/storage/tests/kill`, `docs/testing.md`) runs at four sizes (`docs/plan/stage-2.md`, section 4.4).
Every run uses one master seed, printed first, so any failure is repeated with it.

| Tier | Where | Random kills | Also |
|---|---|---|---|
| Local, and the pull request gate | `ctest -L system`, in the unit and integration phase of the Linux release and ASan/UBSan builds (and every other build) | 25 | every crash point once; random kills of recovery (25), of a migration (25) and of the typed draft (6) |
| Merge queue, push to `develop` | `kill-queue` in `ci.yml`: Linux debug (gcc), ASan/UBSan, TSan (clang), macOS debug | 100 | the same, on one shard of each leg |
| Nightly | `kill-nightly.yml`, 16:41 UTC: Linux release (gcc) and macOS release | 1,000 per leg, in 4 shards of 250, a fresh seed each night | every crash point once per leg; 200 random kills of recovery and 200 of a migration per leg (50 in each shard), the typed draft |
| Exit gate | `kill-gate.yml`, run by hand with a seed | 10,000 per system, in 10 shards of 1,000 | as the nightly |

`kill-harness.yml` is the reusable workflow behind the last three. A plan job (`tools/ci/kill_plan.py`) draws the seed if none was
given and splits the iterations, and a leg for each system and a shard for each range builds only `ej_kill_driver` and
`ej_kill_worker` and runs the driver with `EJ_KILL_SEED`, `EJ_KILL_FIRST` and `EJ_KILL_LAST`. Iteration `i` depends only on the
master seed and `i`, so the shards together run every iteration once, and one failing iteration is repeated on its own with
`ej_kill_driver --seed <seed> --iteration <i>` (docs/testing.md). Shard 0 of each leg also runs the tests that kill at named
crash points (they give the same result every time and are needed once); every shard runs the random ones. A last job
(`tools/ci/kill_summary.py`) adds the shards' result lines up in the run's summary, and fails the run when any run failed or
when fewer random-kill runs were counted than were planned, which is how a shard that died without reporting is caught.

What to look at after a run:

- The run's summary has the tier, the master seed, and a table of runs, failures and acknowledged operations by kind. The seed is
  also the artifact `kill-<tier>-seed`; each shard's `summary.txt` and full `log.txt` are `kill-<tier>-<leg>-shard<n>-summary`
  (kept 90 days).
- A failed iteration prints `FAIL seed=... iteration=... A=... k=...` with its reason, and the store as the kill left it is the
  artifact `kill-failure-<tier>-<leg>-shard<n>` (kept 30 days; the data is synthetic, so it is safe to download). Recover it with
  `Recovery::run`, or repeat the iteration.
- A red nightly opens, or adds to, the standing `nightly` issue (the `Report` job of `kill-nightly.yml`).
- **Time.** A Linux release shard of 1,000 random kills takes about 10 minutes (0.55 s an iteration, measured on the pull request
  gate's 25); an ASan/UBSan iteration takes about twice that. If a macOS shard is too slow for its time allowed, raise the number
  of shards, not the number of iterations.

Run the exit gate with `gh workflow run kill-gate.yml -f seed=0x... [-f iterations=10000] [-f shards=10] [-f systems=both]`. The
workflow runs from the default branch; its result and seed are recorded in `docs/plan/stage-2.md`, section 8.

## Where the time goes, and the limits

Measured on 10 November 2026 over the last 40 merge-queue and 40 pull request runs of `ci.yml`.
The repository is public and its jobs run on GitHub-hosted runners (`ubuntu-26.04`, `ubuntu-24.04`, `macos-26`);
the organisation is on the Free plan, which allows 20 jobs at once (5 on macOS). The self-hosted `tf-gh-*` pool is not
in this repository's runner groups. When a dozen sessions push, the 20 slots are full and even the 10-second jobs wait
10 to 50 minutes for one, so the queue's wall time is mostly waiting.

| Run | Runner-minutes (median, max) | Where it goes |
|---|---|---|
| Pull request | 46, 77 | Linux release 22, ASan/UBSan 9 (26 with a cold ccache), coverage 15 |
| Queue entry, code change | 104, 304; about 170 with the macOS and screenshot legs | Linux release 23, debug 19, TSan 15, ASan/UBSan 9, coverage 20, kill harness 4 legs 22 |
| Queue entry, documents only | about 1 | the static jobs; every build is skipped by the plan |

A code-change queue entry therefore holds about 170 of the 20 x 60 = 1,200 runner-minutes an hour, nearly 15 percent of
the pool, and every pull request push adds about a quarter of that again. A pull request run used to repeat the coverage job that the
queue entry runs on the merged tree; coverage now runs on a pull request only with the `deep` label.

**Job limits.** Healthy Linux build legs ran up to 30 minutes (release 30.0, ASan/UBSan 28.6, TSan 28.7 on a cold
cache), so the old limit of 30 cancelled good jobs. The Linux legs now have 60 minutes. Coverage (45, slowest 27), the
kill harness (90, slowest 11) and the macOS legs (30, slowest 15) keep their limits. The merge queue's own
`check_response_timeout_minutes` is a ruleset setting; it must exceed the time a healthy entry takes under load (the
longest successful entries took 93 and 113 minutes), so it is set to 120.

**Package cache.** From infra v0.1.23 the GCC, LLVM and Qt runtime package installs read their `.deb` files from an Actions cache (`apt-debs-...`, one entry each for gcc, llvm and the Qt packages, saved only by a push to `develop` or a manual run) and run no `apt-get update` when it hits. A cold or failed restore uses the network with four attempts and waits of 20, 60 and 120 seconds. From infra v0.1.24 the GCC packages are downloaded with `curl` first (resuming, dropping a transfer under 200 kB/s, and rotating through the image's mirrors and the main Ubuntu mirrors), because one stalled mirror node failed the merge queue three times on 10 and 11 November 2026. A manual `gh workflow run ci.yml --ref develop` after a toolchain pin or runner image change warms it.

## Orphaned queue runs

When a queue entry is ejected, GitHub deletes its `gh-readonly-queue/...` branch, but its workflow runs carry on and
hold runners; the entry is then re-queued and starts another run. `cancel-orphan-queue-runs.yml` runs every 15 minutes
(`tools/ci/cancel_orphan_queue_runs.py`). It cancels a queued or running `merge_group` run only when the run is over
ten minutes old and GitHub answers 404 for its branch on two lookups 20 seconds apart. Any other answer, including an
error or a rate limit, keeps the run. A live entry's branch exists for as long as its runs do, so it is never cancelled.
A manual run defaults to a dry run: `gh workflow run cancel-orphan-queue-runs.yml -f dry-run=true`.

## Required checks

Two jobs defined in `ci.yml` are the required checks: **CI Status** and **Scan dependency diff**.
CI Status fails when any job failed or was cancelled, and when a job the plan asked for was
skipped (the kill harness's `kill-queue` is one of them in the merge queue; `build-macos` is one when the plan placed it). Do not require checks from the called workflows by name; their names carry the caller's
job name as a prefix.

`ios-build.yml` is a separate workflow because a macOS runner is billed at a multiple and a required check that a path
filter keeps from running never reports. It installs Qt's iOS kit and the macOS host kit (the version in
`.github/toolchain-versions.json`), compiles `apps/app` unsigned with the `ios-release` preset, and
looks inside the device bundle (`apps/app/packaging/ios/verify_app.sh`); it needs no secret. The signed build and the TestFlight upload are the `ios` job of `release.yml`
(`docs/releasing.md`).

There is no Windows leg and no desktop package: Windows, Android and desktop packaging are outside v1. Run every leg on a
branch with `gh workflow run ci.yml --ref <branch>`, and the iOS build with `gh workflow run ios-build.yml --ref <branch>`.
Nothing in CI runs on a device or in the simulator: the bundle is built and inspected, not started.

## Actions cache

The repository's Actions cache holds 10 GB. Past that, GitHub evicts the least recently used entries. On
9 November 2026 the cache held 11.2 GB. Of that, 7 GB was ccache saved from merge-queue and pull-request refs, and the
oldest entry was two days old. The same morning, `bootstrap-vcpkg` failed with HTTP 504 in most queue entries. It
downloads the vcpkg tool from GitHub's releases, and nothing cached that download.

**Who writes.** Only a push to `develop`, a scheduled run and a manual run on `develop` save caches. Pull requests,
merge-queue entries and tags restore `develop`'s entries and save nothing. This is the `cache-write: auto` default of
`setup-vcpkg` and `setup-ccache` in `icl-common-infra`. `build.yml` and `kill-harness.yml` save the ccache only when
`CCACHE_SAVE` is `true`, and `save-vcpkg-cache` checks `VCPKG_CACHE_WRITE`. Every ref can read `develop`'s entries.
A merge-queue ref's entries can never be read again, and a pull request's are read only by that pull request.

**What is cached, and the keys.** Each entry is keyed by OS and architecture (`Linux-X64`, `macOS-ARM64`).

| Entry | Key | Measured, 9 Nov 2026 | Holds |
|---|---|---|---|
| vcpkg tool | `vcpkg-tool-<OS>-<ARCH>-<tool tag>-<hash of vcpkg-tool-metadata.txt>` | about 10 MB each | The bootstrapped `vcpkg` binary. Exact key; a new key only when the vcpkg pin moves to a new tool release |
| vcpkg binaries | `vcpkg-<OS>-<ARCH>-<pin>-<manifest hash>-<lineage>-<content hash>` | 21 MB Linux, 15 MB macOS | Built packages (Catch2; Tracy in the `profiling` lineage). vcpkg names archives by ABI hash, which covers triplet and compiler, so gcc and clang share one entry |
| vcpkg downloads | `vcpkg-downloads-…`, same shape | 7 MB Linux, 1 MB macOS | Port sources and the tools vcpkg fetches itself. vcpkg reads this directory before going upstream, so it is the asset cache |
| ccache | `ccache-<OS>-<ARCH>-<leg>[-kill]-<commit>` | 1.2 GB for 11 legs; ASan/UBSan, TSan and macOS debug are 240 to 400 MB each | One lineage per build leg and kill-harness leg. Coverage restores but does not save |
| Qt kits | `install-qt-action-…` (setup-qt) | 324 MB Linux, 374 MB for each of the two macOS images; the Android `arm64-v8a` kit is one more entry, about 200 MB (an estimate until the first `develop` run measures it) | Saved once per Qt version and runner image |
| pip | `setup-python-…` | 25 MB | The Python packages of the static checks |

Not cached: the clone of the vcpkg fork (GitHub's git service, retried), apt packages, Homebrew and the GCC install.
`setup-vcpkg` runs the bootstrap only when the tool cache has no copy that runs. The bootstrap, the clone and the fetch
retry 5 times, waiting 20, 40, 80 and 160 seconds.

**Budget.** One generation of `develop`'s entries is about 2.4 GB: 1.2 GB of ccache, 1.1 GB of Qt and 0.1 GB for the
rest. Each push to `develop` saves a new ccache generation and leaves the last one behind until it is pruned, so three
pushes in an hour peak at about 4.8 GB. The budget is 8 GB, leaving 2 GB for an Xcode or Qt upgrade, when two kits
exist side by side until the old one is pruned. The prune step warns at 80% of 10 GB.

**Eviction.** `cache-prune.yml` runs every hour at 23 minutes past and on demand. It calls `prune-actions-cache` from
`icl-common-infra`, which deletes entries whose ref is gone (a merged pull request, a merge-queue branch) and every
generation but the newest of each family on a ref. An entry read in the last 20 minutes is kept. A new vcpkg pin, Qt
version or runner image starts a new family, and the old family's entries go when their last reader stops using them.
GitHub also deletes an entry nobody has read for 7 days. A manual run with `dry-run` lists what would go. A manual run
with `purge` set to a key prefix, for example `vcpkg-Linux-X64-`, deletes that family on every ref, which resets a
vcpkg cache that has carried stale archives too long.

**Binary cache choice.** The `files` binary source in `~/.cache/vcpkg/archives`, saved with `actions/cache`. vcpkg
deprecated `x-gha`, which also writes one cache entry per package from every ref. A GitHub Packages NuGet feed would
not count against the 10 GB, but it needs a token with `packages: write` for the writers. It also needs `mono` and
`nuget` on Linux and macOS, one more download that can fail upstream. vcpkg's binaries here are under 40 MB, so the
feed would not change the budget, which is dominated by ccache and Qt.

## QML checks

`tools/ci/check_qml_*.py` keep the QML honest (`docs/plan/stage-4.md`, package 4.1). They read the text of `apps/app/qml/`
and are CTest tests with the label `unit`, so they run in the unit phase of every build; a QML file that fails one fails
the pull request. `python tools/ci/precheck.py` runs these (all but `qml.format` and `qml.lint`) after the shared checks.

| Test | Script | Fails a QML file that |
|---|---|---|
| `qml.platform` | `check_qml_platform.py` | asks which platform it runs on (`Qt.platform`), because what differs arrives as a property or a signal of the bridge |
| `theme.no_literals` | `check_qml_literals.py` | writes a hex colour, `Qt.rgba` and its kin, a named colour, or a number other than 0 for a size, radius, spacing, margin, font size or duration, outside `qml/theme/`. A ratio (`* 2`, `/ 2`) is allowed. A line may end in `// literal-ok: <reason>`; the reasons are printed |
| `i18n.strings` | `check_qml_strings.py` | shows a string literal that is not `qsTr()`, or joins `qsTr()` to something with `+`, or gives it something that is not a literal |
| `qml.tests` | `check_qml_tests.py` | has no `apps/app/tests/ui/<snake_case_name>_test.cpp` with a `TEST_CASE`; a file under a folder named `private` is tested through its neighbour |
| `motion.animators` | `check_qml_motion.py` | has a `NumberAnimation`, `PropertyAnimation`, `SmoothedAnimation` or `SpringAnimation` on `x`, `y`, `scale` or `opacity` (as `property:`, `properties:` or `... on x`), or a `Behavior` on one of them. Those run on the GUI thread; `XAnimator`, `YAnimator`, `ScaleAnimator` and `OpacityAnimator` run on the render thread and hold 120 Hz while a commit runs (U7). An animation of another property (a height, a colour) is not looked at |
| `qml.format` | `check_qml_format.py` | `qmlformat` would change (`apps/app/qml/.qmlformat.ini`; fix with `qmlformat -i`). Skipped, with a message at configure time, by a Qt kit without `qmlformat` |
| `qml.lint` | the `ej_ui_tests_qmllint` target | `qmllint` warns about: `apps/app/qml/.qmllint.ini` sets the maximum to 0 |

The theme is held to less (it is where literals live). `Main.qml` and `shell/AppShell.qml` are tested together by
`app_flow_test.cpp`, which drives the whole app. A file in `qml/platform/ios/` or `qml/platform/desktop/` has a twin of the
same name in the other folder, and the build takes the one for its platform (`qml_files.cmake`).
The unit tests of the scripts are `tools/tests/test_qml_checks.py`, run by the Confidentiality job with the others.

## Licence notices

The build of `apps/app` runs `tools/licences/notices.py` (`cmake/EjAbout.cmake`, the target `ej_notices`) before the link. It reads the
link line of `ej_app` from the CMake file API and fails the build when a linked library is not accounted for, so the iOS build is the
check for the iPhone: an unknown Qt library, a vcpkg port with no copyright file, or a licence with no text stops it. The test
`about.notices` runs the same over a desktop link line in every `ctest` run, and `tools/tests/test_notices.py` over made-up ones. The iOS
workflow prints the result (components, licences, how each is linked and every link input) into its job summary, and `verify_app.sh` checks
that the bundle holds no dynamic library, which is what "static" in the notices means. `ios-build.yml` also runs for a pull request that
changes `cmake/`, `tools/licences/` or `apps/app/CMakeLists.txt`, because they change what the iPhone links. `docs/licensing.md` has the design.

## Platform layout check

The **Plan** job of `ci.yml` runs `tools/ci/check_platform_layout.py`. For a `src/platform` directory that holds both
`ios/` and `desktop/` (the bridge), every `ios/<name>.mm` needs a `<name>.cpp` in `desktop/` and in every other sibling directory (`android/` once Stage 9 adds it), and the reverse; a `.mm` file may
exist only under `src/platform/ios`; and the library's CMake must compile that directory with `-fobjc-arc`. A helper
private to the iOS directory (`top_view_controller`) is listed in the script and needs no stub. Its unit tests are in
`tools/tests/test_platform_layout.py`.

## Android build

`android-build.yml` (Stage 9, package 9.1) runs on `ubuntu-24.04`. It installs:

- Qt's Linux kit for the host tools, then the Android `arm64-v8a` and `armeabi-v7a` kits (all through `setup-qt`, with the pinned
  aqtinstall source the iOS build uses, because aqtinstall 3.3.0 cannot read Qt 6.12's `all_os` repository);
- NDK `27.2.12479018`, `platforms;android-37.0` and `build-tools;37.0.0` with the runner's `sdkmanager`, and the runner's
  JDK 21.

It builds `android-release` (both ABIs, through Qt's multi-ABI build) and runs `tools/ci/check_android_package.py` on the APK and the bundle. The check fails when:

- the merged manifest asks for any permission but the application's own (no `INTERNET`, no `AD_ID`);
- the SDK range is not 28 to 36;
- the application ID, the launcher activity (`EjActivity`, portrait) or `allowBackup` differ;
- a component other than the activity is exported without a permission guarding it;
- an intent filter takes every type (`*/*`, or any type with a `*`): only the setup file's types, JSON and octet-stream,
  are allowed, so that the application is not offered for every file;
- the application has no `dataExtractionRules` or no `fullBackupContent` (the backup rules, which copy the files directory
  and nothing else: `tools/tests/test_android_backup_rules.py` checks their content in the sources), or a component is
  direct-boot aware, which would let the application run before the first unlock with its store locked;
- a packaged library links a library that is neither in the package nor provided by Android at API 28 (the NDK's
  sysroot), which would stop the app at launch with "dlopen failed";
- the program, the Android platform plugin or the SQLite driver is missing (Qt packages plugins only for the Qt modules
  the application links itself, so `android.cmake` links Qt Sql);
- a 64-bit native library is not aligned for 16 KB pages, either in its ELF segments (`llvm-readelf`) or in the zip
  (`zipalign -c -P 16 4`).

Its unit tests are in `tools/tests/test_android_package.py`. The packages are uploaded as the `android-unsigned` artifact
for 7 days. Two cache entries are new: the Qt Android kits for `arm64-v8a` and `armeabi-v7a` (one `setup-qt` entry each per Qt version). No ccache
or Gradle cache is saved, and the NDK is installed each run, outside the cache budget.

## Android emulator smoke test

`android-emulator.yml` (Stage 9, package 9.8) is the one job that runs the Android build, which the build job cannot do.
It runs on a push to `develop`, by hand, and on a pull request that carries the label `android-emulator`; it is not a
required check. The owner's decision 8 (10 November 2026) is that it stays non-blocking for two weeks and is made required
if it is stable, so the pass rate of its runs is written here when that is decided.

It installs the same Qt host kit and NDK as the build job, and the Qt `x86_64` Android kit (one more `setup-qt` cache entry,
about 200 MB, an estimate until the first `develop` run measures it). It builds the `android-emulator` preset (x86_64,
RelWithDebInfo), signs the APK with a key made for the run (neither a secret nor the upload key), runs
`check_android_package.py` on it, allows KVM, and starts an API 36 `google_apis` `x86_64` emulator with
`reactivecircus/android-emulator-runner` (pinned to a commit). The emulator's system image, about 1.5 GB, is downloaded each
run and is not cached.

Then `tools/ci/android_smoke.py` drives the application from outside. A Qt release build takes no start-up arguments, so
there is no smoke-test flag: what the application says is read from logcat, where the bridge writes the lifecycle lines under
the tag `ej`. It fails when:

- the package does not install;
- Package Manager does not offer the application for "Open with" of a `content://` octet-stream file or for "Share to" of
  JSON, or does offer it for an image;
- the application does not reach `lifecycle: active` within 90 s, which is the whole of start-up working (the native
  libraries, the store, the bridge);
- going Home does not give `lifecycle: inactive`, coming back does not give a second `lifecycle: active`, or the process
  changed in between;
- the application's own log has a crash, a missing native library (`dlopen failed`) or a missing Java method;
- the package lacks the backup flag.

The log and a picture of the screen are uploaded as the `android-smoke` artifact for 7 days. The tool's decisions are in
`Smoke`, which `tools/tests/test_android_smoke.py` runs against a fake device, so they are tested on every pull request
without an emulator. The label `android-emulator` is defined in `.github/labels.json`; the owner applies that file.

## Store graphics

`tools/store/make_play_graphics.py` makes the Google Play icon (512 x 512, from the iOS `AppIcon-1024.png`) and the
feature graphic (1024 x 500, the tile palette of `docs/design/tokens.json` and the icon, no text) into
`store/play/en-GB/images/`, which is not committed (Stage 11, 11.5). The output depends only on files in the repository and
the Pillow version in `requirements/store.txt`, so each release can make it again. The job that runs `tools/tests` installs
that file, so `tools/tests/test_play_graphics.py` checks the sizes, the colours and that two runs give the same bytes.

## Store listing text check

The **Plan** job of `ci.yml` runs `tools/ci/check_store_text.py` on `store/` (Stage 11, task 11.1). The upload sends each
text file whole, so the check fails when a file is not UTF-8 with LF line ends and one final newline, has a blank first
or last line or trailing spaces, is over its store's limit (the tables in `docs/plan/stage-11.md`; the App Store keywords
in bytes, the rest in characters), or is missing when the store requires it. It also fails when a keyword is empty,
repeated, 2 characters or fewer, next to a space, or a word of the app's name; when a URL file is not https under the documentation site; when a
category is not an App Store identifier; when the Play title differs from the App Store name; and when there are more than
ten screenshot captions or one is over 60 characters. The confidentiality guard already reads `store/` with every other
tracked file. The unit tests are in `tools/tests/test_store_text.py`.

## The site address

The guide, support and privacy pages are one MkDocs site, published by `docs.yml` to GitHub Pages from a build artifact
(Pages source: GitHub Actions) on a push to `main`. Its address is written in one place: `site_url` in `mkdocs.yml`.

- Pages of `docs/` that need the address write `{{ site_url }}` (it includes the trailing slash) and
  `tools/docs/site_address_hook.py` fills it in at build time. A page never writes the host out.
- The files that cannot read `mkdocs.yml` hold the same address: `store/appstore/en-GB/support_url.txt`,
  `privacy_url.txt`, `marketing_url.txt` and the demo setup address in `store/appstore/review_information/notes.txt`.
  `tools/ci/check_site_address.py` compares them (and `check_store_text.py` reads the host from `mkdocs.yml` too). The
  app's own constant joins the list with the in-app help (Stage 10, 10.6).
- `.github/repo-settings.json` has a `homepage` with the address. It is the owner's file and is not read by a check.

**Moving to a custom domain.** Nothing in the repository is tied to the `github.io` address except the lines above, so
the move is: (1) the owner adds the domain in the repository's Pages settings and the DNS record (a `CNAME` for a
subdomain), which is not a file in the repository, because Pages deployed from a workflow ignores a `CNAME` file; (2) one
pull request changes `site_url` and the files the check names, and it fails until they all agree; (3) the owner updates
the `homepage` in the repository settings. Do this before the store submission: a store URL that changes later needs a
new submission.

## iOS property list check

The **Plan** job of `ci.yml` runs `tools/ci/check_ios_plist.py` on `apps/app/packaging/ios/Info.plist.in` (with the CMake
placeholders stubbed). It fails when the exported type's identifier, description or extension differ from the constants
in `libs/ios_bridge/include/error_journaling/document_types.hpp`, when the document types do not name the exported
type (role Viewer, rank Owner) and `public.json` (rank Alternate), when `LSSupportsOpeningDocumentsInPlace` is not
false, when `UIFileSharingEnabled` is on, when `CADisableMinimumFrameDurationOnPhone` is not true (a ProMotion phone
would run the app at 60 Hz, U7), or when a usage-description key appears. It also reads the privacy manifest,
`apps/app/packaging/ios/PrivacyInfo.xcprivacy`, and fails unless tracking is false, the tracking domains and collected
data types are empty, and each accessed API category is one of Apple's with an application's reason code for it. Its unit
tests are in `tools/tests/test_ios_plist.py`. On the built bundle `verify_app.sh` checks that the manifest is there and
declares each required-reason category the executable calls and carries no Tracy profiler, and reports the network code
that Qt QML links (Qt Network) without refusing it. The **Plan** job also runs `tools/ci/check_no_network.py`, which fails
when the application's code in `apps/` or `libs/` (tests aside) uses a network API (Qt Network, `NSURLSession`,
`CFNetwork`, `Network.framework`, BSD sockets), links `Qt6::Network`, or names a remote URL other than the project's own
pages, which only ever open in the browser. Its unit tests are in `tools/tests/test_no_network.py` (Stage 11, 11.2). `verify_app.sh` repeats the type, the document type, the two flags and the 120 Hz key on the built
bundle. It also checks the launch screen (Stage 5, 5.8): `UILaunchScreen` names a colour and an image that exist in the asset
catalogue, each with a light and a dark entry; the image ignores the safe area (`UIImageRespectsSafeAreaInsets` false); the
two PNGs are 1206 x 2622 px (402 x 874 pt at 3x) and opaque; and the colour is the `background` token in both modes.
`verify_app.sh` then looks for the image and the colour, light and dark, in the built bundle's `Assets.car` (`assetutil --info`).
The test `launch.image_current` (label `ui`, every `ctest` run) renders the launch skeleton again with `ej_launch_image
--check` and fails, with the command that rewrites them, when the PNGs differ from what the QML draws now (exact inside the
tiles, 2 levels of a channel elsewhere). Later packages add the entitlements.

## Confidentiality check

The **Confidentiality** job of `ci.yml` is part of CI Status. It runs the unit tests of the two checks in
`tools/ci/`, then:

- `check_config_files.py` fails when any tracked file other than `config/sample-config.json` is named `*-config.json` or
  `*.ejconfig` (the setup file as the app exports it).
- `check_guide_screenshots.py` fails when the guide's pictures, `docs/guide/screenshots.json` and the screens
  `ej_screenshots` renders disagree: a picture that names an unknown state or mode, a listed picture with no file (or a file
  that is not listed, or not of the list's width), a page that shows a picture that is not listed or has no useful alt
  text, and a listed picture that no page uses. Regenerate them with `tools/docs/guide_screenshots.py` (see "The guide's
  pictures" in `docs/releasing.md`). It needs no build.
- `check_setup_doc.py` fails when `docs/guide/setup-file.md` and the setup-file reader in `libs/core` disagree: a key the
  reader accepts that the page does not list (or the reverse), a key marked required or optional the wrong way, a length
  limit, number range, clock choice or whole-file limit that differs, a `ConfigError::Code` or `Problem` with no row, or
  an example that is not `config/sample-config.json`. The reader is the truth. `tools/ci/precheck.py` runs it too.
- `check_site_address.py` fails when the documentation site's address is written down in more than one way (see "The
  site address").
- `check_help_links.py` fails when the in-app help and the guide disagree (Stage 10, 10.6). It reads
  `apps/app/qml/components/HelpTopics.qml` and the pages of `docs/`: the app's `siteUrl` is not `site_url` of `mkdocs.yml`; a
  topic has fewer than two or more than five paragraphs, or names a page that is not a file of the guide, or a heading
  (`anchor`) that the page does not have (the anchor is the one MkDocs makes of the heading); the pages of the Help group
  (guide, support, privacy) or the heading that "How to fix a setup file" opens are missing; or a screen asks for a topic
  id that does not exist. So a heading of the guide cannot be renamed, or a page moved, without the app's links changing in
  the same pull request. It needs no build. `tools/ci/precheck.py` runs it too. When the site moves to a custom domain,
  change `site_url` and `siteUrl` together (the check fails until they match) and the host allowed in
  `tools/ci/check_no_network.py`, which lets the app name only the project's own pages.
- `check_confidential_terms.py` fails on a case-insensitive whole-word match of any term in the tracked files and their
  names, the commit messages and author names of the pull request range (or merge group, or push), and the pull request
  title and body. The title and body are passed through `env:`, never interpolated into the script. The script prints the
  location (file and line, commit and line, title, body line) and never a term.

The term list is the repository secret **`CONFIDENTIAL_TERMS`**: one term per line, blank lines ignored.

The secret is optional. **Owner action, when wanted:** create it (Settings, Secrets and variables, Actions, New
repository secret) named `CONFIDENTIAL_TERMS`, from a terms file kept outside the repository:
`gh secret set CONFIDENTIAL_TERMS < terms.txt`. Add it as a Dependabot secret too if Dependabot pull requests should be
checked before the merge queue. The list never goes in the repository.

When the secret is missing or empty, the term check prints a warning annotation, does not run and passes, on every
event. That also covers a pull request from a fork or from Dependabot, which cannot read the secret. The choice means the
guard is off, without a failing check, until the secret exists; the warning on each run is the reminder. With terms
present, any match fails the job.

## Run the confidentiality checks locally

```bash
python3 -m unittest discover -s tools/tests
python3 tools/ci/check_config_files.py
CONFIDENTIAL_TERMS="$(cat ~/terms.txt)" python3 tools/ci/check_confidential_terms.py
```

The terms file stays outside the repository.

## Run the checks locally

```bash
python3 tools/ci/precheck.py
python3 tools/ci/check_qml_literals.py   # or platform, strings, tests: one QML check on its own
```

It needs a checkout of `icl-common-infra` next to this repository or in `ICL_COMMON_INFRA`.
