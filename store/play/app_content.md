# Google Play app content: drafted answers

The answers for the Play Console, Policy, App content. Task 11.0 checks each question against the Console before 11.8
enters them. See [`docs/plan/stage-11.md`](../../docs/plan/stage-11.md#play-app-content).

## Data safety

| Question | Answer |
|---|---|
| Does your app collect or share any of the required user data types? | **No** |
| Data collected | None |
| Data shared | None. A plain-text log that the user sends through the Android share sheet is a transfer the user starts, to a destination they choose |
| Encrypted in transit | Not applicable, nothing is sent |
| Way to request data deletion | Not applicable, no data is collected. Deleting the app deletes its data |

The Android build has to match these answers: no `INTERNET` permission and no `AD_ID` in the merged manifest (test T11-7,
with Stage 9).

## Content rating (IARC questionnaire)

| Item | Answer |
|---|---|
| Category | Utility, productivity, communication or other |
| Violence, fear, sexuality, language, controlled substances | No |
| Gambling, simulated gambling | No. The "bingo board" is a grid of tiles. Nothing is won, wagered or scored |
| Users can interact or exchange content | No |
| Shares the user's location | No |
| Digital purchases | No |
| Expected rating | Everyone / PEGI 3 / USK 0 |

## Target audience and content

| Item | Answer |
|---|---|
| Target age groups | 18 and over (owner decision D8) |
| Appeals to children | No |

## Other declarations

| Declaration | Answer |
|---|---|
| Ads | No ads |
| Advertising ID | Not used |
| App access | All functionality is available without special access. Instructions are the same as the App Review notes (`../appstore/review_information/notes.txt`) |
| Government app | No |
| Financial features | None |
| Health app | No |
| News app | No |
| Account creation, so account deletion | No account |
| Privacy policy | The URL in `../appstore/en-GB/privacy_url.txt` (Stage 10) |
