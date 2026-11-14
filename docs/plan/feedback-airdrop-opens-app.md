# Feedback: an AirDropped setup file should open in the app

Tester feedback from the first AirDrop of a setup file between two iPhones: Settings > Import or export > Export this
setup, then AirDrop. The receiving phone accepted the transfer and then showed the file as a page of JSON text. The
tester had to tap the share button on that page and choose Field Test before the import started. With the app installed
the file should open in the app straight away.

## What is known

- The share sheet gets a file item named `field-test-setup.ejconfig` (`libs/ios_bridge/src/platform/ios/share.mm`).
- The application exports the type `com.iainchesworthlabs.fieldtest.config` (extension `ejconfig`) and names it in a
  document type with the handler rank Owner (`apps/app/packaging/ios/Info.plist.in`).
- The exported type conforms to `public.json`. Sharing the file to the app from the preview works, so the type and the
  `QFileOpenEvent` path are right.

## Cause (not yet confirmed on a device)

The exported type conforms to `public.json`, which is a type iOS can show in Quick Look. The likely cause is that the
system opens its own preview for a file it can display instead of handing it to the Owner. This is an inference from how
iOS treats text-like types; no Mac or device runs here, so it is checked on a device (task 6).

## Decision

- The exported type conforms to `public.data` and `public.content` and no longer to `public.json`. Nothing in the app
  needs the JSON conformance: the importer reads the bytes and validates them (`ConfigImporter`).
- The Alternate document type for `public.json` stays, so a plain `.json` file still offers the app as one choice. That
  case keeps the Quick Look behaviour, which is the system's and is acceptable for a file that is not ours.
- The export, the file name and the extension do not change.

## Open questions for the owner

1. **Is a plain `.json` file that opens in a preview acceptable?** The Alternate rank cannot be made to win over Quick
   Look. Dropping the registration would make a `.json` file offer no app at all, so the plan keeps it.
2. **If the device pass shows the preview still wins**, the fallback is to hand the share sheet an `NSItemProvider`
   with the exported type named explicitly, instead of a bare `NSURL`. That is a second change and a second device
   pass, so it waits for the result of task 6.

## Tasks and tests

1. `Info.plist.in`: `UTTypeConformsTo` becomes `public.data` and `public.content`; the comment above the type says why.
2. `tools/ci/check_ios_plist.py`: require `public.data` and refuse `public.json` in the exported type's conformance.
   `tools/tests/test_ios_plist.py`: the sample plist, the two cases that name `public.json` conformance, and a new case
   that fails when `public.json` is present (`unit`).
3. `apps/app/packaging/ios/verify_app.sh`: check the built plist's conformance list the same way.
4. `libs/ios_bridge/include/error_journaling/document_types.hpp`: the comment on the type, and `docs/releasing.md`,
   `docs/ci.md` and `docs/plan/stage-5.md` where they say the type conforms to `public.json`.
5. `changelog.d/airdrop-opens-app.fixed.md` (a user-visible change), and `docs/guide/setup-file.md`: a sentence that a
   setup file sent by AirDrop opens in the app. `docs/privacy.md` does not change: nothing stored or sent changes.
6. Device pass, run by the owner on a TestFlight build, sender and receiver both with the app:
   (a) AirDrop the exported file: the app opens on the import confirmation with no preview;
   (b) AirDrop a plain `.json` file: the system preview or an "Open in" choice appears and Field Test is offered;
   (c) import from Files and from Settings still works;
   (d) the receiver has the previous build installed first, to see whether the old type registration lingers.
   The pull request says that none of this has run on a device.

Checks: `python3 tools/ci/precheck.py`, the `unit` label for the plist tests, and the iOS CI job that runs
`verify_app.sh`. There is no C++ behaviour change, so `linux-debug` and `linux-asan-ubsan` are not needed beyond CI.
