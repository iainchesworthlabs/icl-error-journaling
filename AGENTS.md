# Instructions for coding agents

This file applies to any automated contributor. Read it before changing the repository.

## Working rules

- Work on a branch named `<prefix>/<slug>` with the prefix `feature`, `bugfix`, `docs`, `chore` or `agent`, and a
  lower-case slug with hyphens. Never commit to `develop` or `main` directly.
- Commit with `git commit -s`. Write the subject in the imperative, 72 characters or fewer, and the body as a plain
  description of what changed and why.
- Open pull requests against `develop`. Fill in the pull request template. Squash merge is the only method.
- Run `python3 tools/ci/precheck.py` before pushing. It runs the checks of the Policy job.
- Build and test with the presets in `CMakePresets.json`. A change to code runs `linux-debug` and
  `linux-asan-ubsan` locally before the pull request.
- Do not create or edit secrets, tags, releases, repository settings or rulesets. Those are done by the owner.
- Do not edit `cmake/icl/`. Change `icl-common-infra` and re-run `tools/ci/sync_cmake.py`.

## Code rules

- The full project standard is [`docs/standard.md`](https://github.com/iainchesworthlabs/icl-common-infra/blob/develop/docs/standard.md)
  in `icl-common-infra`. Read the sections that apply to the change.
- C++23. No `#if`, `#ifdef` or `#elif` on a platform, compiler or standard-library version. Put the
  difference in `src/platform/<os>/` instead. Other preprocessor directives are counted in
  `tools/preprocessor-baseline.json` and the count must not rise.
- Names: `snake_case` for namespaces, functions and variables; `PascalCase` for types; `m_PascalCase` for members;
  `UPPER_SNAKE_CASE` for constants and enumerators. Allman braces. Run `clang-format` on changed files.
- Return `std::expected` for recoverable errors in library code. Do not throw across a library boundary.
- Every new behaviour has a test with a label (`unit`, `integration`, `system`, `ui`, `perf`).

## If you change X, update Y

| You change | Also update |
|---|---|
| A user-visible behaviour | A fragment in `changelog.d/` |
| A public header in `libs/*/include` | The tests, and `docs/architecture.md` when the layout changes |
| A dependency in `vcpkg.json` | Nothing else; Dependabot and the baseline check cover the fork |
| A CMake option or preset | `docs/building.md` |
| Test labels or phases | `docs/testing.md` |
| A workflow in `.github/workflows` | `docs/ci.md`; pin every `uses:` to a full SHA with a version comment |
| Which paths need a build | `.github/ci/plan.json` |
| Coverage for a component | `tools/coverage-floors.json` (raise it when coverage rose) |
| Files that add a preprocessor directive | `tools/preprocessor-baseline.json` only through `--update-baseline` when the count fell |
| A new source file | The SPDX licence header, copied from an existing file |
