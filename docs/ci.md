# Continuous integration

The workflows in `.github/workflows/` call reusable workflows in `icl-common-infra`, pinned by
commit SHA. Dependabot proposes the SHA bump; the pull request also needs
`tools/ci/sync_cmake.py` run, because the CMake modules are vendored.

## What runs

| Event | Jobs |
|---|---|
| Draft pull request | Policy, Plan, workflow analysis, dependency scan |
| Ready pull request | Above, plus Linux release and ASan/UBSan builds and coverage |
| Merge queue, push to `develop` | Above, plus Linux debug and TSan, and the macOS desktop builds (debug and release, with the tests; Xcode 27 as a leg that cannot fail the run) |
| Change to what the iOS build reads (a path filter), or a manual run | iOS compile check for a device (`ios-build.yml`, not required) |
| Nightly | CodeQL |
| Tag `v*` | Release: the source archive, and the signed iOS build for TestFlight |

The Plan job reads `.github/ci/plan.json`. A change to documentation only builds nothing. A path
the plan does not recognise builds everything.

## Required checks

Two jobs defined in `ci.yml` are the required checks: **CI Status** and **Scan dependency diff**.
CI Status fails when any job failed or was cancelled, and when a job the plan asked for was
skipped. Do not require checks from the called workflows by name; their names carry the caller's
job name as a prefix.

`ios-build.yml` is a separate workflow because a macOS runner is billed at a multiple and a required check that a path
filter keeps from running never reports. It installs Qt's iOS kit and the macOS host kit (the version in
`.github/toolchain-versions.json`), compiles `apps/app` unsigned with the `ios-release` preset, and
looks inside the device bundle (`apps/app/packaging/ios/verify_app.sh`); it needs no secret. The signed build and the TestFlight upload are the `ios` job of `release.yml`
(`docs/releasing.md`).

There is no Windows leg and no desktop package: Windows, Android and desktop packaging are outside v1. Run every leg on a
branch with `gh workflow run ci.yml --ref <branch>`, and the iOS build with `gh workflow run ios-build.yml --ref <branch>`.
Nothing in CI runs on a device or in the simulator: the bundle is built and inspected, not started.

## Confidentiality check

The **Confidentiality** job of `ci.yml` is part of CI Status. It runs the unit tests of the two checks in
`tools/ci/`, then:

- `check_config_files.py` fails when any tracked file other than `config/sample-config.json` is named `*-config.json`.
- `check_confidential_terms.py` fails on a case-insensitive whole-word match of any term in the tracked files and their
  names, the commit messages and author names of the pull request range (or merge group, or push), and the pull request
  title and body. The title and body are passed through `env:`, never interpolated into the script. The script prints the
  location (file and line, commit and line, title, body line) and never a term.

The term list is the repository secret **`CONFIDENTIAL_TERMS`**: one term per line, blank lines ignored.

**Owner action:** create the secret (Settings, Secrets and variables, Actions, New repository secret) named
`CONFIDENTIAL_TERMS`. Also add it as a Dependabot secret if Dependabot pull requests should be checked before the merge
queue. The list never goes in the repository.

When the secret is missing the job fails on `push` and `merge_group` and on a pull request from this repository. Only a
pull request from a fork, or from Dependabot, which cannot read the secret, warns and passes; the merge queue runs the
check again with the secret, so nothing reaches `develop` unchecked. Failing closed elsewhere means a forgotten secret is
noticed at once instead of silently disabling the guard.

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
