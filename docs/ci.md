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
| Change to what the iOS build reads (a path filter), or a manual run | iOS compile check, device and simulator (`ios-build.yml`, not required) |
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
`.github/toolchain-versions.json`), compiles `apps/app` unsigned with the `ios-release` and `ios-simulator` presets, and
looks inside the device bundle (`apps/app/packaging/ios/verify_app.sh`); it needs no secret. The signed build and the TestFlight upload are the `ios` job of `release.yml`
(`docs/releasing.md`).

There is no Windows leg and no desktop package: Windows, Android and desktop packaging are outside v1. Run every leg on a
branch with `gh workflow run ci.yml --ref <branch>`, and the iOS build with `gh workflow run ios-build.yml --ref <branch>`.
Nothing in CI runs on a device or in the simulator: the bundle is built and inspected, not started.

## Run the checks locally

```bash
python3 tools/ci/precheck.py
```

It needs a checkout of `icl-common-infra` next to this repository or in `ICL_COMMON_INFRA`.
