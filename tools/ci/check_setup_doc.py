#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Fail when docs/guide/setup-file.md and the setup-file reader in libs/core disagree.

    python3 tools/ci/check_setup_doc.py [--root .]

The reader (libs/core/src/configuration.cpp) is the truth. The page must:

- list every key the reader accepts, in the table for its object, and no other key;
- say whether the key is required, as the reader does;
- show the length limit of a text key, the range of a number key, the choices of the clock keys and the `{value}`
  placeholder of the template line;
- show the limits of the whole file (CONFIG_* in configuration.hpp);
- have a row for every ConfigError::Code and ConfigError::Problem;
- carry, as its example, exactly config/sample-config.json.

Tables are introduced by `<!-- fields: <object> -->`, limit rows carry `<!-- limit:NAME -->` and message rows carry
`<!-- code:NAME -->` or `<!-- problem:NAME -->`. The page and the checks are described in docs/plan/stage-10.md.
"""

from __future__ import annotations

import argparse
import json
import re
import sys
from pathlib import Path

CPP = "libs/core/src/configuration.cpp"
HEADER = "libs/core/include/error_journaling/configuration.hpp"
UNIT_HEADER = "libs/core/include/error_journaling/unit_number.hpp"
PAGE = "docs/guide/setup-file.md"
SAMPLE = "config/sample-config.json"

# The reader function that handles each object of the file, and the name the page's table uses.
SECTIONS = {
    "top": "import_configuration",
    "preferences": "read_preferences",
    "preferences.time_format": "read_time_format",
    "error_types": "read_error_types",
    "locations": "read_locations",
    "note_templates": "read_templates",
    "interaction_types": "read_interaction_types",
}
LIST_KEYS = {"error_types": "error_types", "locations": "locations", "note_templates": "note_templates", "interaction_types": "interaction_types"}


def constants(*texts: str) -> dict[str, int]:
    """Integer constants such as `constexpr int MAX_NAME = 200;` and `inline constexpr int X = 1'000'000;`."""
    found: dict[str, int] = {}
    pattern = re.compile(r"constexpr\s+(?:std::size_t|int|std::size_t)\s+(\w+)\s*=\s*([\d']+)\s*(?:\*\s*([\d']+))?;")
    for text in texts:
        for name, left, right in pattern.findall(text):
            value = int(left.replace("'", ""))
            if right:
                value *= int(right.replace("'", ""))
            found[name] = value
    found_static = re.findall(r"static constexpr std::size_t (\w+) = (\d+);", "\n".join(texts))
    for name, value in found_static:
        found[name] = int(value)
    return found


def function_body(source: str, name: str) -> str:
    """The text of a top-level function from its signature to the closing brace in column 0."""
    match = re.search(rf"^[^\n]*\b{name}\([^\n]*\n(?:[^\n]*\n)*?\}}\n", source, re.MULTILINE)
    if not match:
        raise ValueError(f"function {name} not found in {CPP}")
    return match.group(0)


def number(token: str, known: dict[str, int]) -> int:
    token = token.strip()
    sign = -1 if token.startswith("-") else 1
    token = token.lstrip("-")
    return sign * (int(token) if token.isdigit() else known[token])


def facts_for(section: str, source: str, known: dict[str, int]) -> dict[str, dict]:
    """What the reader says of each key of one object: required, text limit, number range, literals."""
    body = function_body(source, SECTIONS[section])
    accepted = None
    for found in re.finditer(r"reject_unknown\([^,]+,\s*[^,]+,\s*\{([^}]*)\}\)", body):
        accepted = re.findall(r'"(\w+)"', found.group(1))
    if accepted is None:
        raise ValueError(f"no reject_unknown list in {SECTIONS[section]}")
    facts: dict[str, dict] = {key: {"required": False} for key in accepted}

    for key, required, limit in re.findall(
        r'r\.text\(\*?object,\s*"(\w+)",\s*\w+,\s*(true|false),\s*(\w+)\)', body
    ):
        facts[key]["required"] = required == "true"
        facts[key]["text_limit"] = number(limit, known)
    for key, low, high in re.findall(
        r'r\.integer\(\*?object,\s*"(\w+)",\s*\w+,\s*([-\w]+),\s*([-\w]+),', body
    ):
        facts[key]["range"] = (number(low, known), number(high, known))
    if "r.id<" in body:
        facts["id"]["required"] = True
    if section == "top":
        facts["schema_version"]["required"] = True
        for key in re.findall(r'r\.list\(root,\s*"(\w+)"\)', source):
            facts[key]["required"] = True
        for key, required in re.findall(r'r\.field\(root,\s*"(\w+)",\s*"",\s*(true|false)\)', source):
            facts[key]["required"] = required == "true"
    if section == "preferences":
        facts["unit_environments"]["max_item"] = known["MAX_ENVIRONMENT_LENGTH"]
    if section == "preferences.time_format":
        for key in ("style", "meridiem"):
            facts[key].pop("text_limit", None)  # only a buffer size: the user sees the choices
        facts["style"]["choices"] = sorted(set(re.findall(r'QLatin1StringView\("(\d+h\w*)"\)', body)))
        facts["meridiem"]["choices"] = sorted(set(re.findall(r'QLatin1StringView\("(upper|lower)"\)', body)))
    return facts


def page_tables(page: str) -> dict[str, dict[str, str]]:
    """Each `<!-- fields: x -->` table as {key: the whole row}."""
    tables: dict[str, dict[str, str]] = {}
    for found in re.finditer(r"<!-- fields: ([\w.]+) -->\n\n((?:\|[^\n]*\n)+)", page):
        rows: dict[str, str] = {}
        for line in found.group(2).splitlines()[2:]:
            cells = [cell.strip() for cell in line.strip().strip("|").split("|")]
            key = cells[0].strip("`")
            rows[key] = line
        tables[found.group(1)] = rows
    return tables


def digits(text: str) -> str:
    return text.replace(",", "")


def check(root: Path) -> list[str]:
    problems: list[str] = []
    source = (root / CPP).read_text(encoding="utf-8")
    header = (root / HEADER).read_text(encoding="utf-8")
    unit = (root / UNIT_HEADER).read_text(encoding="utf-8")
    page = (root / PAGE).read_text(encoding="utf-8")
    known = constants(source, header, unit)
    tables = page_tables(page)

    for section in SECTIONS:
        facts = facts_for(section, source, known)
        rows = tables.get(section)
        if rows is None:
            problems.append(f"{PAGE}: no table marked `<!-- fields: {section} -->`")
            continue
        for key in sorted(set(facts) - set(rows)):
            problems.append(f"{PAGE}: the reader accepts `{key}` in {section}, but the table does not list it")
        for key in sorted(set(rows) - set(facts)):
            problems.append(f"{PAGE}: the table for {section} lists `{key}`, which the reader does not accept")
        for key, fact in facts.items():
            row = rows.get(key)
            if row is None:
                continue
            where = f"{section}.{key}"
            wording = "Required" if fact["required"] else "Optional"
            other = "Optional" if fact["required"] else "Required"
            if f"| {wording} |" not in row:
                problems.append(f"{PAGE}: {where} should be marked {wording}, not {other}")
            text = digits(row)
            if "text_limit" in fact and str(fact["text_limit"]) not in text:
                problems.append(f"{PAGE}: {where} should show the limit of {fact['text_limit']} characters")
            if "max_item" in fact and str(fact["max_item"]) not in text:
                problems.append(f"{PAGE}: {where} should show the name limit of {fact['max_item']} characters")
            if "range" in fact:
                for bound in fact["range"]:
                    if str(abs(bound)) not in text:
                        problems.append(f"{PAGE}: {where} should show the bound {bound}")
            for choice in fact.get("choices", []):
                if f'"{choice}"' not in row:
                    problems.append(f'{PAGE}: {where} should show the choice "{choice}"')
    if section_has_pattern(source) and "{value}" not in tables.get("note_templates", {}).get("output_pattern", ""):
        problems.append(f"{PAGE}: note_templates.output_pattern should show `{{value}}`")

    for name in ("CONFIG_SCHEMA_VERSION", "CONFIG_MAX_BYTES", "CONFIG_MAX_ENTRIES_PER_LIST", "CONFIG_BOARD_POSITIONS"):
        row = re.search(rf"^\|[^\n]*<!-- limit:{name} -->[^\n]*$", page, re.MULTILINE)
        value = known.get(name)
        if value is None:
            problems.append(f"{HEADER}: {name} was not found; update tools/ci/check_setup_doc.py")
        elif row is None:
            problems.append(f"{PAGE}: no row marked `<!-- limit:{name} -->`")
        else:
            cell = row.group(0).strip().strip("|").split("|")[-1]
            first = re.search(r"\d[\d,]*", cell)
            if not first or int(digits(first.group(0))) != value:
                problems.append(f"{PAGE}: {name} is {value} in the reader but the row says {cell.strip()!r}")

    for kind, enum in (("code", "Code"), ("problem", "Problem")):
        found = re.search(rf"enum class {enum}\s*\{{(.*?)\}};", header, re.DOTALL)
        names = re.findall(r"^\s*([A-Z_]+)\b", re.sub(r"//[^\n]*", "", found.group(1)), re.MULTILINE) if found else []
        documented = set(re.findall(rf"<!-- {kind}:([A-Z_]+) -->", page))
        for name in sorted(set(names) - documented):
            problems.append(f"{PAGE}: ConfigError::{enum}::{name} has no row marked `<!-- {kind}:{name} -->`")
        for name in sorted(documented - set(names)):
            problems.append(f"{PAGE}: a row is marked {kind}:{name}, which is not a ConfigError::{enum}")

    example = re.search(r"## A complete example.*?```json\n(.*?)```", page, re.DOTALL)
    if not example:
        problems.append(f"{PAGE}: no ```json block under `## A complete example`")
    else:
        try:
            if json.loads(example.group(1)) != json.loads((root / SAMPLE).read_text(encoding="utf-8")):
                problems.append(f"{PAGE}: the example differs from {SAMPLE}")
        except json.JSONDecodeError as error:
            problems.append(f"{PAGE}: the example is not valid JSON ({error})")
    return problems


def section_has_pattern(source: str) -> bool:
    return "UNKNOWN_PLACEHOLDER" in source and '{value}' in source


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--root", default=str(Path(__file__).resolve().parent.parent.parent))
    args = parser.parse_args(argv)
    try:
        problems = check(Path(args.root))
    except (OSError, ValueError, KeyError) as error:
        print(f"check_setup_doc: cannot check: {error}", file=sys.stderr)
        return 2
    for problem in problems:
        print(problem)
    if problems:
        print(f"{len(problems)} problem(s). The reader in libs/core is the truth; update {PAGE}.")
        return 1
    print("setup-file guide: ok")
    return 0


if __name__ == "__main__":
    sys.exit(main())
