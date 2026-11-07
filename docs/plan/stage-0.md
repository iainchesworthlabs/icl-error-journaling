# Stage 0: repository and pipeline

Task breakdown for Stage 0 of the [implementation plan](../implementation-plan.md). Each work package is one pull
request into `develop`.

**Exit gate:** a signed empty app installs from TestFlight on an iPhone 17, and CI is green on all tiers.

## Work packages

### 0.1 Bootstrap (done)

Repository from the template, renamed, infrastructure repinned to v0.1.19, sanitised docs copied in, this plan and the
[bootstrap plan](../design/bootstrap-plan.md).

### 0.2 Qt Quick application and presets

- Pin Qt 6.12.0 in `.github/toolchain-versions.json` (`qt` key).
- Replace the template's UIKit sample in `apps/ios` with an empty Qt Quick application `ej_app` (one window, the board
  area as a placeholder, `qt_add_qml_module` with a URI that differs from the executable name, section 7.1), packaged with
  the template's iOS property list, asset catalogue and `verify_app.sh`.
- Targets: `ej_core` (C++23, QtCore only), `ej_storage` (empty, links `ej_core`), `ej_ios_bridge` (Objective-C++ on iOS,
  desktop stub elsewhere, chosen by CMake under `src/platform/<os>/`), each with a placeholder test.
- Presets: macOS desktop (debug and release), `ios-release` (device), `ios-simulator`; Linux presets stay for the gate.
- CI: macOS desktop build and tests, TSan on Linux, iOS build. Drop the Windows legs and the desktop package legs of
  `release.yml`; keep the iOS release job.
- Update `docs/building.md`, `docs/ci.md`, `docs/architecture.md`, `docs/releasing.md`.

### 0.3 Configuration sample and confidentiality guard

- `config/sample-config.json` with fictional values only.
- `.gitignore` rule for `*-config.json` except the sample, and a check that fails on any other such file.
- A check that fails when a confidential term appears in tracked files, commit messages of the pull request, or the
  pull request text. The term list is read from a repository secret at run time and is never printed or committed.
- Wired into the `CI Status` aggregator, documented in `docs/ci.md`.

### 0.4 Spike: share sheet from Qt

On the empty app: present `UIActivityViewController` with a fixed text from the Qt window's root view controller, and
report whether the user shared or cancelled. Result recorded in `docs/plan/spikes.md`.

### 0.5 Spike: haptic on touch-down

On the empty app: a `UIImpactFeedbackGenerator` prepared ahead and fired on the press (not the release) of a QML item.
Result recorded in `docs/plan/spikes.md`, including what can and cannot be measured without a device.

### 0.6 Signed TestFlight build

After the owner's Apple steps (bootstrap plan, owner actions): rehearse `release.yml` on a branch, read the log for
`VERIFY SUCCEEDED` and `UPLOAD SUCCEEDED`, then install on an iPhone 17.

## Tests

- Placeholder unit test per library target, so every CI leg has something to run.
- `verify_app.sh` on the unsigned iOS build in CI.
- The confidentiality check has its own unit tests with fictional terms.

## What cannot be verified here

The development machine has no Mac. macOS and iOS builds are verified on the hosted runners; the spikes and the
TestFlight install are verified only on a device, and each pull request says which parts have not run on one.

## Exit gate

Passed on 7 Nov 2026. TestFlight 0.1.0 (2) and (3) installed and ran on the owner's iPhone 17, and CI was green on every
tier. Build 3 carried the spike controls; the device results are in [spikes](spikes.md#device-results).
