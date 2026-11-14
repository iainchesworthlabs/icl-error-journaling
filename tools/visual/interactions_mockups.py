#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Write the iPad and foldable mock-ups of the Interactions board (Stage 14, package I-8).

    python tools/visual/interactions_mockups.py [--check]

Writes docs/design/screens/Interactions.<frame>[.dark].dc.html for the iPad in portrait and landscape and for the foldable
frames of Stage 13, and InteractionsPicker.<frame>[.dark].dc.html (the person picker over the board) for the frames where the
sheet is placed differently. The pages follow docs/plan/stage-14.md, 4.2, and reuse the building blocks of
tools/visual/ipad_mockups.py and tools/visual/fold_mockups.py; every colour is read from docs/design/tokens.json (the second
hue family `interactionPalette` and `colour.interactionAccent`). The pages are static HTML. With --check it writes nothing and
fails when a file differs from what it would write.

The fold drawn on the half-folded pages is an annotation, not part of the UI. The people and types are the fictional stand-ins
of docs/.
"""

from __future__ import annotations

import argparse
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))

import fold_mockups as fold  # noqa: E402
import ipad_mockups as base  # noqa: E402
from fold_mockups import FOLD_CLEAR, Frame, TAB_CONTENT  # noqa: E402
from ipad_mockups import icon  # noqa: E402

ROOT = base.ROOT
SCREENS = base.SCREENS
TOKENS = base.TOKENS

STRIP = 28  # interactionStripHeightPt
STRIP_GAP = 8
MIN_TILE = 88

IPAD_PORTRAIT = Frame("ipad-portrait", "iPad portrait", 820, 1180, 24, 20)
IPAD_LANDSCAPE = Frame("ipad-landscape", "iPad landscape", 1180, 820, 24, 20)

# name, count today, asks a person
TILES = [
    ("Greeted", 1, False),
    ("Spoke with", 2, True),
    ("Played song", 1, False),
    ("Visitor interaction", 0, False),
    ("Handed over", 0, False),
    ("Asked a question", 1, False),
    ("Gave help", 0, False),
    ("Restarted", 0, False),
    ("Moved UNIT", 0, False),
    ("Charging", 0, False),
    ("Cleaned", 0, False),
]

# The log shows both kinds, in time order, so an interaction appears next to the error it followed.
MIXED = [
    ("09:42", "T|", "Wrong Response"),
    ("10:05", "T|", "Wrong Response"),
    ("10:20", "(Interaction)", "Spoke with Alex Sample"),
    ("10:30", "T|", "Mute Delay"),
    ("10:48", "(Interaction)", "Played Morning Medley"),
    ("11:02", "T|", "Wrong Track Played – asked for Evening Song"),
    ("11:15", "(Interaction)", "Greeted"),
]

PERSON = '<circle cx="12" cy="8" r="3.5"></circle><path d="M5 20a7 7 0 0 1 14 0"></path>'


def mixed(call):
    """Run `call` with the example log of this script in place of the Errors-only one, then put the old one back."""
    saved = list(base.ENTRIES)
    base.ENTRIES[:] = MIXED
    try:
        return call()
    finally:
        base.ENTRIES[:] = saved


class Page(fold.Page):
    """A mock-up with the five tabs and the Interactions board."""

    def __init__(self, frame: Frame, dark: bool) -> None:
        super().__init__(frame, dark)
        self.hues = [t["darkHex" if dark else "hex"] for t in TOKENS["interactionPalette"]]
        self.accent = self.c["interactionAccent"]

    # ---- the five tabs ----

    def tab_items(self, selected: int, vertical: bool) -> str:
        c = self.c
        names = [
            ("log", "Errors", "Main"),
            ("note", "Interactions", "Interactions"),
            ("review", "Review", "Review"),
            ("history", "History", "History"),
            ("settings", "Settings", "BoardSettings"),
        ]
        items = []
        for i, (glyph, label, target) in enumerate(names):
            colour = c["primaryText"] if i == selected else c["textSecondary"]
            weight = 600 if i == selected else 500
            size = "height: 64px; justify-content: center; " if vertical else ""
            pill = ""
            if vertical and i == selected:
                pill = f"background: {c['primaryTint']}; border-radius: 16px; margin: 0 8px; "
            items.append(
                f'<a href="{target}.{self.f.name}{self.suffix()}.dc.html" style="display: flex; flex-direction: column; align-items: center; '
                f'{size}{pill}gap: 3px; text-decoration: none; color: {colour}; font-size: 10.5px; font-weight: {weight}">'
                f"{icon(glyph, colour)}{label}</a>"
            )
        return "".join(items)

    def tab_bar(self, selected: int = 1, top: int | None = None, half: bool = False) -> str:
        return super().tab_bar(selected, top, half).replace("repeat(4, minmax(0, 1fr))", "repeat(5, minmax(0, 1fr))")

    # ---- the shared blocks, with the log of both kinds ----

    def log_rows(self, font: int = 15) -> str:
        return mixed(lambda: base.Page.log_rows(self, font))

    def log_strip(self, rows: int) -> str:
        return mixed(lambda: fold.Page.log_strip(self, rows))

    def last_bar(self) -> str:
        return super().last_bar().replace("10:20  T| Mute Delay", "11:15  (Interaction) Greeted")

    # ---- the board ----

    def strip(self, width: int) -> str:
        c = self.c
        return (
            f'<div aria-label="Interactions board. Swipe here to switch to the Errors board." style="width: {width}px; height: {STRIP}px; '
            f'box-sizing: border-box; display: flex; align-items: center; gap: 8px; padding: 0 4px">'
            f"{icon('note', self.accent, 18)}"
            f'<span style="font-size: 15px; font-weight: 700; color: {self.accent}">Interactions</span>'
            f'<span style="flex-grow: 1"></span>'
            f'<span style="font-size: 12px; color: {c["textSecondary"]}; white-space: nowrap">Swipe here to switch to Errors</span></div>'
        )

    def board(self, width: int, tile_height: int, gap: int | None = None) -> str:
        c, r = self.c, self.r
        gap = r["tile_gap"] if gap is None else gap
        pad = r["tile_pad"]
        tiles = []
        for (name, count, person), fill in zip(TILES, self.hues):
            badge = ""
            if count:
                badge = (
                    f'<span style="position: absolute; top: {pad - 4}px; right: {pad - 4}px; min-width: {r["badge"]}px; '
                    f"height: {r['badge']}px; padding: 0 7px; box-sizing: border-box; border-radius: {r['badge'] // 2}px; "
                    f"background: {c['badgeBackground']}; color: {c['badgeText']}; font-size: {r['badge_text']}px; font-weight: 700; "
                    f'display: flex; align-items: center; justify-content: center">{count}</span>'
                )
            mark = ""
            if person:
                mark = (
                    f'<span style="position: absolute; left: {pad}px; top: {pad}px; display: flex">'
                    f'<svg width="18" height="18" viewBox="0 0 24 24" fill="none" stroke="{c["textPrimary"]}" stroke-width="2" '
                    f'stroke-linecap="round" stroke-linejoin="round" aria-hidden="true">{PERSON}</svg></span>'
                )
            aria = name + (f", {count} today" if count else "") + (", asks for a person" if person else "")
            tiles.append(
                f'<button class="tile" aria-label="{aria}" style="position: relative; height: {tile_height}px; border-radius: 16px; '
                f"border: 0; background: {fill}; padding: {pad}px; display: flex; align-items: flex-end; text-align: left; "
                f"font-family: inherit; font-size: {r['label']}px; font-weight: 600; line-height: 1.2; color: {c['textPrimary']}; "
                f'box-shadow: {"none" if self.dark else "0 1px 2px rgba(20,33,43,0.08)"}"><span>{name}</span>{mark}{badge}</button>'
            )
        tiles.append(
            f'<a href="Other.{self.f.name}{self.suffix()}.dc.html" aria-label="Other interaction type" style="height: {tile_height}px; '
            f"box-sizing: border-box; border-radius: 16px; border: 1.5px dashed {self.other_border}; background: {self.other_fill}; "
            f"padding: {pad}px; display: flex; flex-direction: column; justify-content: space-between; text-decoration: none; "
            f'color: {c["textPrimary"]}">{icon("search", c["primaryText"], 24 if self.f.compact else 28)}'
            f'<span style="font-size: {r["label"]}px; font-weight: 600">Other…</span></a>'
        )
        return (
            f'<div style="width: {width}px; display: grid; grid-template-columns: repeat(3, minmax(0, 1fr)); gap: {gap}px">'
            + "".join(tiles)
            + "</div>"
        )

    def board_block(self, width: int, tile_height: int, gap: int | None = None) -> str:
        return (
            f'<div style="display: flex; flex-direction: column; gap: {STRIP_GAP}px">{self.strip(width)}'
            f"{self.board(width, tile_height, gap)}</div>"
        )


# ---- layouts ----


def tile_height_for(room: int, gap: int) -> int:
    return max(MIN_TILE, int((room - 3 * gap) // 4))


def regular_log(p: Page) -> str:
    """iPad portrait (Regular): the 680 pt column, the same tile sizes as the Errors board, the tab bar in the column."""
    f = p.f
    x, width = p.column(base.LOG_COLUMN)
    gap = p.r["tile_gap"]
    tile = int(max(104, min(152, round((width - 2 * gap) / 3 * 0.7))))
    body = (
        f'<div style="position: absolute; left: {x}px; width: {width}px; top: {f.top + 16}px; display: flex; flex-direction: column; '
        f'gap: {p.r["gap"]}px">{p.header_card()}{p.composer()}{p.last_bar()}{p.board_block(width, tile)}</div>'
    )
    tw = min(width, 520)
    body += p.toast((f.width - tw) // 2, tw, TAB_CONTENT + f.bottom + 16)
    return body + p.tab_bar()


def panes_log(p: Page, board_width: int, log_left: int, log_right: int, left: int) -> str:
    """Board pane (composer, strip, board) beside the log pane, which lists both kinds, newest in view."""
    f, r = p.f, p.r
    top = f.top + 16
    bottom = p.bottom_edge()
    board_top = top + fold.COMPOSER + r["gap"] + STRIP + STRIP_GAP
    tile = tile_height_for(bottom - board_top, r["tile_gap"])
    body = (
        f'<div style="position: absolute; left: {left}px; width: {board_width}px; top: {top}px; display: flex; '
        f'flex-direction: column; gap: {r["gap"]}px">{p.composer()}{p.board_block(board_width, tile)}</div>'
    )
    return body + p.log_pane(log_left, log_right - log_left, top, bottom, True) + p.tab_bar()


def wide_log(p: Page) -> str:
    left, right = p.content()
    board_width = round((right - left - fold.PANE_GAP) * fold.BOARD_SHARE)
    return panes_log(p, board_width, left + board_width + fold.PANE_GAP, right, left)


def book_log(p: Page) -> str:
    left, right = p.content()
    board_right = int(p.f.fold_at - FOLD_CLEAR)
    return panes_log(p, board_right - left, int(p.f.fold_at + FOLD_CLEAR), right, left)


def tabletop_log(p: Page) -> str:
    """Tabletop: the grid stays on the flat half at the size of the Errors grid; the strip is in the upright half, under the
    composer, so the grid has the same height as the Errors grid."""
    f = p.f
    left, right = p.content()
    width = right - left
    fold_at = int(f.fold_at)
    top = f.top + 12
    tab_top = fold_at - FOLD_CLEAR - TAB_CONTENT
    if width >= 600:
        card = round((width - 8) * 0.45)
        heads = (
            f'<div style="display: flex; gap: 8px; align-items: flex-start"><div style="width: {card}px; flex-shrink: 0">{p.header_card()}</div>'
            f'<div style="flex-grow: 1; min-width: 0">{p.composer()}</div></div>'
        )
        strip_top = top + fold.COMPOSER + 8
    else:
        heads = p.header_card() + p.composer()
        strip_top = top + fold.HEADER_CARD + 8 + fold.COMPOSER + 8
    log_top = strip_top + STRIP + 8
    rows = max(1, (tab_top - 8 - log_top - 30) // 33)
    upper = (
        f'<div style="position: absolute; left: {left}px; width: {width}px; top: {top}px; display: flex; flex-direction: column; gap: 8px">'
        f"{heads}{p.strip(width)}{p.log_strip(rows)}</div>"
    )
    board_top = fold_at + FOLD_CLEAR
    gap = 8
    tile = int((f.height - f.bottom - 8 - board_top - 3 * gap) // 4)
    lower = f'<div style="position: absolute; left: {left}px; width: {width}px; top: {board_top}px">{p.board(width, tile, gap)}</div>'
    body = upper + lower + p.tab_bar(top=tab_top)
    body += p.toast(left + 8, width - 16, f.height - log_top - 52 - 2)
    return body


def log_body(p: Page) -> str:
    if p.f.fold == "vertical":
        return book_log(p)
    if p.f.fold == "horizontal":
        return tabletop_log(p)
    if p.f.wide:
        return wide_log(p)
    return regular_log(p)


def interactions_screen(p: Page) -> str:
    return p.document("Interactions – second board", log_body(p))


# ---- the person picker ----


def picker_inner(p: Page) -> str:
    c = p.c
    people = [("Alex Sample", "North Site", True), ("Jordan Placeholder", "North Site", True), ("Pat Stand-in", "North Site", False)]

    def row(name: str, place: str, ticked: bool, last: bool) -> str:
        border = "" if last else f"border-bottom: 1px solid {c['divider']}; "
        box = (
            f'<span style="width: 24px; height: 24px; border-radius: 12px; background: {c["primary"]}; display: flex; align-items: center; '
            f'justify-content: center">{icon("tick", c["textOnPrimary"], 14, 3)}</span>'
            if ticked
            else f'<span style="width: 24px; height: 24px; border-radius: 12px; border: 1.5px solid {c["borderStrong"]}; box-sizing: border-box"></span>'
        )
        return (
            f'<div style="height: 52px; flex-shrink: 0; display: flex; align-items: center; gap: 12px; padding: 0 14px; {border}font-size: 16px">'
            f'{box}<span style="flex-grow: 1">{name}</span><span style="font-size: 14px; color: {c["textSecondary"]}">{place}</span></div>'
        )

    rows = "".join(row(n, s, t, i == len(people) - 1) for i, (n, s, t) in enumerate(people))
    return (
        base.sheet_nav(p, "Skip", "Spoke with …", "Done (2)")
        + f'<div style="padding: 0 16px 12px; flex-shrink: 0"><div style="display: flex; align-items: center; gap: 8px; height: 40px; '
        f'border-radius: 10px; background: {c["searchBackground"]}; padding: 0 10px; font-size: 16px; color: {c["textPlaceholder"]}">'
        f"{icon('search', c['textSecondary'], 18)}Search or type a name</div></div>"
        f'<div style="padding: 0 16px; display: flex; flex-direction: column; gap: 12px; min-height: 0; overflow: hidden">'
        f'{p.section_label("Recent · North Site")}<div style="background: {c["surfaceRaised"]}; border-radius: 14px; overflow: hidden">{rows}</div>'
        f'{p.section_label("All people")}<div style="background: {c["surfaceRaised"]}; border-radius: 14px; overflow: hidden">'
        f"{row('Robin Specimen', 'South Site', False, True)}</div></div>"
    )


def picker_screen(p: Page) -> str:
    c = p.c
    f = p.f
    if f.fold == "vertical":
        # In the trailing half, clear of the fold; the board stays in sight, dimmed, in the other half.
        left = int(f.fold_at + FOLD_CLEAR)
        right = p.content()[1] + p.r["margin"] - 12
        sheet = fold.sheet_card(p, picker_inner(p), left, right - left, f.top + 16, f.height - f.bottom - 16)
    else:
        left, right = p.content()
        width = min(fold.SHEET_WIDTH, right - left)
        margin = f.top + 40
        sheet = fold.sheet_card(p, picker_inner(p), left + (right - left - width) // 2, width, margin, f.height - margin)
    body = base.dimmed(log_body(p)) + sheet
    return p.document("Pick people", body, background=c["sheetScrim"])


PAGES: list[tuple[str, object, Frame]] = [
    ("Interactions", interactions_screen, IPAD_PORTRAIT),
    ("Interactions", interactions_screen, IPAD_LANDSCAPE),
    ("Interactions", interactions_screen, fold.OPEN),
    ("Interactions", interactions_screen, fold.OPEN_PORTRAIT),
    ("Interactions", interactions_screen, fold.BOOK),
    ("Interactions", interactions_screen, fold.TABLETOP),
    ("Interactions", interactions_screen, fold.FLIP_TABLETOP),
    ("InteractionsPicker", picker_screen, IPAD_LANDSCAPE),
    ("InteractionsPicker", picker_screen, fold.BOOK),
]


def outputs() -> dict[Path, str]:
    files = {}
    for name, make, frame in PAGES:
        for dark in (False, True):
            page = Page(frame, dark)
            html = make(page)  # type: ignore[operator]
            for script in ("ipad_mockups.py", "fold_mockups.py"):
                html = html.replace(f"Generated by tools/visual/{script}", "Generated by tools/visual/interactions_mockups.py")
            files[SCREENS / f"{name}.{frame.name}{page.suffix()}.dc.html"] = html
    return files


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--check", action="store_true", help="write nothing; fail when a file is not what would be written")
    args = parser.parse_args(argv)
    stale = []
    for path, text in outputs().items():
        current = path.read_text(encoding="utf-8") if path.is_file() else None
        if current == text:
            continue
        if args.check:
            stale.append(path.name)
        else:
            path.write_text(text, encoding="utf-8", newline="\n")
            print(f"wrote {path.relative_to(ROOT)}")
    if stale:
        print("not current (run tools/visual/interactions_mockups.py): " + ", ".join(stale), file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
