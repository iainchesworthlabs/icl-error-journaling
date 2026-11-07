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
4. The Release workflow archives the source at the tag (the only asset of the GitHub release) and, when
   `SIGN_IOS_UPLOAD` is `true`, builds, signs and uploads the iOS application to TestFlight (below). No desktop package
   is built: macOS and Linux are development and CI platforms only, and Windows is not supported.
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

## Phones and tablets

A project that builds for Android or iOS follows section 7.7 of the standard in `icl-common-infra`, and signs and
distributes an iOS build with the shared actions described in `docs/ios-signing.md` there. An ad-hoc IPA lists the devices'
identifiers and so belongs only in a private repository; neither a release asset nor a workflow artifact is private in a
public one. So the template ships the TestFlight way only.

### iOS and TestFlight

`apps/app` is the Qt Quick application `ej_app` (an empty window with a placeholder for the board). The path from compile
to TestFlight (compile, sign, check, validate with Apple, upload) is `apps/app/packaging/ios.cmake` and
`apps/app/packaging/ios/`, the `ios` job of `release.yml` and `ios-build.yml`. Both workflows install Qt's iOS kit and
the macOS host kit at the version in `.github/toolchain-versions.json`, and the presets chain vcpkg's toolchain to the
kit's. The application is portrait only (`Info.plist.in`).

- **`ios-build.yml`** compiles the application unsigned on every change that the iOS build reads, and looks inside the
  bundle with `apps/app/packaging/ios/verify_app.sh`. It needs no secret. It is not a required check (see `docs/ci.md`).
- **The `ios` job of `release.yml`** runs only when the repository variable `SIGN_IOS_UPLOAD` is `true`; with it unset the
  release is made without iOS. It imports the certificate and profile (`import-ios-signing`), configures with the build
  number of the run, archives and exports an App Store IPA (`export-ios-ipa`), checks the IPA with `verify_app.sh
  --signed --store`, and hands it to `upload-testflight`. A tag, or a manual run with `upload_testflight`, uploads; any
  other run (`gh workflow run release.yml --ref <branch>`) has Apple validate the IPA and stops. The IPA is a workflow
  artifact for 7 days under the name `ios-appstore`, and is never put on the release.
- **One identifier.** `ERROR_JOURNALING_APPLICATION_ID` in `CMakeLists.txt` is the bundle identifier
  (`com.iainchesworthlabs.<app>`, the same string as the Android application id will be). The two workflows repeat it as
  `BUNDLE_ID`, and `verify_app.sh` fails when the bundle says otherwise. `tools/instantiate.py` sets all three.
- **Build number.** `CFBundleVersion` is `ERROR_JOURNALING_IOS_BUILD_NUMBER` (the run number in the release job), or the
  project version when empty. TestFlight refuses a number that is not higher than every earlier one for the same version.
- **Property list.** `ITSAppUsesNonExemptEncryption` is `false`, which is the owner's declaration to Apple that the
  application uses no encryption beyond the system's. Change it only if that stops being true. The check refuses usage
  text for a permission the application does not ask for. Safe areas are the application's: it reads `SafeArea.margins`
  (Qt 6.9 or later).
- **The setup file type.** The property list exports one type of its own, `com.iainchesworthlabs.errorjournaling.config`
  (extension `ejconfig`, conforming to `public.json`), names it in `CFBundleDocumentTypes` with the handler rank Owner,
  and registers `public.json` as an alternate, so a setup file that was not renamed still offers the application.
  `LSSupportsOpeningDocumentsInPlace` is `false` (the system copies the file into the sandbox, and the importer deletes
  it), and `UIFileSharingEnabled` stays unset (it would list the Documents folder in Files). No permission, capability or
  entitlement is involved. `tools/ci/check_ios_plist.py` and `verify_app.sh` check these.
- **Signing names.** The signing needs, by name only: the organisation secrets `IOS_DIST_CERT_P12_BASE64`,
  `IOS_DIST_CERT_PASSWORD`, `APPSTORE_API_KEY_ID`, `APPSTORE_API_ISSUER_ID` and `APPSTORE_API_PRIVATE_KEY` (shared with
  the repository), the repository secret `IOS_PROVISIONING_PROFILE_BASE64`, and the repository variable `SIGN_IOS_UPLOAD`.

What the owner does in Apple's developer portal and App Store Connect, once per application (details in
`docs/ios-signing.md` of `icl-common-infra`):

1. Identifiers: register an explicit App ID for the bundle identifier.
2. Profiles, Distribution: an **App Store Connect** profile for that App ID and the Apple Distribution certificate, with no
   devices. Its base64 is the secret `IOS_PROVISIONING_PROFILE_BASE64`.
3. App Store Connect, Apps, New App: iOS, the bundle identifier, a name that is unique across the App Store (the name
   under the icon comes from `CFBundleDisplayName`, which is `ERROR_JOURNALING_DISPLAY_NAME`), any SKU.
4. TestFlight tab: an Internal Testing group with the testers.
5. Add the repository to the selected repositories of the organisation secrets above, and set `SIGN_IOS_UPLOAD` to `true`.

The agent that works in the repository does none of these: secrets, variables and the Apple account are the owner's.
