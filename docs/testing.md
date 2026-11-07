# Testing

Tests use Catch2 through CTest. Each test carries one label that names its kind: `unit`,
`integration`, `system`, `ui` or `perf`. CI runs them in that order, with `unit`,
`integration` and `system` in parallel, `ui` at two jobs, and `perf` last on its own.
A failed test is run once more by name; a pass on the second run is reported as a warning so
flaky tests stay visible. A phase that matches no tests fails, unless the phase is marked optional.

| Kind | Where | Example |
|---|---|---|
| unit | `libs/*/tests` | `ej_core_tests`, `ej_storage_tests`, `ej_ios_bridge_tests` (the desktop stub and the QML-facing wiring, against a fake; the iOS code is only compiled in CI) |
| ui | `apps/app` | `app.smoke` starts the application off-screen and waits for a frame; `app.version` |

## Golden tests and fixtures

The export format is pinned by golden files: `libs/core/tests/golden/<name>.txt` holds the exact bytes an export must
produce. A test builds a log, renders it and calls `CHECK_GOLDEN("<name>", text)` (`libs/core/tests/golden.hpp`). The
comparison is on bytes, so a missing final newline, a tab or a different dash fails. On a mismatch the failure message
names the first differing line and the helper writes the actual output to `<name>.actual.txt` in
`<build>/libs/core/tests/golden-actual/`. To accept an intended change, copy that file over the golden and review the diff
in the pull request. `.gitattributes` marks the golden and fixture files `-text eol=lf`, so a checkout on Windows cannot
convert line endings.

A fixture is any other input file a test reads, kept in `libs/core/tests/fixtures/` and found with
`ej::test::fixture_path(name)`. Fixtures and goldens contain only the fictional stand-ins (Sam Example, UNIT, North Site,
Wrong Response); the confidentiality check runs over them.

## Coverage

`linux-coverage` builds with instrumentation. CI runs gcovr and `coverage_gate.py` against
`tools/coverage-floors.json`. Floors are per component: `default` applies to any component not
listed. The gate prints a hint when a component is above its floor; raise the floor in the same
pull request so coverage only moves up.

A line that cannot be reached by a test, such as a failing system call, is excluded with a
`GCOVR_EXCL_BR_LINE` comment and a reason.

## Running one kind

```bash
ctest --preset linux-debug -L unit
```
