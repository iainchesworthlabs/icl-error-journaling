# Stage 11: Store release

Task breakdown, test list, release checklist and owner actions for Stage 11 of the
[implementation plan](../implementation-plan.md) ("Stages 7 to 12"). It covers the public release on the App Store and on
Google Play. The listing text drafted for review is in [`store/`](https://github.com/iainchesworthlabs/icl-error-journaling/tree/develop/store).

**Exit gate:** the build is approved by App Review and by Play review, and the owner has released it on both stores.

**Status:** draft for owner review. No code is written for this stage until the owner approves this page. This stage is
delivered last, but its plan comes early. That leaves time for the owner's accounts, agreements and legal advice
([Owner actions](#owner-actions)).

Task 11.0 checked the rules on this page against Apple's and Google's published pages on 10 Nov 2026
([Rule check](#rule-check-10-nov-2026) lists the sources and what changed). The page has not been checked against App
Store Connect or the Play Console themselves, which have the last word when the owner fills them in.

## What the stage covers

| Area | Section | Work package | Comes from |
|---|---|---|---|
| App Store listing text | [Listing](#app-store-listing) | 11.1 (drafted here) | Stage 10 URLs |
| App Store screenshots and preview | [Screenshots](#app-store-screenshots) | 11.4 | Stage 7 `ej_screenshots`, Stage 8 iPad layouts |
| App Privacy label, privacy manifest | [App Privacy](#app-privacy) | 11.2 | The code (verified below) |
| Age rating | [Age rating](#age-rating) | 11.1 (answers drafted here) | — |
| Export compliance | [Export compliance](#export-compliance) | 11.2 | `Info.plist.in` |
| App Review notes and demo setup | [App Review](#app-review-information) | 11.1, 11.3 | Stage 10 hosts the demo file |
| Versions, builds, workflow, phased release | [Release flow](#versions-builds-and-the-release-flow) | 11.6 | `release.yml` |
| Play listing, feature graphic, screenshots | [Play listing](#google-play-listing) | 11.1, 11.4, 11.5 | Stage 9 build |
| Data safety, content rating, audience | [Play app content](#play-app-content) | 11.1 (answers drafted here) | — |
| Play tracks | [Play tracks](#play-tracks) | 11.6 | Stage 9 signing and internal track |
| Licence facts | [Licence](#licence-facts-and-options) | 11.3 (notices) | Owner's legal advice |
| Metadata upload | [Metadata upload](#metadata-upload) | 11.7 | — |
| Checklist and gate | [Release checklist](#release-checklist) | 11.8 | All of the above |

**Depends on:**

- Stage 7: the final screens, and the `ej_screenshots` tool that renders every screen in light, dark and large text from
  stand-in data (planned in Stage 4 as 4.16).
- Stage 8: the iPad layouts in portrait and landscape. The iPad screenshots are taken in these layouts.
- Stage 9: the Android build, Play App Signing, the upload key in CI, and the internal-testing track.
- Stage 10: the support page, the privacy policy page, the marketing page and the demo setup file, all at stable URLs.

The listing text can be reviewed now. The screenshots, the URLs and the iPad and Android facts are filled in when those
stages land.

## App Store

### App Store listing

The drafts are in `store/appstore/en-GB/`. They use the file layout that `fastlane deliver` reads, so 11.7 can upload them
unchanged. The primary language is English (UK). Whether to add English (US) is an owner decision.

| Field | Limit | Draft file | Draft |
|---|---|---|---|
| Name | 30 characters, unique on the store | `name.txt` | Field Test |
| Subtitle | 30 characters | `subtitle.txt` | One-tap session error log |
| Promotional text | 170 characters, can change without a review | `promotional_text.txt` | |
| Description | 4,000 characters | `description.txt` | |
| Keywords | 100 bytes, comma-separated | `keywords.txt` | |
| What's New | 4,000 characters | `release_notes.txt` | First release |
| Support URL | Required | `support_url.txt` | Stage 10 |
| Marketing URL | Optional | `marketing_url.txt` | Stage 10 |
| Privacy policy URL | Required for every app | `privacy_url.txt` | Stage 10 |
| Primary category | — | `../primary_category.txt` | Productivity |
| Secondary category | Optional | `../secondary_category.txt` | Business |
| Copyright | — | `../copyright.txt` | 2026 Iain Chesworth Labs |

The URL files hold the documentation site's address with the path Stage 10 is expected to use. Stage 10 confirms the paths,
or 11.1 corrects them.

Words that stay out of the listing:

- Any name that the confidentiality rule (PRD NFR-10) keeps out of the repository. The only stand-ins used are "the robot",
  UNIT and North Site.
- "Bingo" used to suggest a game or a prize. The board is called a bingo board because it is a grid of tiles. The review
  notes say so, because App Review and the Play content rating both ask about gambling.

### App Store screenshots

Required sets. The app runs on an iPad (Stage 8), so an iPad set is required. Apple's screenshot page also lists the
6.1" class as required. If App Store Connect does not ask for it once a 6.9" set is given, it is left out. Its size is the
iPhone 17's, the frame the capture rig already uses.

| Set | Device class | Pixels, portrait | Points at scale |
|---|---|---|---|
| iPhone 6.9" | iPhone 17 Pro Max class | 1320 × 2868 | 440 × 956 at 3× |
| iPhone 6.1" | iPhone 17 class, if App Store Connect asks for it | 1206 × 2622 | 402 × 874 at 3× |
| iPhone 6.5" | Needed only when no 6.9" set is given, so not made | 1242 × 2688 | 414 × 896 at 3× |
| iPad 13" | iPad Pro 13" class | 2064 × 2752 (and 2752 × 2064 landscape) | 1032 × 1376 at 2× |

Each set has 1 to 10 images, PNG or JPEG, with no transparency. The planned shots are the same for each device class, in this order:

1. The Log board mid-morning: a dozen entries, counts on four tiles, light appearance.
2. The same board in dark appearance.
3. A templated note being written ("Played song").
4. The Review screen with the day's journal.
5. The share sheet step: the plain-text log ready to share. The system sheet itself cannot be rendered on the desktop, so
   this shot shows the text preview that the app shows before the sheet opens.
6. History of earlier days.
7. Settings, with the board setup.
8. iPad only: landscape with the board and the log side by side.

The images come from the app's own QML, rendered by `ej_screenshots` (Stage 7) at the exact pixel sizes above. They are
not drawn by hand. The data is the stand-in dataset (`tools/dataset`, the configuration in `config/sample-config.json`:
Sam Example, UNIT, North Site, Wrong Response and the other types). The rendering is deterministic: the clock is fixed and
the fonts are bundled. CI can make the images again for every release, and a test fails when a screen changes and the
committed list of shots no longer matches. The PNGs are a workflow artifact. They are not committed.

Built in 11.4: `tools/ej_screenshots/store_screenshots.py` and the shot list `tools/ej_screenshots/store_shots.json`
(`docs/testing.md`). Shot 5, the text preview before the share sheet, is not made, because `screens.json` has no state that
shows it. It is added to the list when Stage 7 gives it one. The iPhone set has six shots, and the iPad sets use the
Review, History and board setup screens in place of the share step. The phone captures are 402 × 874 points, so the 6.9"
shot is that capture at a scale of 3, enlarged by 9%; Play's 9:16 phone shots have the screen's colour at each side. The
iPad captures are 820 × 1180 and 1180 × 820, fitted to the store's sizes in the same way.

Two choices for the owner:

- **Status bar.** A desktop render has no iOS status bar. Option (a) draws a neutral one (9:41, full signal and battery)
  in the safe-area band. Option (b) leaves the band empty in the board's background colour. Apple accepts either, as long as
  the shot shows the app in use.
- **Captions.** Option (a) is a plain capture. Option (b) adds a caption band above a scaled capture, for example "One tap
  logs the error and the time". A caption is store text, so it is drafted in `store/appstore/en-GB/screenshot_captions.txt`
  and reviewed with the rest.

### App preview video (optional)

Up to three previews per device class, 15 to 30 seconds each, captured from the device. There is no Mac, so
Simulator recording is not available. A screen recording from the iPhone 17 and the iPad, trimmed to 30 seconds, would
work. The proposal is to skip the preview for 1.0 and decide again after release. This is an owner decision.

### App Privacy

**Answer: Data Not Collected.** Apple counts data as collected only when it is sent off the device, where the developer
or a third-party partner can get at it for longer than it takes to serve a request in real time. The app
sends nothing anywhere. The log leaves the device only when the user shares it through the system share sheet, copies it,
or backs up the device. In each case the user sends it, to a destination they choose. The developer never receives it.

What the code shows, checked on `develop` at 92080f9:

| Check | Evidence | Result |
|---|---|---|
| Qt modules asked for | `find_package(Qt6 … COMPONENTS …)` in `apps/` and `libs/`: Core, Gui, Qml, Quick, Sql, Test (tests only) | No `Network`, `WebSockets`, `WebView`, `Positioning` or `Bluetooth` asked for. Qt QML still links Qt Network on its own (see below) |
| Network APIs in the sources | `QNetwork*`, `QTcp*`, `QUdp*`, `QWebSocket`, `NSURLSession`, `URLSession`, `openUrlExternally` in `apps/` and `libs/` | No calls. The one hit, in `perf_marks.hpp`, is a comment that says "no network" |
| Third-party SDKs | `vcpkg.json` | Catch2 (tests, desktop only). Tracy only with the `profiling` feature, which only the `linux-debug` preset turns on. No analytics, crash reporting or advertising SDK |
| Permissions | `Info.plist.in` has no usage text, and `verify_app.sh` refuses any | None |
| Entitlements | `error-journaling.entitlements` | Only Data Protection. `verify_app.sh --signed` refuses iCloud, app groups and associated domains |
| Tracking | No IDFA, no `AppTrackingTransparency` | No tracking. App Tracking Transparency not needed |

Two things the sources alone cannot prove. 11.2 turns both into checks:

1. **Qt Network is linked, but never called.** The first iOS build with 11.2's check (PR #140) showed that Qt QML
   links Qt Network, because the kit is built with `qml_network` so that QML could load a remote URL. Qt Network in turn
   links `CFNetwork` and `Network`. So the executable carries network code, and the app never calls it. Two checks cover
   this:
   - `verify_app.sh` reports the network code it finds on every build, and fails only on the Tracy profiler.
   - `tools/ci/check_no_network.py` fails when the app's own code uses a network API, links Qt Network, or names a
     remote URL other than the project's own pages, which open in the browser.

   "Data Not Collected" is about what the app sends, and it sends nothing, so the answer stands. On Android the same
   link makes Qt add the `INTERNET` permission, which Stage 9 removes (T11-7).
2. **Privacy manifest.** The repository has no `PrivacyInfo.xcprivacy`. Apple asks for one that declares tracking (none),
   the collected data types (none), and a reason for each "required reason" API the binary calls. A static Qt
   probably calls file-timestamp APIs (`stat`), system boot time (`mach_absolute_time`), and `NSUserDefaults` if
   `QSettings` is used. The exact list comes from the symbols in the signed build, together with any ITMS-91053 warnings
   Apple sent after the TestFlight uploads. The owner can forward those emails or say none came. 11.2 adds the manifest,
   and a check in `check_ios_plist.py` and `verify_app.sh` that it is in the bundle and declares no tracking domains.

The privacy policy page (Stage 10) says the same thing in plain words: no data collected, nothing sent, where the data
lives on the device, how to delete it (delete the app), and a contact address.

### Accessibility Nutrition Labels (optional)

App Store Connect lets an app declare its support for VoiceOver, Voice Control, Larger Text, Dark Interface, Differentiate
Without Colour, Sufficient Contrast and Reduced Motion. Stage 7 audits each of
these. 11.8 declares only what the Stage 7 audit passed on a device.

### Age rating

Apple's questionnaire (the 2025 version, with ratings 4+, 9+, 13+, 16+ and 18+). The
drafted answers are in `store/appstore/age_rating.md`. In short, every content question is "None", and the capability
questions are:

| Question | Answer | Why |
|---|---|---|
| Unrestricted web access | No | No web view, no browser |
| User-generated content shared with others | No | Notes stay on the device. Sharing goes through the system sheet to apps the user picks |
| Messaging or chat | No | |
| Advertising | No | |
| Parental controls, age assurance | No | |
| Gambling, simulated gambling, contests | No | The "bingo board" is a grid of tiles. Nothing is won, wagered or scored |
| Medical or wellness information | No | |

Expected rating: **4+**.

### Export compliance

`Info.plist.in` sets `ITSAppUsesNonExemptEncryption` to `false`. App Store Connect then asks no encryption questions for
each build. The facts behind it:

- The app has no encryption code of its own and no network access.
- At rest, iOS Data Protection encrypts the files. That is the operating system's encryption, which the app only asks for
  through the entitlement and the file protection class.
- Qt Network is linked (see App Privacy) but never called. Its TLS backend uses Apple's system TLS. That is still the
  system's encryption.

Since only the operating system's encryption is used, the app is in the exempt category, and no documentation is uploaded.
This is the owner's declaration to Apple, as `docs/releasing.md` says. 11.8 re-reads Apple's export compliance questions
and the US export rules they cite, so the owner can confirm it.

### App Review information

Drafted in `store/appstore/review_information/notes.txt`. The notes say:

- What the app is for: a facilitator logs, one tap at a time, the errors a robot makes during a session, and shares the
  day's log as plain text.
- There is no account and no sign-in, so no demo account is needed. There is no network access, no server and no hardware
  to connect. The app does not talk to the robot.
- How to try it in two minutes. Path 1: on first launch choose "Set up by hand" and add two error types. Path 2: open the
  demo setup file in Safari, open it in Field Test, and choose "Use this setup". Then tap tiles, undo one, add a note,
  open Review, and share.
- The demo setup file is `config/sample-config.json`, published by Stage 10 on the documentation site as
  `demo-setup.ejconfig`, with stand-in names only. It is the same file the tests already import (11.3 adds a test that the
  published copy equals the sample and imports cleanly). It can also go in the review attachment field.
- "Bingo" means the grid of tiles. There is no game, wager or prize.
- On an iPad the board and the log sit side by side in landscape (Stage 8).

The contact name, telephone and email are typed into App Store Connect by the owner and are not committed. The files
`first_name.txt` and the others are left out of `store/` on purpose. 11.7 skips them.

### Versions, builds and the release flow

**Versions.** The App Store version (`CFBundleShortVersionString`) is the project version in `CMakeLists.txt`. It is
`0.1.0` today. The public release is **1.0.0**. `docs/releasing.md` already makes the tag, `project()` and `vcpkg.json`
agree, and the Release workflow refuses a tag that does not match. Fixes after release are 1.0.x. Features are 1.x.0.

**Build numbers.** `CFBundleVersion` is the release workflow's run number. Apple wants it to rise within a version, and a
run number only ever rises, so it is correct as it is. Android's `versionCode` must be an integer that rises across all
releases. The proposal is that Stage 9 uses the same run number, so one build of one tag has the same number on both
stores. Stage 9 decides.

**From TestFlight to the App Store** (the order the work packages give; the last two steps are the owner's):

1. Changelog assembled, version set to 1.0.0, merged into `develop`, signed tag `v1.0.0` pushed, as in
   `docs/releasing.md`. The tag is the owner's.
2. `release.yml` builds, signs, checks and uploads the IPA to TestFlight (exists today).
3. The build is tested from TestFlight on the iPhone 17 and the iPad: the release checklist below.
4. `store-metadata.yml` (11.7, manual dispatch) uploads the listing text, the screenshots and the review notes to the
   1.0.0 version in App Store Connect. It attaches the build and sets the release options. It never submits.
5. **Owner:** check the version page in App Store Connect, answer what the workflow cannot (age rating, App Privacy,
   pricing and countries, the content rights question), and press **Submit for Review**.
6. **Owner:** after approval, press **Release**, because the version is set to release manually.

**Phased release.** Apple's phased release only affects automatic updates for existing users, over seven days (1%, 2%,
5%, 10%, 20%, 50%, 100%). It can be paused for up to 30 days in all, and new downloads always get the new version. It
applies only to version updates, so 1.0.0 cannot use it. The proposal is: 1.0.0 released manually without a phased release, and every later
version phased and released manually. 11.7 writes these two settings in the metadata. The owner can change them on the
version page.

## Google Play

### Google Play listing

The drafts are in `store/play/en-GB/`, in the layout that `fastlane supply` reads.

| Field | Limit | Draft file |
|---|---|---|
| App name | 30 characters | `title.txt` |
| Short description | 80 characters | `short_description.txt` |
| Full description | 4,000 characters | `full_description.txt` |
| Release notes | 500 characters per language | `changelogs/default.txt` |
| Category | — | Productivity (set in the Console) |
| Contact email, website, privacy policy | Email required, privacy policy required | Stage 10 and the owner |

**Graphics**:

| Asset | Size | Source |
|---|---|---|
| App icon | 512 × 512 PNG, 32-bit | Rendered from the same source as the iOS `AppIcon` (11.5) |
| Feature graphic | 1024 × 500, JPEG or 24-bit PNG, no transparency | Drawn by 11.5 (PR #143): the board's twelve tiles in the token palette and the app icon. No text |
| Phone screenshots | 2 to 8. Each side 320 to 3,840 px, and the long side no more than twice the short side | 1080 × 1920 (9:16), the same eight shots as the App Store. The App Store's 1320 × 2868 is taller than 2:1, so it cannot be reused |
| Large-screen screenshots (tablets and Chromebooks) | At least 4 for the app to be shown on large screens. 1,080 to 7,680 px, 16:9 or 9:16. Google no longer has separate 7" and 10" sets | 1440 × 2560 portrait and 2560 × 1440 landscape, from the Stage 8 layouts |

### Play app content

The drafted answers are in `store/play/app_content.md`.

- **Data safety: no data collected, no data shared.** The form is required even for an app that collects nothing.
  Google counts data as collected when the app sends it off the device. Google's "sharing" is wider than Apple's
  "collect": it includes handing data to another app on the same device. Google exempts a transfer that the user starts
  and expects, so the share sheet export is still not "sharing". Google's page does not name the share sheet, so this
  answer rests on that exemption. The Android build must hold the same facts as iOS: no `INTERNET` permission
  in the merged manifest, and no advertising ID. Qt for Android adds `INTERNET` when Qt Network is linked. 11.2 adds a
  check, which Stage 9 runs on the AAB.
- **Content rating (IARC):** category "Utility, productivity, communication or other". Every content question is No,
  including gambling and user interaction. Expected: Everyone / PEGI 3 / USK 0.
- **Target audience: 18 and over.** It is a workplace tool. Choosing an under-13 age group would bring in the Families
  policy and its rules. This is an owner decision.
- **Other declarations:** no ads, no advertising ID, not a news, government, financial or health app, no account (so no
  account deletion), app access "all functionality is available without special access", with the same two-minute
  instructions as the App Review notes.

### Play tracks

Stage 9 gives the signed AAB, Play App Signing with the upload key in CI, and the internal-testing track. Stage 11 adds:

1. **Internal testing:** each tagged build goes here first (Stage 9).
2. **Closed testing:** a named testers list. **If the Play account is a personal account created after November 2023,
   Google needs a closed test with at least 12 testers opted in for 14 days in a row before it allows production
   access** (confirmed on Google's page). The page names only personal accounts, so this plan assumes an organisation account
(which needs a D-U-N-S number) does not have this rule. Only secondary sources say so outright. This is
   the longest wait in the stage, so it is first in the owner actions.
3. **Production:** a staged rollout (for example 20%, then 100%), started by the owner in the Console. 11.7 can promote a
   release from closed to production as a draft. The owner reviews it and starts the rollout.

Play technical rules Stage 9 must meet before production:

- **Target API.** Since 31 Aug 2026, new apps and updates must target API 36 (Android 16).
- **16 KB memory pages.** Native libraries must support them. A new app needs this now, and an update without it cannot
  be released after 1 Feb 2027.
- **Developer verification.** The package name must be registered under Android developer verification, which Google
  has enforced since 30 Sep 2026. Unregistered apps are removed from Play.

## Both stores

### Licence facts and options

This section sets out facts and options for the owner's legal adviser. It gives no opinion on what is allowed.

**The facts of this repository:**

- The app's own code is GPL-3.0-or-later (`LICENSES/GPL-3.0-or-later.txt`, `REUSE.toml`: "Iain Chesworth Labs
  contributors").
- Commit history: every commit is by the owner's account, except Dependabot's version bumps. Agent-written commits are
  made under the owner's identity with a DCO sign-off. Who holds the copyright (the owner or a company) is for the owner
  to state.
- The source is public on GitHub (`iainchesworthlabs/icl-error-journaling`, public), with a source archive and an SBOM on
  each GitHub release.
- What ships in the app: Qt (Core, Gui, Qml, Quick, Sql, and the iOS platform plugin), under LGPL-3.0 (Qt also offers GPL
  and commercial licences). SQLite comes inside Qt Sql and is public domain. Nothing else third-party ships: Catch2 and
  Tracy are for tests and development only. 11.3 confirms the list from the linked binaries.
- On iOS, Qt's kit is a static build, so Qt is statically linked into the one executable. On Android, Qt for Android
  normally ships as shared libraries in the package. Stage 9 confirms this.
- The app shows no Qt licence notice today, and the bundle carries no licence texts.

**What the GPL-3.0 says** (quoted from `LICENSES/GPL-3.0-or-later.txt`):

- Section 10: "You may not impose any further restrictions on the exercise of the rights granted or affirmed under this
  License."
- Section 6, on User Products: "the Corresponding Source conveyed under this section must be accompanied by the
  Installation Information. But this requirement does not apply if neither you nor any third party retains the ability to
  install modified object code on the User Product (for example, the work has been installed in ROM)."
- Section 7: "'Additional permissions' are terms that supplement the terms of this License by making exceptions from one or
  more of its conditions."

**What the LGPL-3.0 says about a combined work** (section 4; the full text is at gnu.org/licenses/lgpl-3.0.txt and will
be added to the bundle). A combined work may be conveyed "under terms of your choice that, taken together, effectively do
not restrict modification of the portions of the Library contained in the Combined Work and reverse engineering for
debugging such modifications", if the conveyor also:

- (a) gives prominent notice that the Library is used and is covered by the LGPL;
- (b) includes a copy of the GPL and the LGPL;
- (c) includes the Library's copyright notice among any notices shown at run time;
- (d) either (0) conveys the Minimal Corresponding Source and the application in a form that lets the user relink it with
  a modified Library, or (1) uses a suitable shared library mechanism;
- (e) provides Installation Information where section 6 of the GPL would require it.

**What Apple's terms say** (summarised, not quoted. The adviser reads the current text: the Apple Developer Program
License Agreement and its Schedules, the App Store Review Guidelines, and the Licensed Application End User License
Agreement at apple.com/legal/internet-services/itunes/dev/stdeula/):

- Apple distributes apps as the developer's agent. The user gets the app under Apple's standard EULA, unless the
  developer gives its own EULA in App Store Connect. A custom EULA must include the minimum terms Apple lists.
- Apple's standard EULA limits use to Apple devices the user owns or controls, as the Usage Rules allow. It forbids
  transfer, redistribution and sublicensing, and forbids copying, reverse engineering and modification, except as the law
  or the licence terms of included open-source components allow.
- Installing an app on an iPhone needs Apple's signature. A user can install a modified build only with their own Apple
  developer account and a provisioning profile.

**What Google Play's terms say** (summarised. The adviser reads the current Developer Distribution Agreement): the
developer, not Google, grants each user a non-exclusive, worldwide, perpetual licence to use the app (section 5.3 of the
agreement in force from 15 Sep 2025). A developer's own EULA governs if one is given, but where it conflicts with the
agreement, the agreement wins. Android lets users install apps from outside the store.

**History, for context:** in January 2011 VLC for iOS was taken off the App Store after one of its copyright holders
complained that the App Store terms conflicted with the GPL. VLC came back in 2013, after its developers relicensed the
code.

**Options the owner could take advice on** (not ranked, and they can be combined):

1. Distribute as now, as the copyright holder of the app's own code. Advice needed on whether this holds if there are
   other copyright holders, and on the Qt obligations, which are separate.
2. Add a GPL-3.0 section 7 additional permission (an "app store exception") to the app's licence.
3. Relicense the app's own code under a permissive licence (for example MIT or Apache-2.0), or dual-license it.
4. Give a custom EULA in App Store Connect that defers to the GPL for the app's code, and to the LGPL for Qt.
5. Buy a commercial Qt licence for the mobile builds. This removes the LGPL obligations for Qt but does not change the
   app's own licence.
6. Release on Google Play only, and keep iOS on TestFlight.

**Needed under every option except 5:** the LGPL notices for Qt. 11.3 adds them: an "Open-source licences" page in Settings
> About that names Qt and its licence, links to the source on GitHub and to the build instructions in `docs/building.md`,
and shows the GPL and LGPL texts bundled in the app. The listing's "Open source" paragraph is drafted, and its final words
wait for the decision.

### Metadata upload

**Proposal: `fastlane` (`deliver` for the App Store, `supply` for Play)**, run by a new workflow, `store-metadata.yml`, with
manual dispatch only. It uses the App Store Connect API key already shared with the repository (`APPSTORE_API_*`). For
Play it uses a service-account key, which the owner creates and adds as a secret (name proposed:
`PLAY_SERVICE_ACCOUNT_JSON`). fastlane is pinned in a `Gemfile.lock`, and every action in the workflow is pinned to a full
SHA, as `docs/ci.md` requires.

The other way is a small Python script on the App Store Connect API and the Play Developer API. It has fewer dependencies
but more code to own. The owner decides.

Whichever is chosen, the upload:

- runs `--skip_binary_upload`. Builds come only from `release.yml`;
- sets `submit_for_review: false` and `automatic_release: false`, and `supply`'s release status `draft`. Submission and
  release stay with the owner;
- makes a dry run (`deliver --verify_only`, or a `supply` validate) on a pull request that changes `store/`. It needs the
  secrets, so it runs only on the owner's branches and on manual runs.

### Release checklist

Run for 1.0.0, and again for each release. 11.8 adds it to `docs/releasing.md`.

**Before the tag**

- [ ] Stages 7 to 10 exit gates passed. Stage 12 decided by the owner.
- [ ] `CMakeLists.txt`, `vcpkg.json` and the changelog say 1.0.0.
- [ ] Store text reviewed. Limits and confidentiality checks green (11.1).
- [ ] Support, privacy and marketing URLs load, and say what this page says (Stage 10).
- [ ] The demo setup file at its URL imports on the iPhone and the iPad.
- [ ] Licence decision made, notices in the app, listing paragraph final (11.3).
- [ ] Privacy manifest present. No-network check green on the signed build (11.2).

**Build and test**

- [ ] Tag pushed by the owner. `release.yml` green. Build in TestFlight. AAB in Play internal testing.
- [ ] Acceptance checklist passed from the store builds on the iPhone 17, the iPad (both orientations) and the Android
  phone.
- [ ] Screenshots made again from the tag, and checked by eye in light and dark.

**Store pages**

- [ ] `store-metadata.yml` run against the tag. App Store Connect and the Play Console show the text, the images and the
  build.
- [ ] App Privacy: Data Not Collected. Play Data safety: no data collected or shared.
- [ ] Age rating 4+. IARC rating Everyone. Target audience 18+.
- [ ] Export compliance shown as exempt on the build.
- [ ] Price and countries set (owner).
- [ ] Closed-test rule met on Play, if it applies.

**Owner only**

- [ ] Submit for review (App Store). Send for review (Play production).
- [ ] After approval: release (App Store). Start the rollout (Play).
- [ ] Check that the live listings show and install on a clean device.

### Work packages

Each one is a small pull request into `develop`, squash merged. 11.1 is this pull request.

| WP | What | Files | Test labels |
|---|---|---|---|
| 11.0 | Re-read every rule at Apple's and Google's sources, and correct this page | `docs/plan/stage-11.md` | — |
| 11.1 | Store text drafts (this PR). Then a check of the limits and of the confidentiality words over `store/` | `store/`, `tools/ci/check_store_text.py`, tests | `unit` |
| 11.2 | No-network and no-Tracy check on the signed IPA. `PrivacyInfo.xcprivacy` and its check. The `INTERNET` permission check for Stage 9 | `apps/app/packaging/ios/`, `tools/ci/check_ios_plist.py` | `unit`, `system` |
| 11.3 | Open-source licences page and the bundled texts. Demo setup file test. Third-party list from the link | `apps/app/qml/screens/settings/`, `libs/` or `apps/app/src/`, tests, `changelog.d/` | `unit`, `ui` |
| 11.4 | Store screenshot mode for `ej_screenshots`: exact sizes, status bar or captions as decided, the fixed shot list, a CI job that makes them | `tools/ej_screenshots/`, `.github/workflows/`, `docs/ci.md` | `system` |
| 11.5 | Play icon 512 and feature graphic, rendered from sources in the repository | `tools/`, `store/play/` | `system` |
| 11.6 | Version 1.0.0 path: `release.yml` release notes into `store/`, and Android `versionCode` agreed with Stage 9 | `.github/workflows/release.yml`, `docs/releasing.md`, `docs/ci.md` | — |
| 11.7 | `store-metadata.yml` and the fastlane setup (or the API script), draft-only, never submits | `.github/workflows/`, `fastlane/` or `tools/store/`, `docs/ci.md` | `unit` (dry-run parser) |
| 11.8 | Release checklist in `docs/releasing.md`. Accessibility label answers from Stage 7. Exit gate run with the owner | `docs/releasing.md` | — |

### Test list

| ID | Test | Label |
|---|---|---|
| T11-1 | Every App Store and Play text file is within its limit. Keywords are measured in bytes, with no space after a comma and no word from the name repeated | `unit` |
| T11-2 | The confidentiality guard covers `store/`. It fails on a name outside the stand-in list | `unit` |
| T11-3 | Every URL file holds an `https` URL on the documentation site's host | `unit` |
| T11-4 | The demo setup file equals `config/sample-config.json` and imports with no warnings | `unit` |
| T11-5 | The app's own code uses no network API and no remote URL (`check_no_network.py`). The IPA has no Tracy symbols, and its linked network code is reported | `unit`, `system` |
| T11-6 | `PrivacyInfo.xcprivacy` is in the bundle, with `NSPrivacyTracking` false, no tracking domains, no collected types, and a reason for each API category the symbol scan finds | `system` |
| T11-7 | The Android merged manifest has no `INTERNET` permission and no `AD_ID` (with Stage 9) | `system` |
| T11-8 | Every screenshot has its set's exact pixel size, the list of shots matches the committed list, and two runs give the same bytes | `system` |
| T11-9 | The Open-source licences page lists Qt with its licence, and opens the bundled GPL and LGPL texts | `ui` |
| T11-10 | The metadata upload's dry run reads `store/` with no errors and asks for no submission | `unit` |

## Owner actions

These are the owner's. The agent can do none of them (accounts, agreements, secrets and legal advice). They are listed in
the order that best saves waiting time.

1. **Google Play developer account.** Choose personal or organisation. An organisation account needs a D-U-N-S number,
   which can take days to weeks. A personal account needs identity checks and, if new, the 12-tester and 14-day closed
   test. Pay the one-time fee. Complete Android developer verification and register the package name
   `com.iainchesworthlabs.fieldtest`.
2. **Legal advice on the licence** ([facts and options](#licence-facts-and-options)), and a decision. This blocks 11.3's
   final text and the listing's open-source paragraph.
3. **App Store Connect app record for public release:** the existing TestFlight record is the one that is released. Check
   that the name "Field Test" is still free. Accept any new agreement in Business. For a paid app, also complete the
   Paid Apps agreement, tax forms and banking.
4. **EU trader status (Digital Services Act)** in App Store Connect, and the matching declaration in Play. A trader's
   address, phone and email are shown on the listing in the EU. Not declaring removes the app from EU storefronts
   (confirmed: since February 2025 Apple has removed apps without a declaration from EU storefronts). For an organisation
   the address shown comes from its D-U-N-S record.
5. **Mac availability** (Stage 12, O-M3). In App Store Connect, Pricing and Availability, turn off "iPhone and iPad Apps
   on Apple Silicon Macs", which is on by default. Desktop support is v2.
6. **Price and countries** for both stores (free or paid, and which storefronts).
7. **Contact details** for App Review and the Play listing: typed into the consoles, never committed.
8. **Play service-account key** for `supply`, added as a repository secret, if 11.7 uses fastlane.
9. **Forward any ITMS-91053 email** from earlier TestFlight uploads, or say that none came (for the privacy manifest).
10. **Submission and release** on both stores, at the end.

## Decisions for the owner

| # | Decision | Proposal |
|---|---|---|
| D1 | Licence route | Owner, after advice |
| D2 | Free or paid, and countries | Owner |
| D3 | Play account type | Owner |
| D4 | Store languages | English (UK) only for 1.0 |
| D5 | Categories | Productivity, then Business |
| D6 | Screenshot status bar and captions | Drawn neutral status bar, no captions for 1.0 |
| D7 | App preview video | Skip for 1.0 |
| D8 | Play target audience | 18 and over |
| D9 | Metadata upload tool | fastlane `deliver` and `supply` |
| D10 | Phased release | None for 1.0.0, phased for later versions, always released manually |
| D11 | Android `versionCode` | The release run number, the same as iOS (agreed with Stage 9) |

## Assumptions only the stores can confirm

- The limits and required screenshot sizes above, checked against the published pages in 11.0. App Store Connect and the
  Play Console reject what is wrong, and decide whether the 6.1" set is needed.
- That Apple and Google accept "no data collected" for an app whose only way out is the system share sheet.
- That the 2025 age-rating questions give 4+ for these answers.
- That Apple and Google accept "no data collected" for an app whose binary carries Qt Network, which it never calls.

## Rule check, 10 Nov 2026

Task 11.0 read each rule on this page against the stores' published pages. What changed on this page as a result:

- **Screenshots.**
  - Apple's page also lists the 6.1" iPhone class (1206 × 2622) as required, so it is added "if asked".
  - The 6.5" set is needed only when no 6.9" set is given.
  - Google replaced the 7" and 10" tablet sets with one "large screens" set: at least 4 images, 1,080 to 7,680 px, 16:9
    or 9:16.
- **Phased release** applies only to version updates, so it cannot apply to 1.0.0.
- **Data safety.** Google's "sharing" includes handing data to another app on the device. The share sheet answer rests on
  Google's exemption for transfers the user starts.
- **Google Play's licence to users** comes from the developer (Developer Distribution Agreement, 5.3). There is no
  default licence from Google.
- **New requirements:**
  - Apple builds must use Xcode 26 and the iOS 26 SDK (since 28 Apr 2026). The minimum target is iOS 13 (since 9 Sep
    2026). `release.yml` already builds on `macos-26`, whose Xcode is 26, and
    targets iOS 18.
  - Play apps and updates must target API 36 (since 31 Aug 2026).
  - Native libraries must support 16 KB pages: updates without it are refused from 1 Feb 2027.
  - Android developer verification and package registration are enforced from 30 Sep 2026.
- **Owner action added** (from Stage 12): turn off the app on Apple Silicon Macs.

Confirmed as written:

- The App Store text limits. Keywords are 100 bytes, and each keyword must be over 2 characters. What's New cannot be
  set on a first version.
- App previews: 3 per size, 15 to 30 seconds.
- The definition of collected data. The privacy manifest's five categories and their reason codes.
- The age rating questions and ratings. Apple sets the rating from the answers, which are required on every
  submission.
- Accessibility Nutrition Labels are voluntary, with nine features.
- Export compliance needs no documentation for an app that uses only the system's encryption. Whether a US annual
  self-classification report applies is not on Apple's page; it is a question for the owner's adviser.
- The EU trader declaration and its removal rule.
- The standard EULA's open-source exception, and the custom EULA field.
- Google's text and graphic limits.
- The 12 testers for 14 days rule for new personal accounts.
- A privacy policy is required for every Play app.

Sources (official pages only):

- developer.apple.com/help/app-store-connect/reference/app-information/platform-version-information
- developer.apple.com/help/app-store-connect/reference/app-information/screenshot-specifications
- developer.apple.com/help/app-store-connect/reference/app-information/app-preview-specifications
- developer.apple.com/app-store/review/guidelines/
- developer.apple.com/app-store/app-privacy-details/
- developer.apple.com/documentation/bundleresources/describing-use-of-required-reason-api
- developer.apple.com/help/app-store-connect/reference/app-information/age-ratings-values-and-definitions
- developer.apple.com/help/app-store-connect/manage-app-accessibility/overview-of-accessibility-nutrition-labels
- developer.apple.com/help/app-store-connect/reference/app-information/export-compliance-documentation-for-encryption
- developer.apple.com/help/app-store-connect/update-your-app/release-a-version-update-in-phases
- developer.apple.com/help/app-store-connect/manage-compliance-information/manage-european-union-digital-services-act-trader-requirements
- developer.apple.com/news/upcoming-requirements/
- apple.com/legal/internet-services/itunes/dev/stdeula/
- support.google.com/googleplay/android-developer/answer/9859152
- support.google.com/googleplay/android-developer/answer/9866151
- support.google.com/googleplay/android-developer/answer/10787469
- support.google.com/googleplay/android-developer/answer/14151465
- support.google.com/googleplay/android-developer/answer/10144311
- developer.android.com/google/play/requirements/target-sdk
- developer.android.com/guide/practices/page-sizes
- play.google/developer-distribution-agreement.html
