# App Store App Privacy and export compliance: drafted answers

See [`docs/plan/stage-11.md`](../../docs/plan/stage-11.md#app-privacy) for the evidence from the code.

## App Privacy

| Question | Answer |
|---|---|
| Do you or your third-party partners collect data from this app? | **No, we do not collect data from this app** |
| Tracking | None. No IDFA, no App Tracking Transparency prompt |
| Privacy policy URL | `en-GB/privacy_url.txt` (Stage 10) |

The label shown on the store is **Data Not Collected**.

## Privacy manifest (`PrivacyInfo.xcprivacy`, task 11.2)

| Key | Value |
|---|---|
| `NSPrivacyTracking` | false |
| `NSPrivacyTrackingDomains` | empty |
| `NSPrivacyCollectedDataTypes` | empty |
| `NSPrivacyAccessedAPITypes` | One entry per category that the symbol scan of the signed build finds. Expected: file timestamp (C617.1), system boot time (35F9.1), and user defaults (CA92.1) if `QSettings` is linked. 11.2 confirms each one |

## Export compliance

| Question | Answer |
|---|---|
| `ITSAppUsesNonExemptEncryption` in `Info.plist` | false (set today) |
| Encryption in the app | None of its own. Files at rest use iOS Data Protection, the operating system's encryption |
| Documentation to upload | None |
