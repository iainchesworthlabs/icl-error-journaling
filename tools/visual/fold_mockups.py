#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Write the foldable mock-ups of Stage 13 (docs/design/screens/*.fold-*.dc.html and *.flip-*.dc.html).

    python tools/visual/fold_mockups.py [--check]

The foldable pages follow the same layout rules as the iPad pages of Stage 8, widened by the rules of
docs/plan/stage-13.md ("Layout rules"). They reuse the building blocks of tools/visual/ipad_mockups.py (the header card,
the composer, the log rows, the sheets) and take every colour from docs/design/tokens.json. Change a rule here and in the
plan page together, then run the script again. With --check it writes nothing and fails when a file differs from what it
would write.

The frames are design frames, not device specifications. Each is named by its shape (unfolded, cover screen, posture), and
the plan page says which devices fall near it. The fold drawn on the half-folded pages is an annotation, not part of the UI.
"""

from __future__ import annotations

import argparse
import re
import sys
from dataclasses import dataclass
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))

import ipad_mockups as base  # the shared building blocks of Stage 8
from ipad_mockups import ENTRIES, TYPES, icon

ROOT = base.ROOT
SCREENS = base.SCREENS

# Layout rules (docs/plan/stage-13.md). Compact values are the phone's; Regular and Wide values are Stage 8's.
COMPACT = {
    "margin": 16,
    "gap": 12,
    "tile_gap": 8,
    "label": 13.5,
    "badge": 24,
    "badge_text": 13,
    "tile_pad": 12,
}
LARGE = {
    "margin": 24,
    "gap": 12,
    "tile_gap": 12,
    "label": 17,
    "badge": 28,
    "badge_text": 15,
    "tile_pad": 14,
}
TAB_CONTENT = 49
RAIL = 72
FOLD_CLEAR = (
    12  # nothing interactive within this distance of a fold line, on either side
)
SHEET_WIDTH = base.SHEET_WIDTH
BOARD_SHARE = base.BOARD_SHARE
PANE_GAP = base.PANE_GAP
COMPOSER = base.COMPOSER
HEADER_CARD = base.HEADER_CARD
LAST_BAR = 44
READING_COLUMN = base.READING_COLUMN
FORM_COLUMN = base.FORM_COLUMN
NOTE_PLACEHOLDER = "Type a note, then hold a tile…"


@dataclass(frozen=True)
class Frame:
    """A design frame: its size in points (dp on Android), its safe-area insets, and the fold, when it divides the screen."""

    name: str
    title: str
    width: int
    height: int
    top: int
    bottom: int
    right: int = 0
    fold: str = ""  # "", "vertical" (book posture) or "horizontal" (tabletop posture)

    @property
    def landscape(self) -> bool:
        return self.width > self.height

    @property
    def compact(self) -> bool:
        return self.width < 600

    @property
    def wide(self) -> bool:
        w, h = self.width, self.height
        return h >= 640 and ((w >= 1100) or (w >= 648 and w >= 0.8 * h))

    @property
    def rail(self) -> bool:
        """The tab bar is a rail on the trailing edge when the window is short, except in tabletop posture."""
        return self.height < 720 and self.width >= 400 and self.fold != "horizontal"

    @property
    def fold_at(self) -> float:
        return (self.width if self.fold == "vertical" else self.height) / 2


OPEN = Frame("fold-open", "unfolded, landscape", 841, 701, 24, 20)
OPEN_PORTRAIT = Frame("fold-open-portrait", "unfolded, portrait", 701, 841, 24, 20)
COVER = Frame("fold-cover", "narrow cover screen", 344, 882, 40, 20)
SHORT = Frame("fold-short", "short cover screen", 466, 678, 12, 20, right=44)
BOOK = Frame("fold-book", "half folded, book", 841, 701, 24, 20, fold="vertical")
TABLETOP = Frame(
    "fold-tabletop", "half folded, tabletop", 701, 841, 24, 20, fold="horizontal"
)
FLIP_TABLETOP = Frame(
    "flip-tabletop", "flip phone, tabletop", 412, 915, 40, 20, fold="horizontal"
)


class Page(base.Page):
    """A foldable page. It reuses Stage 8's blocks and adds the rail, the posture panes and the Compact sizes."""

    def __init__(self, frame: Frame, dark: bool) -> None:
        super().__init__(frame, dark)  # type: ignore[arg-type]
        self.f = frame
        self.r = COMPACT if frame.compact else LARGE
        # False on a page that hides the tabs (Today's details is pushed over the Log screen, as on the phone).
        self.tabs = True

    # ---- geometry ----

    def content(self) -> tuple[int, int]:
        """The left and right x of the area for content: inside the margins, the rail and the side inset."""
        right = (
            self.f.width
            - self.f.right
            - (RAIL if self.f.rail and self.tabs else 0)
            - self.r["margin"]
        )
        return self.r["margin"], right

    def column(self, limit: int) -> tuple[int, int]:
        left, right = self.content()
        width = min(limit, right - left)
        return left + (right - left - width) // 2, width

    def bottom_edge(self) -> int:
        """Where content stops: above the tab bar, or above the home indicator when the tab bar is a rail."""
        if self.f.rail:
            return self.f.height - self.f.bottom - self.r["gap"]
        return self.f.height - (TAB_CONTENT + self.f.bottom) - 16

    # ---- blocks ----

    def composer(self) -> str:
        # The phone's composer, whose chips wrap onto a second row when the card is narrow (NoteComposer.qml).
        html = super().composer()
        html = html.replace(f"height: {COMPOSER}px; ", "min-height: 110px; ")
        html = html.replace(
            '<div style="display: flex; gap: 8px">',
            '<div style="display: flex; flex-wrap: wrap; gap: 8px">',
            1,
        )
        html = html.replace(
            '<div style="flex-grow: 1; height: 44px;',
            '<div style="flex-grow: 1; min-width: 0; height: 44px;',
            1,
        )
        return html.replace(
            "Add a note to today’s log…",
            f'<span style="white-space: nowrap; overflow: hidden; text-overflow: ellipsis">{NOTE_PLACEHOLDER}</span>',
        )

    def board(self, width: int, tile_height: int, gap: int | None = None) -> str:
        c, r = self.c, self.r
        gap = r["tile_gap"] if gap is None else gap
        pad = r["tile_pad"]
        tiles = []
        for (name, count), fill in zip(TYPES, self.tiles):
            badge = ""
            if count:
                badge = (
                    f'<span style="position: absolute; top: {pad - 4}px; right: {pad - 4}px; min-width: {r["badge"]}px; '
                    f"height: {r['badge']}px; padding: 0 7px; box-sizing: border-box; border-radius: {r['badge'] // 2}px; "
                    f"background: {c['badgeBackground']}; color: {c['badgeText']}; font-size: {r['badge_text']}px; font-weight: 700; "
                    f'display: flex; align-items: center; justify-content: center">{count}</span>'
                )
            aria = name + (f", {count} today" if count else "")
            tiles.append(
                f'<button class="tile" aria-label="{aria}" style="position: relative; height: {tile_height}px; border-radius: 16px; '
                f"border: 0; background: {fill}; padding: {pad}px; display: flex; align-items: flex-end; text-align: left; "
                f"font-family: inherit; font-size: {r['label']}px; font-weight: 600; line-height: 1.2; color: {c['textPrimary']}; "
                f'box-shadow: {"none" if self.dark else "0 1px 2px rgba(20,33,43,0.08)"}"><span>{name}</span>{badge}</button>'
            )
        tiles.append(
            f'<a href="Other.{self.f.name}{self.suffix()}.dc.html" aria-label="Other error type" style="height: {tile_height}px; '
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

    def tab_items(self, selected: int, vertical: bool) -> str:
        c = self.c
        names = [
            ("log", "Log", "Main"),
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
                f'{size}{pill}gap: 3px; text-decoration: none; color: {colour}; font-size: 10.5px; font-weight: {weight}">{icon(glyph, colour)}{label}</a>'
            )
        return "".join(items)

    def tab_bar(self, selected: int, top: int | None = None, half: bool = False) -> str:
        """The tab bar: a rail on the trailing edge when the window is short, otherwise a bar at the foot of the window (or,
        in tabletop posture, at the foot of the upper half, `top` giving its y)."""
        c = self.c
        if self.f.rail:
            x = self.f.width - self.f.right - RAIL
            return (
                f'<nav aria-label="Tabs" style="position: absolute; left: {x}px; width: {RAIL}px; top: 0; bottom: 0; box-sizing: border-box; '
                f"padding: {self.f.top + 12}px 0 {self.f.bottom}px; background: {c['surface']}; border-left: 1px solid {c['tabBarBorder']}; "
                f'display: flex; flex-direction: column; justify-content: center; gap: 8px">{self.tab_items(selected, True)}</nav>'
                + self.side_inset()
            )
        _, width = self.column(base.LOG_COLUMN)
        if top is None:
            place = f"bottom: 0; height: {TAB_CONTENT + self.f.bottom}px; padding-top: 8px; border-top: 1px solid {c['tabBarBorder']}"
        else:
            place = (
                f"top: {top}px; height: {TAB_CONTENT}px; padding-top: 6px; border-top: 1px solid {c['tabBarBorder']}; "
                f"border-bottom: 1px solid {c['tabBarBorder']}"
            )
        return (
            f'<nav aria-label="Tabs" style="position: absolute; left: 0; right: 0; {place}; box-sizing: border-box; '
            f'background: {c["surface"]}; display: flex; justify-content: center">'
            f'<div style="width: {width}px; display: grid; grid-template-columns: repeat(4, minmax(0, 1fr))">{self.tab_items(selected, False)}</div></nav>'
        )

    def side_inset(self) -> str:
        """The status bar and camera on the side of a short cover screen, drawn as a placeholder (its real width comes from
        the safe area at run time)."""
        if not self.f.right:
            return ""
        c = self.c
        x = self.f.width - self.f.right
        return (
            f'<div aria-hidden="true" style="position: absolute; left: {x}px; width: {self.f.right}px; top: 0; bottom: 0; '
            f"display: flex; flex-direction: column; align-items: center; padding-top: 18px; gap: 10px; box-sizing: border-box; "
            f'font-size: 11px; font-weight: 600; color: {c["textSecondary"]}">'
            f'<span style="width: 14px; height: 14px; border-radius: 7px; background: #0B0F13"></span>'
            f'<span style="writing-mode: vertical-rl">10:21</span></div>'
        )

    def fold_mark(self) -> str:
        """The fold, as an annotation: a soft band with a dashed centre line and a label, like the notes of TileLabels."""
        if not self.f.fold:
            return ""
        c = self.c
        at = self.f.fold_at
        band = 2 * FOLD_CLEAR
        shade = "rgba(255,255,255,0.05)" if self.dark else "rgba(20,33,43,0.05)"
        if self.f.fold == "vertical":
            box = f"left: {at - FOLD_CLEAR}px; width: {band}px; top: 0; bottom: 0"
            line = f"border-left: 1.5px dashed {c['borderStrong']}; position: absolute; left: {FOLD_CLEAR}px; top: 0; bottom: 0"
            label = f"position: absolute; left: -8px; width: {band + 16}px; text-align: center; bottom: 4px"
        else:
            box = f"top: {at - FOLD_CLEAR}px; height: {band}px; left: 0; right: 0"
            line = f"border-top: 1.5px dashed {c['borderStrong']}; position: absolute; top: {FOLD_CLEAR}px; left: 0; right: 0"
            label = f"position: absolute; right: 8px; top: {FOLD_CLEAR + 1}px"
        return (
            f'<div aria-hidden="true" style="position: absolute; {box}; background: {shade}; pointer-events: none">'
            f'<div style="{line}"></div>'
            f'<span style="{label}; font-size: 10px; font-weight: 600; letter-spacing: 0.4px; color: {c["textSecondary"]}">FOLD</span></div>'
        )

    def log_pane(self, x: int, width: int, top: int, bottom: int, toast: bool) -> str:
        """Stage 8's log pane: the header card, today's log with its count and Copy, and Share log."""
        c = self.c
        border = f"border: 1px solid {c['reviewDivider']}; " if self.dark else ""
        html = (
            f'<div style="position: absolute; left: {x}px; width: {width}px; top: {top}px; bottom: {self.f.height - bottom}px; '
            f'display: flex; flex-direction: column; gap: {self.r["gap"]}px">'
            f"{self.header_card()}"
            f'<section aria-label="Today’s log" style="flex-grow: 1; min-height: 0; background: {c["reviewSurface"]}; {border}border-radius: 16px; '
            f'display: flex; flex-direction: column; overflow: hidden; position: relative">'
            f'<div style="display: flex; align-items: center; gap: 8px; height: 52px; flex-shrink: 0; padding: 0 6px 0 14px; '
            f'border-bottom: 1px solid {c["reviewDivider"]}">'
            f'<span style="font-size: 17px; font-weight: 700; color: {c["reviewText"]}; flex-grow: 1">Today’s log</span>'
            f'<span style="font-size: 14px; color: {c["reviewTextSecondary"]}">7 entries</span>'
            f'<button aria-label="Copy whole log" style="width: 44px; height: 44px; border: 0; background: transparent; display: flex; '
            f'align-items: center; justify-content: center">{icon("copy", c["primaryText"], 20)}</button></div>'
            f'<div style="flex-grow: 1; min-height: 0; overflow: hidden; padding-bottom: 76px">{self.log_rows(14)}</div>'
            f'<div style="padding: 12px 14px 14px; flex-shrink: 0">{self.primary_button("Share log", "share")}</div>'
            "</section></div>"
        )
        if toast:
            html += self.toast(
                x + 14, width - 28, self.f.height - bottom + 14 + 54 + 12
            )
        return html

    def log_strip(self, rows: int) -> str:
        """The Last bar grown into a short log: the newest entries, newest last, with the count. A tap opens Review."""
        c = self.c
        border = f"border: 1px solid {c['reviewDivider']}; " if self.dark else ""
        lines = []
        for i, (time, marker, text) in enumerate(ENTRIES[-rows:]):
            sep = (
                ""
                if i == rows - 1
                else f"border-bottom: 1px solid {c['reviewDivider']}; "
            )
            lines.append(
                f'<div style="display: flex; gap: 10px; padding: 7px 12px; {sep}font-size: 14px; line-height: 1.3; color: {c["reviewText"]}; '
                f'white-space: nowrap; overflow: hidden; text-overflow: ellipsis">'
                f'<span style="font-variant-numeric: tabular-nums; color: {c["reviewTextSecondary"]}">{time}</span>'
                f'<span style="overflow: hidden; text-overflow: ellipsis"><span style="color: {c["reviewTextSecondary"]}">{marker}</span> {text}</span></div>'
            )
        return (
            f'<a href="Review.{self.f.name}{self.suffix()}.dc.html" aria-label="Today’s log, 7 entries. Opens Review." '
            f'style="display: block; background: {c["reviewSurface"]}; {border}border-radius: 14px; overflow: hidden; text-decoration: none">'
            f'<div style="display: flex; align-items: center; gap: 8px; height: 30px; padding: 0 12px; border-bottom: 1px solid {c["reviewDivider"]}">'
            f'<span style="font-size: 12px; font-weight: 600; color: {c["reviewTextSecondary"]}; text-transform: uppercase; letter-spacing: 0.5px; '
            f'flex-grow: 1">Today’s log</span><span style="font-size: 13px; color: {c["reviewTextSecondary"]}">7 today</span>'
            f"{icon('chevron', c['reviewTextSecondary'], 14)}</div>{''.join(lines)}</a>"
        )

    def document(self, title: str, body: str, background: str | None = None) -> str:
        html = super().document(title, body + self.fold_mark(), background)
        html = html.replace(
            f"<title>{title} – iPad {'landscape' if self.f.landscape else 'portrait'}",
            f"<title>{title} – {self.f.title}",
        )
        html = html.replace(
            "Generated by tools/visual/ipad_mockups.py",
            "Generated by tools/visual/fold_mockups.py",
        )
        return relink(html, self.suffix())


def relink(html: str, suffix: str) -> str:
    """Point each link at a page that exists: the same screen in this frame, else unfolded, else the phone's."""

    def target(match: re.Match[str]) -> str:
        name, frame = match.group(1), match.group(2)
        for candidate in (
            f"{name}.{frame}{suffix}.dc.html",
            f"{name}.{OPEN.name}{suffix}.dc.html",
            f"{name}{suffix}.dc.html",
        ):
            if candidate in PLANNED or (SCREENS / candidate).is_file():
                return f'href="{candidate}"'
        return match.group(0)

    return re.sub(
        r'href="([A-Za-z]+)\.((?:fold|flip)-[a-z-]+?)(?:\.dark)?\.dc\.html"',
        target,
        html,
    )


# ---- the Log screen ----


def column_log(p: Page, toast: bool = True) -> str:
    """The phone's column (Compact), on a cover screen. Short screens use 8 pt gaps and put the toast in the Last bar's place."""
    f, r = p.f, p.r
    left, right = p.content()
    width = right - left
    short = f.rail
    gap = 8 if short else r["gap"]
    top = f.top + (12 if short else 16)
    chip_rows = 2 if width < 304 else 1
    composer = COMPOSER + (42 if chip_rows == 2 else 0)
    used = HEADER_CARD + composer + LAST_BAR + 3 * gap
    tile_height = int(min(104, (p.bottom_edge() - top - used - 3 * r["tile_gap"]) // 4))
    last = p.last_bar()
    if toast and short:
        last = (
            p.toast(0, width, 0)
            .replace(
                "position: absolute; left: 0px; width", "position: relative; width"
            )
            .replace("bottom: 0px; height: 52px", "height: 44px")
        )
    body = (
        f'<div style="position: absolute; left: {left}px; width: {width}px; top: {top}px; display: flex; flex-direction: column; gap: {gap}px">'
        f"{p.header_card()}{p.composer()}{last}{p.board(width, tile_height)}</div>"
    )
    if toast and not short:
        body += p.toast(left, width, TAB_CONTENT + f.bottom + 12)
    return body + p.tab_bar(0)


def wide_log(p: Page, toast: bool = True) -> str:
    """Board and log side by side (Stage 8's Wide), with the rail when the window is short."""
    f, r = p.f, p.r
    left, right = p.content()
    inner = right - left
    board_width = round((inner - PANE_GAP) * BOARD_SHARE)
    log_x = left + board_width + PANE_GAP
    log_width = right - log_x
    top = f.top + 16
    bottom = p.bottom_edge()
    board_top = top + COMPOSER + r["gap"]
    tile_height = int((bottom - board_top - 3 * r["tile_gap"]) // 4)
    body = (
        f'<div style="position: absolute; left: {left}px; width: {board_width}px; top: {top}px; display: flex; '
        f'flex-direction: column; gap: {r["gap"]}px">{p.composer()}{p.board(board_width, tile_height)}</div>'
    )
    return body + p.log_pane(log_x, log_width, top, bottom, toast) + p.tab_bar(0)


def book_log(p: Page, toast: bool = True) -> str:
    """Book posture: the board in the leading half, the log in the trailing half, each clear of the fold."""
    f, r = p.f, p.r
    left, right = p.content()
    fold = f.fold_at
    board_right = int(fold - FOLD_CLEAR)
    log_left = int(fold + FOLD_CLEAR)
    top = f.top + 16
    bottom = p.bottom_edge()
    board_width = board_right - left
    tile_height = int((bottom - (top + COMPOSER + r["gap"]) - 3 * r["tile_gap"]) // 4)
    body = (
        f'<div style="position: absolute; left: {left}px; width: {board_width}px; top: {top}px; display: flex; '
        f'flex-direction: column; gap: {r["gap"]}px">{p.composer()}{p.board(board_width, tile_height)}</div>'
    )
    return (
        body + p.log_pane(log_left, right - log_left, top, bottom, toast) + p.tab_bar(0)
    )


def tabletop_log(p: Page, toast: bool = True) -> str:
    """Tabletop posture: the board on the flat lower half, under the hand; the header card, the composer, a short log and
    the tab bar on the upright upper half. Nothing sits on the fold."""
    f = p.f
    left, right = p.content()
    width = right - left
    fold = int(f.fold_at)
    top = f.top + 12
    tab_top = fold - FOLD_CLEAR - TAB_CONTENT
    if width >= 600:
        # Room for the header card and the composer side by side, which leaves the short log more rows.
        card = round((width - 8) * 0.45)
        heads = (
            f'<div style="display: flex; gap: 8px; align-items: flex-start"><div style="width: {card}px; flex-shrink: 0">{p.header_card()}</div>'
            f'<div style="flex-grow: 1; min-width: 0">{p.composer()}</div></div>'
        )
        strip_top = top + COMPOSER + 8
    else:
        heads = p.header_card() + p.composer()
        strip_top = top + HEADER_CARD + 8 + COMPOSER + 8
    rows = max(1, (tab_top - 8 - strip_top - 30) // 33)
    upper = (
        f'<div style="position: absolute; left: {left}px; width: {width}px; top: {top}px; display: flex; flex-direction: column; gap: 8px">'
        f"{heads}{p.log_strip(rows)}</div>"
    )
    board_top = fold + FOLD_CLEAR
    board_bottom = f.height - f.bottom - 8
    gap = 8
    tile_height = int((board_bottom - board_top - 3 * gap) // 4)
    lower = f'<div style="position: absolute; left: {left}px; width: {width}px; top: {board_top}px">{p.board(width, tile_height, gap)}</div>'
    body = upper + lower + p.tab_bar(0, top=tab_top)
    if toast:
        # Over the title and the oldest rows of the short log, so it covers no tile and not the newest entry.
        body += p.toast(left + 8, width - 16, f.height - strip_top - 52 - 2)
    return body


def log_body(p: Page, toast: bool = True) -> str:
    if p.f.fold == "vertical":
        return book_log(p, toast)
    if p.f.fold == "horizontal":
        return tabletop_log(p, toast)
    if p.f.wide:
        return wide_log(p, toast)
    return column_log(p, toast)


def main_screen(p: Page) -> str:
    return p.document("Log – main board", log_body(p))


# ---- pages in columns ----


def review_screen(p: Page) -> str:
    c = p.c
    x, width = p.column(READING_COLUMN)
    body = (
        f'<div style="position: absolute; left: {x}px; width: {width}px; top: {p.f.top}px; display: flex; flex-direction: column; gap: 12px">'
        f'<div style="display: flex; align-items: center; justify-content: space-between; height: 44px">'
        f'<span style="font-size: 15px; color: {c["textSecondary"]}">Sat 7 Nov · 7 entries</span>'
        f'<span aria-label="Copy whole log" style="width: 44px; height: 44px; display: flex; align-items: center; justify-content: center">'
        f"{icon('copy', c['primaryText'], 22)}</span></div>"
        f'<h1 style="margin: -4px 0 0; font-size: 30px; font-weight: 700; letter-spacing: -0.4px">Today’s log</h1>'
        f"{p.header_block()}{p.review_card()}"
        f'<div style="margin-top: 4px">{p.primary_button("Share log", "share")}</div></div>'
        + p.tab_bar(1)
    )
    return p.document("Today’s log", body, background=c["reviewBackground"])


def header_screen(p: Page) -> str:
    """Today's details: Stage 8's page in the form column. It covers the tabs, as on the phone, so the rail is not shown and
    the column centres in the whole window. In a short window the page scrolls; the picture is cut where the window ends."""
    p.tabs = False
    return base.header_screen(p)


def settings_screen(p: Page) -> str:
    c = p.c
    x, width = p.column(FORM_COLUMN)
    body = (
        f'<div style="position: absolute; left: {x}px; width: {width}px; top: {p.f.top + 16}px; display: flex; flex-direction: column; gap: 14px">'
        f'<h1 style="margin: 0; font-size: 30px; font-weight: 700; letter-spacing: -0.4px">Settings</h1>'
        f"{p.section_label('Your board')}{base.mini_board(p, width)}"
        f'<p style="margin: -4px 4px 0; font-size: 13px; line-height: 1.4; color: {c["textSecondary"]}">Hold and drag a tile to move it. '
        f"Tap a tile to change its error type, or + to add one. Each type keeps its colour.</p>"
        + p.grouped(
            [
                ("Error types", "", True),
                ("Note templates", "", True),
                ("Locations", "", True),
            ]
        )
        + p.grouped(
            [("Tester", "Sam Example", True), ("Time format", "24-hour", True)]
        )
        + p.grouped([("Import or export setup", "", True)])
        + "</div>"
        + p.tab_bar(3)
    )
    return p.document("Settings", body)


# ---- sheets ----


def sheet_card(
    p: Page, inner: str, left: int, width: int, top: int, bottom: int
) -> str:
    """A form sheet placed in a region (the window, or the half of it away from the fold), as tall as its content and no
    taller than the region, scrolling inside beyond that."""
    c = p.c
    return (
        f'<div style="position: absolute; left: {left}px; width: {width}px; top: {top}px; height: {bottom - top}px; display: flex; '
        f'align-items: center; pointer-events: none"><div role="dialog" style="pointer-events: auto; width: 100%; max-height: 100%; '
        f"padding-bottom: 16px; box-sizing: border-box; background: {c['sheetBackground']}; border-radius: 16px; overflow: hidden; display: flex; "
        f'flex-direction: column; box-shadow: 0 20px 48px rgba(0,0,0,0.3)">{inner}</div></div>'
    )


def other_inner(p: Page) -> str:
    c = p.c
    swatch = f"width: 14px; height: 14px; border-radius: 4px; border: 1px solid {c['border']}"

    def row(label: str, fill: str, count: str = "", last: bool = False) -> str:
        border = "" if last else f"border-bottom: 1px solid {c['divider']}; "
        tail = (
            f'<span style="font-size: 14px; color: {c["textSecondary"]}">{count}</span>'
            if count
            else ""
        )
        return (
            f'<div style="height: 52px; flex-shrink: 0; display: flex; align-items: center; gap: 12px; padding: 0 14px; {border}font-size: 16px">'
            f'<span style="{swatch}; background: {fill}"></span><span style="flex-grow: 1; white-space: nowrap; overflow: hidden; '
            f'text-overflow: ellipsis">{label}</span>{tail}</div>'
        )

    off = row("[Your other error type]", c["swatch"]) + row(
        "[Another error type]", c["swatch"], last=True
    )
    on = "".join(
        [
            row("Navigation Failure", p.tiles[10]),
            row("Audio Drop Out", p.tiles[5]),
            row("Track Not Found", p.tiles[3]),
            row("Wrong Response", p.tiles[0], "2 today"),
            row("Mute Delay", p.tiles[7], "1 today", last=True),
        ]
    )
    return (
        base.sheet_nav(p, "Cancel", "Log another error")
        + f'<div style="padding: 0 16px 12px; flex-shrink: 0"><div style="display: flex; align-items: center; gap: 8px; height: 40px; border-radius: 10px; '
        f'background: {c["searchBackground"]}; padding: 0 10px; font-size: 16px; color: {c["textPlaceholder"]}">'
        f"{icon('search', c['textSecondary'], 18)}Search error types</div></div>"
        f'<div style="padding: 0 16px; display: flex; flex-direction: column; gap: 12px; min-height: 0; overflow: hidden">'
        f'{p.section_label("Not on your board")}<div style="background: {c["surfaceRaised"]}; border-radius: 14px; overflow: hidden; flex-shrink: 0">{off}</div>'
        f'{p.section_label("On your board")}<div style="background: {c["surfaceRaised"]}; border-radius: 14px; overflow: hidden">{on}</div></div>'
    )


def other_screen(p: Page) -> str:
    c = p.c
    margin = p.f.top + 40
    if p.f.fold == "vertical":
        # The sheet goes in the trailing half, clear of the fold; the board stays in sight, dimmed, in the other.
        left = int(p.f.fold_at + FOLD_CLEAR)
        right = p.content()[1] + p.r["margin"] - 12
        sheet = sheet_card(
            p,
            other_inner(p),
            left,
            right - left,
            p.f.top + 16,
            p.f.height - p.f.bottom - 16,
        )
    else:
        left, right = p.content()
        width = min(SHEET_WIDTH, right - left)
        x = left + (right - left - width) // 2
        sheet = sheet_card(p, other_inner(p), x, width, margin, p.f.height - margin)
    body = base.dimmed(log_body(p, toast=False)) + sheet
    return p.document("Other error picker", body, background=c["sheetScrim"])


def keyboard(p: Page, top: int) -> str:
    """The software keyboard, drawn as keys without letters: it is the system's, not the app's."""
    c = p.c
    fill = "#2B3138" if p.dark else "#D3D8DE"
    key = "#4A525B" if p.dark else "#FFFFFF"
    rows = []
    for count in (10, 9, 7):
        keys = "".join(
            f'<span style="flex: 1; max-width: 58px; height: 42px; border-radius: 6px; background: {key}"></span>'
            for _ in range(count)
        )
        rows.append(
            f'<div style="display: flex; justify-content: center; gap: 6px">{keys}</div>'
        )
    rows.append(
        f'<div style="display: flex; justify-content: center; gap: 6px"><span style="width: 420px; height: 42px; border-radius: 6px; background: {key}"></span>'
        f'<span style="width: 96px; height: 42px; border-radius: 6px; background: {c["primary"]}"></span></div>'
    )
    return (
        f'<div aria-hidden="true" style="position: absolute; left: 0; right: 0; top: {top}px; bottom: 0; background: {fill}; padding: 10px 8px; '
        f'box-sizing: border-box; display: flex; flex-direction: column; gap: 10px">{"".join(rows)}'
        f'<span style="text-align: center; font-size: 11px; color: {c["textSecondary"]}">System keyboard (annotation)</span></div>'
    )


def edit_entry_screen(p: Page) -> str:
    """Edit entry with a note being typed, in tabletop posture: the sheet in the upright half, the keyboard in the flat half."""
    c = p.c

    def field(label: str, value: str) -> str:
        return (
            f'<div style="display: flex; align-items: center; height: 48px; padding: 0 14px; border-bottom: 1px solid {c["divider"]}; font-size: 16px">'
            f'<span style="flex-grow: 1">{label}</span><span style="color: {c["textSecondary"]}">{value}</span>'
            f"{icon('chevron', c['textSecondary'], 14)}</div>"
        )

    inner = (
        base.sheet_nav(p, "Cancel", "Edit entry", "Save")
        + f'<div style="padding: 0 16px; display: flex; flex-direction: column; gap: 10px; min-height: 0; overflow: hidden">'
        f'<div style="background: {c["surfaceRaised"]}; border-radius: 14px; overflow: hidden; flex-shrink: 0">'
        f"{field('Error', 'Wrong Track Played')}"
        f'<div style="display: flex; align-items: center; height: 48px; padding: 0 14px; font-size: 16px"><span style="flex-grow: 1">Time</span>'
        f'<span style="padding: 6px 10px; border-radius: 8px; background: {c["timeChipBackground"]}; font-size: 16px; font-weight: 500; '
        f'font-variant-numeric: tabular-nums">11:02</span></div></div>'
        f"{p.section_label('Note')}"
        f'<div style="background: {c["surfaceRaised"]}; border: 2px solid {c["primaryText"]}; border-radius: 14px; padding: 12px 14px; font-size: 17px; '
        f'min-height: 64px; box-sizing: border-box; flex-shrink: 0">asked for Evening Song<span style="display: inline-block; width: 2px; height: 20px; '
        f'margin-left: 1px; vertical-align: -3px; background: {c["primaryText"]}"></span></div>'
        f"{p.section_label('In the shared log')}"
        f'<div style="background: {c["surfaceRaised"]}; border-radius: 14px; padding: 12px 14px; font-size: 14px; line-height: 1.45">'
        f"11:02 T| Wrong Track Played – asked for Evening Song</div></div>"
    )
    fold = int(p.f.fold_at)
    left, right = p.content()
    width = min(SHEET_WIDTH, right - left)
    x = left + (right - left - width) // 2
    sheet = sheet_card(p, inner, x, width, p.f.top + 8, fold - FOLD_CLEAR)
    body = (
        base.dimmed(log_body(p, toast=False)) + sheet + keyboard(p, fold + FOLD_CLEAR)
    )
    return p.document("Edit entry", body, background=c["sheetScrim"])


PAGES: list[tuple[str, object, Frame]] = [
    ("Main", main_screen, OPEN),
    ("Main", main_screen, OPEN_PORTRAIT),
    ("Main", main_screen, COVER),
    ("Main", main_screen, SHORT),
    ("Main", main_screen, BOOK),
    ("Main", main_screen, TABLETOP),
    ("Main", main_screen, FLIP_TABLETOP),
    ("Review", review_screen, OPEN),
    ("Header", header_screen, OPEN),
    ("BoardSettings", settings_screen, OPEN),
    ("Other", other_screen, OPEN),
    ("Other", other_screen, BOOK),
    ("EditEntry", edit_entry_screen, TABLETOP),
]

PLANNED = {
    f"{name}.{frame.name}{suffix}.dc.html"
    for name, _, frame in PAGES
    for suffix in ("", ".dark")
}


def outputs() -> dict[Path, str]:
    files = {}
    for name, make, frame in PAGES:
        for dark in (False, True):
            page = Page(frame, dark)
            files[SCREENS / f"{name}.{frame.name}{page.suffix()}.dc.html"] = make(page)  # type: ignore[operator]
    return files


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(
        description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter
    )
    parser.add_argument(
        "--check",
        action="store_true",
        help="write nothing; fail when a file is not what would be written",
    )
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
        print(
            "not current (run tools/visual/fold_mockups.py): " + ", ".join(stale),
            file=sys.stderr,
        )
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
