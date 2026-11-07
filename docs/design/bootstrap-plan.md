# Bootstrap plan

How this repository was set up, which parts of the
[C++23 project standard](https://github.com/iainchesworthlabs/icl-common-infra/blob/develop/docs/standard.md) (version
0.14) apply, the values chosen for open items, and what the owner still has to do.

Invocation: `MODE=new NAME=icl-error-journaling KIND=app+lib UI=qt PLATFORMS=ios,macos(desktop, development only),wasm(later, demo and tests)`.

## What was done

1. The repository was created from `icl-cpp-template` in the `iainchesworthlabs` organisation, public.
2. `tools/instantiate.py --name error_journaling --repo icl-error-journaling` renamed the template.
3. `tools/ci/repin.py` moved every `icl-common-infra` reference and `cmake/icl/` to v0.1.19 (`c412e6a`).
4. The sanitised requirements, plan and design package were copied into `docs/` (`prd.md`, `implementation-plan.md`,
   `design/`, `media/`).
5. `main` was created at the template's initial commit, so it is an ancestor of `develop` (D1).

## Names

| Item | Value |
|---|---|
| Repository | `iainchesworthlabs/icl-error-journaling` |
| Application name | CS Bingo Board (renamed from ICL Error Journaling, 8 Nov 2026) |
| iOS display name (`ERROR_JOURNALING_DISPLAY_NAME`) | CS Bingo Board |
| Bundle identifier (`ERROR_JOURNALING_APPLICATION_ID`) | `com.iainchesworthlabs.errorjournaling` |
| C++ project name, CMake prefix | `error_journaling`, `ERROR_JOURNALING_` |
| Targets (implementation plan, Stage 0) | `ej_core`, `ej_storage`, `ej_app`, `ej_ios_bridge` |

## Sections of the standard that apply

| Section | Applies | Notes |
|---|---|---|
| 3, 3.1 Layout, portable libraries | Yes | `ej_core` is QtCore only (plan, Stage 1), so it is not a portable library in the 3.1 sense; the export renderer and parsers are kept free of platform code. |
| 4 Build system, vcpkg fork, toolchain pins | Yes | Qt 6.12.0 from the shared pin, `C:\Repos\common` locally and aqtinstall in CI. |
| 5 Style | Yes | Template `.clang-format` and `.clang-tidy`; camelCase only for QML-facing names (D9). |
| 6 Tests, sanitizers, coverage | Yes | Kill and corruption tests (plan, Stage 2) are `integration`; the 1,000-iteration kill run is nightly. |
| 7 Qt, localisation, fonts | Partly | v1 is English only for one facilitator; D3 languages are deferred and recorded here as a deviation. Right-to-left follows when languages are added. |
| 7.7 Phones and tablets | Yes | iPhone first. Orientation: portrait only (board is 3 x 4 in portrait, design README). |
| 8 Tracy | Desktop debug only | Off on iOS (section 8). |
| 9 SIMD | No | Nothing compute-bound. |
| 10 Platforms | iOS, macOS desktop | Linux builds stay for the PR gate (tests, sanitizers, TSan). Windows, Android and desktop packages are not shipped in v1. WebAssembly is a later demo and test target. |
| 11 CI | Yes | Staged tiers from the template. |
| 12 Administration as code | Yes | Owner applies (below). |
| 13 Releases | iOS only | The desktop package legs of `release.yml` are removed in Stage 0; GPG, SBOM and attestation stay. |
| 14 Documentation | Yes | MkDocs `--strict`. |

## Deviations from the standard, with reasons

- **Platforms.** iOS is the product. macOS desktop exists to iterate on the UI (plan, Stage 4). Linux is the CI test host.
  Windows, Android and desktop packaging are out of scope for v1.
- **Languages (D3).** English only in v1: a single named facilitator, and the log text is consumed by a downstream ticket
  indexer that expects English wording. Revisit if the app is shared with colleagues (PRD, open items).
- **WebAssembly.** Not in Stage 0. Added later as a demo and test target once the services exist.

## Confidentiality (PRD NFR-10)

The real facilitator, sites, unit prefix, robot, error types and tools are not in this repository and must never be.
Code, tests, samples and docs use only the stand-ins from `docs/` (Sam Example, UNIT, North Site, Wrong Response and so
on). Stage 0 adds a CI check that fails when a confidential term appears in the tree; its term list comes from a CI
secret, not from the repository. A `.gitignore` rule and a check reject any `*-config.json` other than
`config/sample-config.json`.

## Developer machine and disk

The system drive of the owner's Windows machine is nearly full. Worktrees go on `E:\wt\icl-error-journaling\<slug>` and
build inside the worktree (`build/` is ignored). The main checkout at `C:\Repos\icl-error-journaling` is not built in.
Shared tool roots stay where they are (`ICL_VCPKG_STORE`, `ICL_VCPKG_MIRROR`, `ICL_QT_ROOT`, `C:\Repos\common`). There is
no Mac on the machine: every macOS and iOS build is verified in CI on the hosted macOS runners.

## Owner actions

Needed now (repository administration, section 12):

1. Done: settings, rulesets and labels have been applied. For reference, the commands were (dry run first):

   ```bash
   ../icl-common-infra/admin/apply.sh --repo iainchesworthlabs/icl-error-journaling
   RELEASE_BOT_APP_ID=<id> OWNER_USER=iainchesworth ../icl-common-infra/admin/apply.sh --repo iainchesworthlabs/icl-error-journaling --apply
   ```

2. Create the `release` environment with yourself as required reviewer.
3. Optional: create the repository secret `CONFIDENTIAL_TERMS` (one confidential term per line; also as a Dependabot secret
   if wanted). Without it the term check warns and is skipped. Details are in `docs/ci.md`.

Needed before the Stage 0 exit gate (a signed build on TestFlight), in Apple's portal and App Store Connect
(`docs/releasing.md`, "iOS and TestFlight"):

1. Register the explicit App ID `com.iainchesworthlabs.errorjournaling`.
2. Create an App Store Connect provisioning profile for it; its base64 goes in the repository secret
   `IOS_PROVISIONING_PROFILE_BASE64`.
3. Create the app record in App Store Connect and an internal TestFlight group with the testers.
4. Set the repository variable `SIGN_IOS_UPLOAD` to `true`.
5. Add the repository to the selected repositories of the Apple organisation secrets (`APPSTORE_API_ISSUER_ID`,
   `APPSTORE_API_KEY_ID`, `APPSTORE_API_PRIVATE_KEY`, `IOS_DIST_CERT_P12_BASE64`, `IOS_DIST_CERT_PASSWORD`). The GPG and
   release-bot secrets and the Dependabot secret `MERGE_QUEUE_TOKEN` are already visible to every repository
   (`icl-common-infra/docs/secrets.md`).

## Pending decisions (from the plan)

Built against the proposal and marked provisional in the golden tests; to be confirmed before Stage 1 closes:

- Ticket-marker spacing: proposed `HH:mm T| <error type>`.
- Header date format: proposed `7 Nov 2026`.
- Dark mode palette (blocks the Stage 4 exit gate).
- Full list of note templates and other error types (blocks Stage 6; lives in the private configuration file).
