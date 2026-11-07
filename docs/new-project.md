# Starting a project

1. Create the repository from this template in the `iainchesworthlabs` organisation.
2. Clone it and run `python3 tools/instantiate.py --name my_project`. Review the diff.
3. Run `python3 tools/ci/sync_cmake.py --infra <icl-common-infra checkout>` if the pinned
   commit has moved since the template was cut.
4. Edit `.github/repo-settings.json` (topics) and `.github/CODEOWNERS`.
5. Apply the repository settings, rulesets and labels with a dry run first:

   ```bash
   <icl-common-infra>/admin/apply.sh --repo iainchesworthlabs/my-project
   RELEASE_BOT_APP_ID=<id> OWNER_USER=<login> <icl-common-infra>/admin/apply.sh --repo iainchesworthlabs/my-project --apply
   ```

6. Add the secrets listed in `icl-common-infra/docs/secrets.md`: `MERGE_QUEUE_TOKEN` as a Dependabot
   secret, and the release secrets as organisation secrets shared with the repository.
7. If the project ships for iPhone and iPad, follow "iOS and TestFlight" in `docs/releasing.md`: `instantiate.py` has set
   the bundle identifier (`com.iainchesworthlabs.<name without underscores>`); the Apple-side steps and the repository
   variable `SIGN_IOS_UPLOAD` are the owner's. Without them the iOS job of the release does not run.
8. Open the first pull request and check that **CI Status** and **Scan dependency diff** report.
   The develop ruleset already requires both.
