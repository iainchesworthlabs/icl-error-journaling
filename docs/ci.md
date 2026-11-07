# Continuous integration

The workflows in `.github/workflows/` call reusable workflows in `icl-common-infra`, pinned by
commit SHA. Dependabot proposes the SHA bump; the pull request also needs
`tools/ci/sync_cmake.py` run, because the CMake modules are vendored.

## What runs

| Event | Jobs |
|---|---|
| Draft pull request | Policy, Plan, workflow analysis, dependency scan |
| Ready pull request | Above, plus Linux release and ASan/UBSan builds and coverage |
| Merge queue, push to `develop` | Above, plus Linux debug and TSan, the macOS desktop builds (debug and release, with the tests; Xcode 27 as a leg that cannot fail the run), and the kill harness at 100 random kills on four legs (below) |
| Change to what the iOS build reads (a path filter), or a manual run | iOS compile check for a device (`ios-build.yml`, not required) |
| Nightly | CodeQL; the kill harness at 1,000 random kills (`kill-nightly.yml`) |
| Manual run of the Stage 2 exit gate | The kill harness at 10,000 random kills on Linux and macOS (`kill-gate.yml`) |
| Tag `v*` | Release: the source archive, and the signed iOS build for TestFlight |

The Plan job reads `.github/ci/plan.json`. A change to documentation only builds nothing. A path
the plan does not recognise builds everything.

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

## Required checks

Two jobs defined in `ci.yml` are the required checks: **CI Status** and **Scan dependency diff**.
CI Status fails when any job failed or was cancelled, and when a job the plan asked for was
skipped (the kill harness's `kill-queue` is one of them in the merge queue). Do not require checks from the called workflows by name; their names carry the caller's
job name as a prefix.

`ios-build.yml` is a separate workflow because a macOS runner is billed at a multiple and a required check that a path
filter keeps from running never reports. It installs Qt's iOS kit and the macOS host kit (the version in
`.github/toolchain-versions.json`), compiles `apps/app` unsigned with the `ios-release` preset, and
looks inside the device bundle (`apps/app/packaging/ios/verify_app.sh`); it needs no secret. The signed build and the TestFlight upload are the `ios` job of `release.yml`
(`docs/releasing.md`).

There is no Windows leg and no desktop package: Windows, Android and desktop packaging are outside v1. Run every leg on a
branch with `gh workflow run ci.yml --ref <branch>`, and the iOS build with `gh workflow run ios-build.yml --ref <branch>`.
Nothing in CI runs on a device or in the simulator: the bundle is built and inspected, not started.

## Platform layout check

The **Plan** job of `ci.yml` runs `tools/ci/check_platform_layout.py`. For a `src/platform` directory that holds both
`ios/` and `desktop/` (the bridge), every `ios/<name>.mm` needs a `desktop/<name>.cpp` and the reverse; a `.mm` file may
exist only under `src/platform/ios`; and the library's CMake must compile that directory with `-fobjc-arc`. A helper
private to the iOS directory (`top_view_controller`) is listed in the script and needs no stub. Its unit tests are in
`tools/tests/test_platform_layout.py`.

## Confidentiality check

The **Confidentiality** job of `ci.yml` is part of CI Status. It runs the unit tests of the two checks in
`tools/ci/`, then:

- `check_config_files.py` fails when any tracked file other than `config/sample-config.json` is named `*-config.json`.
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
```

It needs a checkout of `icl-common-infra` next to this repository or in `ICL_COMMON_INFRA`.
