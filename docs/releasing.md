# Releasing

1. Merge pull requests into `develop`. Each user-visible change brings a changelog fragment.
2. Assemble the changelog on `develop` with a pull request:
   `python3 <icl-common-infra>/scripts/changelog_tool.py assemble --version 1.2.0 --date YYYY-MM-DD`.
   A final release needs the `## [1.2.0]` section this creates, and the Release workflow fails at its
   first job without it. A pre-release (`v1.2.0-rc.1`) can skip this step: its notes are the fragments
   still in `changelog.d/`, which stay there for the final release. `changelog_tool.py notes --version 1.2.0
   --allow-pending` prints the notes a release would get.
3. Tag the merge commit: `git tag -s v1.2.0 && git push origin v1.2.0`. The tag must point at a
   commit on `develop` and must be semantic version form; a suffix such as `-rc.1` makes a pre-release.
   The version before the suffix must equal `VERSION` in `project()` in `CMakeLists.txt` and `version`
   in `vcpkg.json`; the Release workflow stops at its first job when it does not, so change them in the
   pull request that assembles the changelog.
4. The Release workflow builds the Linux, macOS and Windows packages. Windows binaries are handled
   according to the `SIGN_WINDOWS` repository variable: `false`, `test` or `production`. The Linux package must
   start on Ubuntu 24.04 (glibc 2.39): the `check-glibc-floor` step fails the job when something in it needs a newer
   glibc, which a build on the newest runner can do with a single function. A project that needs a higher floor passes
   `allowed` and says why here.
5. The publish job waits for approval in the `release` environment, then writes `SHA512SUMS`,
   detached GPG signatures, an SPDX SBOM and build provenance, creates the GitHub Release, and for a
   final release fast-forwards `main` to the tag.

`main` only ever moves by that fast-forward. The docs site is deployed from the push to `main`.

## Verifying a download

```bash
gpg --verify SHA512SUMS.asc SHA512SUMS
sha512sum --check SHA512SUMS
gh attestation verify <file> --repo iainchesworthlabs/<repository>
```

The public key is `docs/signing-key.asc`. Its fingerprint is published in `SECURITY.md` of the organisation `.github` repository.

## Windows test signing

Test-signed files carry `-testsigned` in the name and are accepted only on machines that trust the
personal CA. Until a production certificate exists they may go on any release, final or
pre-release, and the notes say so. See `docs/test-signing.md` in `icl-common-infra`.

## Phones and tablets

A project that builds for Android or iOS follows section 7.7 of the standard in `icl-common-infra`, and signs and
distributes an iOS build with the shared actions described in `docs/ios-signing.md` there. An ad-hoc IPA lists the devices'
identifiers and so belongs only in a private repository; neither a release asset nor a workflow artifact is private in a
public one. So the template ships the TestFlight way only.

### iOS and TestFlight

`apps/ios` is a one-screen UIKit application that shows the greeting of the core library. It exists so that the whole
path (compile, sign, check, validate with Apple, upload) works from the first day and can be copied from; a project
replaces the application and keeps the packaging around it: `apps/ios/CMakeLists.txt`, `apps/ios/packaging/`, the `ios`
job of `release.yml` and `ios-build.yml`.

- **`ios-build.yml`** compiles the application unsigned on every change that the iOS build reads, and looks inside the
  bundle with `apps/ios/packaging/verify_app.sh`. It needs no secret. It is not a required check (see `docs/ci.md`).
- **The `ios` job of `release.yml`** runs only when the repository variable `SIGN_IOS_UPLOAD` is `true`; with it unset the
  release is made without iOS. It imports the certificate and profile (`import-ios-signing`), configures with the build
  number of the run, archives and exports an App Store IPA (`export-ios-ipa`), checks the IPA with `verify_app.sh
  --signed --store`, and hands it to `upload-testflight`. A tag, or a manual run with `upload_testflight`, uploads; any
  other run (`gh workflow run release.yml --ref <branch>`) has Apple validate the IPA and stops. The IPA is a workflow
  artifact for 7 days under the name `ios-appstore`, and is never put on the release.
- **One identifier.** `ICL_TEMPLATE_APPLICATION_ID` in `CMakeLists.txt` is the bundle identifier
  (`com.iainchesworthlabs.<app>`, the same string as the Android application id will be). The two workflows repeat it as
  `BUNDLE_ID`, and `verify_app.sh` fails when the bundle says otherwise. `tools/instantiate.py` sets all three.
- **Build number.** `CFBundleVersion` is `ICL_TEMPLATE_IOS_BUILD_NUMBER` (the run number in the release job), or the
  project version when empty. TestFlight refuses a number that is not higher than every earlier one for the same version.
- **Property list.** `ITSAppUsesNonExemptEncryption` is `false`, which is the owner's declaration to Apple that the
  application uses no encryption beyond the system's. Change it only if that stops being true. The check refuses usage
  text for a permission the application does not ask for. Safe areas are the application's: the UIKit sample lays out in
  `safeAreaLayoutGuide`; a Qt Quick application reads `SafeArea.margins` (Qt 6.9 or later).
- **Signing names.** The signing needs, by name only: the organisation secrets `IOS_DIST_CERT_P12_BASE64`,
  `IOS_DIST_CERT_PASSWORD`, `APPSTORE_API_KEY_ID`, `APPSTORE_API_ISSUER_ID` and `APPSTORE_API_PRIVATE_KEY` (shared with
  the repository), the repository secret `IOS_PROVISIONING_PROFILE_BASE64`, and the repository variable `SIGN_IOS_UPLOAD`.

What the owner does in Apple's developer portal and App Store Connect, once per application (details in
`docs/ios-signing.md` of `icl-common-infra`):

1. Identifiers: register an explicit App ID for the bundle identifier.
2. Profiles, Distribution: an **App Store Connect** profile for that App ID and the Apple Distribution certificate, with no
   devices. Its base64 is the secret `IOS_PROVISIONING_PROFILE_BASE64`.
3. App Store Connect, Apps, New App: iOS, the bundle identifier, a name that is unique across the App Store (the name
   under the icon comes from `CFBundleDisplayName`, which is `ICL_TEMPLATE_DISPLAY_NAME`), any SKU.
4. TestFlight tab: an Internal Testing group with the testers.
5. Add the repository to the selected repositories of the organisation secrets above, and set `SIGN_IOS_UPLOAD` to `true`.

The agent that works in the repository does none of these: secrets, variables and the Apple account are the owner's.
