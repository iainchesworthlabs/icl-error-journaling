# Store listings

The text of the App Store and Google Play listings, kept here so that it can be reviewed and edited in pull requests.
Everything here is a draft until the owner approves it. The facts the owner confirms before release are in
[`appstore/README.md`](appstore/README.md).

| Path | What | Read by |
|---|---|---|
| `appstore/en-AU/*.txt` | App Store listing for English (Australia): name, subtitle, description, keywords, promotional text, What's New, the three URLs, screenshot captions | `fastlane deliver` (task 11.7) |
| `appstore/*_category.txt`, `appstore/copyright.txt` | Fields that are not per language | `fastlane deliver` |
| `appstore/review_information/notes.txt` | App Review notes | `fastlane deliver` |
| `appstore/age_rating.md`, `appstore/app_privacy.md` | Answers typed into App Store Connect by the owner | People |
| `play/en-AU/*.txt` | Play listing for English (Australia): title, short and full description | `fastlane supply` |
| `play/en-AU/changelogs/default.txt` | Play release notes | `fastlane supply` |
| `play/app_content.md` | Data safety, content rating, target audience and other declarations | People |

Rules:

- Use only the stand-ins from `docs/` (Sam Example, UNIT, North Site, Wrong Response, "the device"). The listing is public.
- Keep within the stores' limits. `tools/ci/check_store_text.py` enforces them.
- Text files hold the value only: no comments and no licence header, because the upload sends the whole file.
  `REUSE.toml` covers their licence.
- Contact names, telephone numbers and email addresses for App Review are not kept here. The owner types them into the
  consoles.
- Screenshots and graphics are made by the tools and are not committed.
- The three URLs are checked against `site_url` in `mkdocs.yml` by `tools/ci/check_site_address.py`.
