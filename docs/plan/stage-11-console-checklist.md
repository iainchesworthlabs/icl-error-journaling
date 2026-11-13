# Stage 11: App Store Connect checklist for the owner

Everything the owner types or clicks in App Store Connect for the **Field Test** record (`com.iainchesworthlabs.fieldtest`)
that the upload cannot do. The reasons and the evidence are in [`stage-11.md`](stage-11.md). Contact details are never
committed: where this page says `<your ...>`, type your own.

The listing text, the three URLs, the review notes, the copyright line, the categories and the screenshots are uploaded by
the **Store metadata** workflow ([`docs/ci.md`](../ci.md)). It saves them to the version and never submits for review.
Check them in the console after each upload; do not retype them.

Submitting for review and releasing are yours, at the end. Nothing here does either.

## 1. Order

1. Agreements, tax and banking (Business). The Paid Apps agreement is needed for a paid app.
2. Pricing and Availability (section 2).
3. App Information (section 3) and the age rating (section 4).
4. App Privacy (section 5).
5. Run **Store metadata** (App Store) and check the version page (section 9).
6. The version page: build, review information, export compliance (sections 6 to 8).
7. Submit for review, when you decide.

## 2. Pricing and Availability

| Field | Enter |
|---|---|
| Price | US dollars **6.99** as the base price, a paid app. No trial, no in-app purchase |
| Other countries | Leave the App Store's own conversion from the US price. Check the amounts it shows (about 9.99 to 10.99 Australian dollars, about 5.99 pounds, about 6.99 euros) before you save |
| Availability | **All countries and regions** where the app can be published |
| iPhone and iPad apps on Apple Silicon Macs | **Off.** It is on by default. Desktop support is for a later version |
| Apple Vision Pro | Off, if the console offers it. The app is not built for it |
| Pre-order | Off |

## 3. App Information

| Field | Enter |
|---|---|
| Name, subtitle, categories, copyright | Uploaded from `store/appstore/`. Check only |
| Bundle ID | `com.iainchesworthlabs.fieldtest` (already chosen) |
| SKU | Any text you will recognise, for example `field-test-1` |
| Primary language | English (Australia), as the record already is. The listing is English (Australia) only, named Field Test. A UK listing was refused by the store and dropped |
| Content rights | Answer **No** to "contains, shows or accesses third-party content". The app shows only what the user enters |
| Privacy policy URL, support URL, marketing URL | Uploaded: `https://iainchesworthlabs.github.io/icl-error-journaling/privacy/`, `.../support/` and the site root. These GitHub Pages addresses stay until the project's own domain is live; then change the three files in `store/appstore/en-AU/` and upload again |

## 4. Age rating

You asked for 3+. **Apple has no 3+ rating.** Its lowest is **4+** (the 2025 scale is 4+, 9+, 13+, 16+ and 18+), so the
answers below give 4+. 3+ is the PEGI level for the Play Store and the content-rating questionnaire there reaches it
([`store/play/app_content.md`](https://github.com/iainchesworthlabs/icl-error-journaling/blob/develop/store/play/app_content.md)).

In App Information, Age Rating, answer:

| Question | Answer |
|---|---|
| Parental controls | No |
| Age assurance | No |
| Unrestricted web access | No |
| User-generated content shared with other users | No |
| Messaging and chat | No |
| Advertising | No |
| Every content item (violence, profanity, mature or suggestive themes, horror, sexual content, alcohol, tobacco or drugs, medical or wellness, simulated gambling, gambling, contests) | None |

If the console's questions differ from these (Apple changes them), keep the principle: the app has no web access, no shared
user content, no ads and no mature content. The expected result is **4+**.

## 5. App Privacy

| Question | Answer |
|---|---|
| Do you or your third-party partners collect data from this app? | **No, we do not collect data from this app** |
| Privacy Policy URL | The privacy URL above |
| Tracking | None. There is no advertising ID and no App Tracking Transparency prompt |

The label shown on the store is **Data Not Collected**. The reason: the app has no network use. The check
`tools/ci/check_no_network.py` fails the build if the app's own code uses a network API, links Qt Network or names a remote
address other than the project's own pages. The app sends nothing; the log leaves the device only when the user shares or
copies it. Publish the answers after you save them.

## 6. Version page: Build

Attach **build 14**, version 0.1.0 (the upload of run 43152886140). Choose it under Build. You can instead pass `build: 14`
to the workflow and it attaches the build for you.

Build 14 must show as **Ready to Submit** (processing finished, export compliance answered by the build itself).

## 7. Version page: Export compliance

The build's `Info.plist` sets `ITSAppUsesNonExemptEncryption` to false, so App Store Connect asks no encryption question for
the build. If the console asks anyway:

| Question | Answer |
|---|---|
| Does your app use encryption? | Yes, but only the encryption that is part of the operating system (iOS Data Protection and the system's TLS). This is the exempt case |
| Does it use non-exempt encryption of its own? | No |
| Is it available in France? | Not needed: the exempt answer applies |
| Documentation to upload | None |

## 8. Version page: App Review

| Field | Enter |
|---|---|
| Sign-in required | **Off.** There is no account and no demo account |
| Contact first name, last name, phone, email | `<your name>`, `<your telephone>`, `<your email>`. Type these in the console. They are not in the repository |
| Notes | Uploaded from `store/appstore/review_information/notes.txt`. Check only |
| Attachment | Optional: the demo setup file `config/sample-config.json`, saved as `demo-setup.ejconfig`. The notes also link to the published copy |

## 9. Version page: the rest

| Field | Enter |
|---|---|
| Screenshots | Uploaded (iPhone 6.9 inch, iPad 13 inch). Check each device tab and the order |
| App Previews | None |
| Promotional text, description, keywords, What's New | Uploaded, except What's New on the first version (the console refuses it) |
| Version release | **Manually release this version**, so that approval does not publish it. Choose it before you submit |
| Phased release | Not for a first version |

## 10. EU Digital Services Act (trader status)

In App Store Connect, Business, Compliance, Digital Services Act. Without a declaration the app is removed from EU
storefronts.

| Question | Answer |
|---|---|
| Are you a trader? | **Yes** if you sell the app for money as your business or for your organisation. A paid app is a commercial activity. Answer as the legal adviser says |
| Trader name | `<your trader name>` |
| Address, telephone, email | `<your address>`, `<your telephone>`, `<your email>`. The EU listing shows these publicly. Use contact details you are happy to publish |
| Self-certification | You confirm the details are yours and you will offer only products that comply with EU law |

Do the same in the Play Console for the Play listing, when the Android app is set up.

## 11. Before you press Submit

- [ ] Section 2 to 10 are done and saved.
- [ ] Build 14 is attached to the version, and shows no warning.
- [ ] You looked at the listing on an iPhone and an iPad size in the console's preview.
- [ ] The licence decision is made ([`stage-11.md`](stage-11.md#licence-facts-and-options)).
- [ ] "Manually release this version" is chosen.
- [ ] You, not an agent, press **Add for Review** and then **Submit**.
