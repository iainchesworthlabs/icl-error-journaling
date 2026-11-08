// SPDX-License-Identifier: GPL-3.0-or-later
pragma Singleton
import QtQuick

// The design tokens of docs/design/tokens.json as one singleton (Stage 4, package 4.1). A component or a screen uses
// nothing but these values: no colour, size, radius, spacing or duration is written anywhere else, and
// tools/ci/check_qml_literals.py fails a file that does. tokens.json is the contract with the design and this file is
// the implementation; the test `theme.matches_tokens` compares every value, so the two cannot drift.
//
// Three inputs are set from outside, by the shell (4.15) from the bridge (4.3) and by a test from the rig:
//   colourScheme  follows the system appearance until it is set (Application.styleHints.colorScheme, assumption U6)
//   textScale     the Dynamic Type multiplier, 1.0 where there is no bridge
//   reduceMotion  the Reduce Motion setting
QtObject {
    id: root

    // ---- inputs ----

    // Qt.Light, Qt.Dark or Qt.Unknown (treated as light).
    property int colourScheme: Application.styleHints.colorScheme
    // The Dynamic Type multiplier from the bridge. It is clamped to the range of the iOS content size categories.
    property real textScale: 1.0
    property bool reduceMotion: false

    readonly property bool dark: colourScheme === Qt.Dark
    readonly property real clampedScale: Math.max(size.textScaleMin, Math.min(size.textScaleMax, textScale))

    // The multiplier a type role gets: textScale within the iOS range, and within the role's cap when it has one
    // (0 = no cap).
    function scaleFor(cap: real): real {
        const scale = Math.max(size.textScaleMin, Math.min(size.textScaleMax, textScale));
        return cap > 0 ? Math.min(scale, cap) : scale;
    }

    // The multiplier of a named role of `font` ("tileLabel"), with its cap.
    function scaled(role: string): real {
        const entry = font[role];
        return entry ? entry.scale : scaleFor(0);
    }

    // The fill of a board tile. `slot` is the colourSlot role of BoardModel: 0 for Other, 1 to 11 for a type. A value
    // outside that range gives the Other fill, so a bad role never gives a colour that means something.
    function tileColour(slot: int): color {
        const fills = dark ? tileFillsDark : tileFillsLight;
        return Qt.color(slot >= 1 && slot <= fills.length ? fills[slot - 1] : colour.otherTileFill);
    }

    // The tile fills in slot order, slot 1 first: sky, lavender, apricot, aqua, orchid, butter, steel, periwinkle,
    // sand, oat and blush.
    readonly property var tileFillsLight: ["#DCE8F7", "#E6E0F5", "#FBE3CF", "#D3ECEE", "#F3DDEB", "#FFF0C2", "#E0E6EE", "#D9E2FB", "#F6E2D6", "#ECE5D3", "#F5D9E0"]
    readonly property var tileFillsDark: ["#2E486B", "#45396A", "#63462C", "#225359", "#62375E", "#5C4E1F", "#424B57", "#333F71", "#514238", "#504835", "#5F3A48"]
    readonly property int tileSlots: tileFillsLight.length

    // ---- colour ----

    component Colour: QtObject {
        required property bool dark
        readonly property color background: dark ? "#0E141A" : "#F2F4F7"
        readonly property color surface: dark ? "#18212A" : "#FFFFFF"
        readonly property color textPrimary: dark ? "#EEF2F6" : "#14212B"
        readonly property color textSecondary: dark ? "#A7B6C3" : "#5A6B78"
        readonly property color textPlaceholder: dark ? "#93A3B0" : "#6B7A86"
        readonly property color textBody: dark ? "#C7D3DD" : "#3D4C58"
        readonly property color primary: dark ? "#2A6F96" : "#1E5B7A"
        readonly property color primaryPressed: dark ? "#1E5B7A" : "#15435A"
        readonly property color primaryTint: dark ? "#1B2F3D" : "#EAF1F5"
        readonly property color headerCard: dark ? "#5B2A63" : "#5B2A63"
        readonly property color headerCardTextSecondary: dark ? "#EBDDF0" : "#EBDDF0"
        readonly property color border: dark ? "#34434F" : "#C9D3DC"
        readonly property color borderStrong: dark ? "#74869A" : "#9AA8B4"
        readonly property color divider: dark ? "#27333E" : "#E8ECF0"
        readonly property color tabBarBorder: dark ? "#27333E" : "#E1E6EB"
        readonly property color fieldBackground: dark ? "#121A21" : "#F7F9FB"
        readonly property color searchBackground: dark ? "#26323D" : "#E3E8ED"
        readonly property color timeChipBackground: dark ? "#2B3844" : "#E8EEF3"
        readonly property color destructive: dark ? "#FF8F85" : "#B42318"
        readonly property color toastBackground: dark ? "#33444F" : "#14212B"
        readonly property color toastAccent: dark ? "#9FD3E8" : "#9FD3E8"
        readonly property color toastButton: dark ? "#3C4D5C" : "#2C3B47"
        readonly property color badgeBackground: dark ? "#F2F4F7" : "#14212B"
        readonly property color badgeText: dark ? "#14212B" : "#FFFFFF"
        readonly property color sheetScrim: dark ? "#05080B" : "#3A4650"
        readonly property color alertScrim: dark ? "#05080B" : "#2E3943"
        readonly property color surfaceRaised: dark ? "#212C37" : "#FFFFFF"
        readonly property color sheetBackground: dark ? "#141C24" : "#F2F4F7"
        readonly property color primaryText: dark ? "#8FC6E6" : "#1E5B7A"
        readonly property color destructiveFill: dark ? "#8C2A22" : "#B42318"
        readonly property color bannerWarning: dark ? "#2F2815" : "#FFF4D6"
        readonly property color bannerWarningBorder: dark ? "#D2A23A" : "#946B0C"
        readonly property color bannerError: dark ? "#3A1B18" : "#FDECEA"
        readonly property color bannerErrorBorder: dark ? "#FF8F85" : "#B42318"
        readonly property color reviewBackground: dark ? "#000000" : "#F2F4F7"
        readonly property color reviewSurface: dark ? "#000000" : "#FFFFFF"
        readonly property color reviewText: dark ? "#FFFFFF" : "#14212B"
        readonly property color reviewTextSecondary: dark ? "#B4C0CA" : "#5A6B78"
        readonly property color reviewDivider: dark ? "#2A2F34" : "#E8ECF0"
        readonly property color swatch: dark ? "#2B3844" : "#E5E9EE"
        readonly property color textOnPrimary: dark ? "#FFFFFF" : "#FFFFFF"
        readonly property color textOnHeaderCard: dark ? "#FFFFFF" : "#FFFFFF"
        readonly property color textOnDestructive: dark ? "#FFFFFF" : "#FFFFFF"
        readonly property color toastText: dark ? "#FFFFFF" : "#FFFFFF"

        // Other: a neutral tile with a dashed border (tokens.json, tile slot 12).
        readonly property color otherTileFill: dark ? "#18212A" : "#FFFFFF"
        readonly property color otherTileBorder: dark ? "#74869A" : "#9AA8B4"
        // Tile text is always textPrimary on the tile colour (tilePaletteRules).
        readonly property color tileText: textPrimary
    }

    readonly property Colour colour: Colour {
        dark: root.dark
    }

    // ---- sizes, radii, spacing ----

    // The design frame, an iPhone 17 in portrait, and its safe-area insets (assumption U1).
    component Frame: QtObject {
        readonly property real width: 402
        readonly property real height: 874
        readonly property real safeAreaTop: 59
        readonly property real homeIndicator: 34
    }

    readonly property Frame frame: Frame {}

    component Size: QtObject {
        readonly property real tileHeight: 104
        readonly property real tileWidth: 118
        readonly property real tileMinimum: 88
        readonly property int boardColumns: 3
        readonly property int boardRows: 4
        readonly property real minTouchTarget: 44
        readonly property real primaryButtonHeight: 54
        readonly property real fieldHeight: 44
        readonly property real chipHeight: 32
        readonly property real badge: 24
        readonly property real toastHeight: 52
        readonly property real tabBarHeight: 83
        readonly property real listRowHeight: 48
        // The named row heights of the design: settings rows (44), the Other picker's rows (46), the rows of a form such
        // as Edit entry and the import errors (52), an action row such as "Start a new log for another unit or site"
        // (56) and a History row (60). A ListRow or SwipeRow takes one as its `minimumHeight`.
        readonly property real rowHeightCompact: 44
        readonly property real rowHeightPicker: 46
        readonly property real rowHeightForm: 52
        readonly property real rowHeightAction: 56
        readonly property real rowHeightTall: 60
        // A field in its compact form (FirstLaunchManual) and a small button (48); the buttons of an alert (50) and its
        // plain cancel button (46).
        readonly property real compactFieldHeight: 48
        readonly property real smallButtonHeight: 48
        readonly property real alertButtonHeight: 50
        readonly property real alertCancelHeight: 46
        // The top of a large sheet on the design frame (54, five points above the 59 pt safe area). On a phone with
        // another inset the sheet keeps the same distance from the inset.
        readonly property real sheetTop: 54
        // The least height of the header card of the Log screen.
        readonly property real headerCardHeight: 64
        // The top of an alert card on the design frame: the mock-ups pin the card near here instead of centring it.
        // Sheet clamps it so that a tall card on a small screen stays inside the safe area.
        readonly property real alertTop: 236
        // The range of the Dynamic Type multiplier, xSmall to accessibility 5.
        readonly property real textScaleMin: 0.82
        readonly property real textScaleMax: 3.1
    }

    readonly property Size size: Size {}

    component Radius: QtObject {
        readonly property real headerCard: 20
        readonly property real card: 16
        readonly property real tile: 16
        readonly property real groupedList: 14
        readonly property real field: 12
        readonly property real chip: 16
        readonly property real toast: 14
        readonly property real sheetTop: 16
        readonly property real alert: 22
        readonly property real primaryButton: 14
        // The time chip of Edit entry and the version sheet.
        readonly property real timeChip: 8
        // The search field, the tumbler's selection band and the toast's Undo button.
        readonly property real small: 10
    }

    readonly property Radius radius: Radius {}

    component Space: QtObject {
        readonly property real screenMargin: 16
        readonly property real sectionGap: 12
        readonly property real tileGap: 8
        readonly property real rowPadding: 14
    }

    readonly property Space space: Space {}

    // The miniature board of the settings screen (docs/design/screens/BoardSettings.dc.html and BoardLayoutSelected): a
    // tile, the gap between tiles, the label size in points, the scale of a selected tile's lift and how far it rises,
    // the selected ring (3 pt, 1 pt outside the face) and the tick (a 20 pt disc 6 pt in from the corner).
    component MiniBoard: QtObject {
        readonly property real tile: 64
        readonly property real gap: 6
        readonly property real labelSize: 11.5
        readonly property real selectedScale: 1.04
        readonly property real lift: 4
        readonly property real ringWidth: 3
        readonly property real ringGap: 1
        readonly property real tick: 20
        readonly property real tickInset: 6
        readonly property real labelInset: 8
    }

    readonly property MiniBoard miniBoard: MiniBoard {}

    // Line widths: the dashed border of the Other tile, and a hairline for dividers and outlines.
    component Border: QtObject {
        readonly property real otherWidth: 1.5
        readonly property real hairline: 1
    }

    readonly property Border border: Border {}

    // ---- shadows ----

    // The shadows of the design, as CSS draws them: a vertical offset, a blur radius and a colour with its opacity, which
    // differ by scheme for the card, the toast and the lifted tile. components/private/Shadow.qml draws one.
    //   card    the tiles, the note card and the "Last" bar: 0 1 2, so faint that Shadow draws it without an effect
    //   toast   the undo toast and the floating banners: 0 8 24
    //   alert   the alert card: 0 20 48
    //   sheet   a bottom sheet, cast upwards: 0 -4 24
    //   lift    the selected tile of the miniature board: 0 10 20
    component ShadowToken: QtObject {
        required property real offsetY
        required property real blur
        required property color colour
    }

    component Shadows: QtObject {
        id: shadows

        required property bool dark
        readonly property ShadowToken card: ShadowToken {
            offsetY: 1
            blur: 2
            colour: shadows.dark ? Qt.alpha("#000000", 0.5) : Qt.alpha("#14212B", 0.06)
        }
        readonly property ShadowToken toast: ShadowToken {
            offsetY: 8
            blur: 24
            colour: shadows.dark ? Qt.alpha("#000000", 0.5) : Qt.alpha("#14212B", 0.25)
        }
        readonly property ShadowToken alert: ShadowToken {
            offsetY: 20
            blur: 48
            colour: Qt.alpha("#000000", 0.3)
        }
        readonly property ShadowToken sheet: ShadowToken {
            offsetY: -4
            blur: 24
            colour: Qt.alpha("#000000", 0.25)
        }
        readonly property ShadowToken lift: ShadowToken {
            offsetY: 10
            blur: 20
            colour: Qt.alpha("#000000", 0.3)
        }
    }

    readonly property Shadows shadow: Shadows {
        dark: root.dark
    }

    // ---- type ----

    // The logical dots per inch that Qt turns a point size into pixels with: 72 on iOS, where a point is a pixel, and 96
    // on a desktop. A fractional size is set as a point size, so the font is made at exactly the size of the role. It is
    // measured, the same font at 1000 px and at 750 pt (750 pt is 1000 px at 96 dpi), because the screen objects of
    // Qt.application are not something qmllint can read.
    readonly property real fontDpi: pixelProbe.height > 0 ? 96 * pointProbe.height / pixelProbe.height : 72

    readonly property FontMetrics pixelProbe: FontMetrics {
        font: Qt.font({
            "pixelSize": 1000,
            "hintingPreference": Font.PreferNoHinting
        })
    }
    readonly property FontMetrics pointProbe: FontMetrics {
        font: Qt.font({
            "pointSize": 750,
            "hintingPreference": Font.PreferNoHinting
        })
    }

    // One type role. `size` is in points (a logical pixel on the phone) and already multiplied by the role's scale;
    // `font` is ready to assign to a Text, `selectedFont` is the same in `selectedWeight` (a selected tab, the
    // emphasised form of the role) and `linkFont` is the selected form underlined. Each role sets its own scale from the textScale it is given, because an inline
    // component cannot see the ids of the file around it.
    //
    // The size stays fractional (13.5 pt is 13.5, not 14): a whole size is set as a pixel size and a fractional one as a
    // point size that comes to the same pixels. Read `size`, not `font.pixelSize`, for the size of a role.
    //
    // A role with a `lineHeight` other than 1 has it as CSS does, a multiple of the size: a Text sets
    // `lineHeightMode: Text.FixedHeight` and `lineHeight: role.linePitch`. `linePitch` is the natural pitch of the font
    // for a role without a line height, so a component can set both for any role.
    component FontRole: QtObject {
        id: role

        required property real basePt
        required property int weight
        required property real textScale
        required property real dpi
        property real letterSpacing: 0
        // The letter spacing above the default text size (the tile label tightens at its larger sizes).
        property real largeLetterSpacing: letterSpacing
        // The cap on the multiplier for chrome whose geometry is fixed by design; 0 = no cap.
        property real scaleCap: 0
        property real lineHeight: 1.0
        property int maxLines: 0
        property int selectedWeight: weight
        property bool upperCase: false
        // Tabular figures (OpenType "tnum"), so the digits of a time keep their place as they change.
        property bool tabularNumbers: false
        // A family, or a comma-separated list of them in order of preference; empty = the system font.
        property string family: ""

        readonly property real scale: scaleCap > 0 ? Math.min(textScale, scaleCap) : textScale
        readonly property real size: basePt * scale
        // Above the default text size.
        readonly property bool enlarged: scale > 1
        readonly property real effectiveLetterSpacing: enlarged ? largeLetterSpacing : letterSpacing
        readonly property real linePitch: lineHeight !== 1.0 ? size * lineHeight : metrics.height
        // The same pitch as a multiple of the font's own line, for Text.ProportionalHeight. A text that shrinks to fit
        // (fontSizeMode) uses it: the ratio shrinks with the text, and Fit allows for it, where a fixed pitch is not.
        readonly property real lineRatio: metrics.height > 0 ? linePitch / metrics.height : 1
        readonly property font font: role.make(weight, false)
        readonly property font selectedFont: role.make(selectedWeight, false)
        // The selected weight, underlined: a text link (a banner's action).
        readonly property font linkFont: role.make(selectedWeight, true)

        // Qt.font() takes a whole point size only, so a fractional size is set on the font afterwards. Hinting is off, as
        // on iOS, so that a fractional size keeps its own advances on a desktop instead of those of the nearest pixel.
        function make(fontWeight: int, underline: bool): font {
            const spec = {
                "weight": fontWeight,
                "letterSpacing": effectiveLetterSpacing,
                "underline": underline,
                "hintingPreference": Font.PreferNoHinting
            };
            const whole = Math.abs(size - Math.round(size)) < 0.001;
            if (whole)
                spec.pixelSize = Math.round(size);
            if (family.length > 0)
                spec.family = family;
            if (tabularNumbers)
                spec.features = {
                    "tnum": 1
                };
            const made = Qt.font(spec);
            if (!whole)
                made.pointSize = size * 72 / dpi;
            return made;
        }

        readonly property FontMetrics metrics: FontMetrics {
            font: role.font
        }
    }

    component FontSet: QtObject {
        id: set

        required property real textScale
        required property real dpi
        readonly property FontRole largeTitle: FontRole {
            basePt: 30
            weight: 700
            letterSpacing: -0.4
            textScale: set.textScale
            dpi: set.dpi
        }
        readonly property FontRole headerUnit: FontRole {
            basePt: 20
            weight: 700
            letterSpacing: -0.2
            scaleCap: 1.5
            textScale: set.textScale
            dpi: set.dpi
        }
        readonly property FontRole sheetTitle: FontRole {
            basePt: 17
            weight: 600
            textScale: set.textScale
            dpi: set.dpi
        }
        // The actions of a navigation bar: "Cancel" and the back label in 400, the action that finishes a page in 600.
        readonly property FontRole navAction: FontRole {
            basePt: 17
            weight: 400
            selectedWeight: 600
            textScale: set.textScale
            dpi: set.dpi
        }
        readonly property FontRole body: FontRole {
            basePt: 16
            weight: 400
            selectedWeight: 600
            textScale: set.textScale
            dpi: set.dpi
        }
        readonly property FontRole bodySmall: FontRole {
            basePt: 15
            weight: 400
            selectedWeight: 600
            textScale: set.textScale
            dpi: set.dpi
        }
        // The text of a SwipeRow (the Review timeline, History).
        readonly property FontRole rowText: FontRole {
            basePt: 15
            weight: 400
            selectedWeight: 600
            lineHeight: 1.35
            textScale: set.textScale
            dpi: set.dpi
        }
        // The value in a field: the Header unit and version (fieldValueLarge), the "To" field of the version sheet
        // (fieldValue) and a plain form field (fieldText).
        readonly property FontRole fieldValueLarge: FontRole {
            basePt: 20
            weight: 600
            textScale: set.textScale
            dpi: set.dpi
        }
        readonly property FontRole fieldValue: FontRole {
            basePt: 18
            weight: 600
            textScale: set.textScale
            dpi: set.dpi
        }
        readonly property FontRole fieldText: FontRole {
            basePt: 17
            weight: 400
            textScale: set.textScale
            dpi: set.dpi
        }
        // The header block of Review.
        readonly property FontRole headerBlock: FontRole {
            basePt: 14
            weight: 400
            lineHeight: 1.55
            textScale: set.textScale
            dpi: set.dpi
        }
        // The preview of a line as the log will show it (Edit entry, the template value sheet).
        readonly property FontRole previewText: FontRole {
            basePt: 14
            weight: 400
            lineHeight: 1.45
            textScale: set.textScale
            dpi: set.dpi
        }
        // The message of a banner, the toast's line and the line of the "Last" bar.
        readonly property FontRole messageText: FontRole {
            basePt: 14
            weight: 400
            lineHeight: 1.4
            textScale: set.textScale
            dpi: set.dpi
        }
        // The explanation of a choice card.
        readonly property FontRole description: FontRole {
            basePt: 14
            weight: 400
            lineHeight: 1.35
            textScale: set.textScale
            dpi: set.dpi
        }
        // The labels of a SwipeRow's actions.
        readonly property FontRole actionLabel: FontRole {
            basePt: 14
            weight: 600
            textScale: set.textScale
            dpi: set.dpi
        }
        // The title of a banner.
        readonly property FontRole bannerTitle: FontRole {
            basePt: 15
            weight: 600
            lineHeight: 1.3
            textScale: set.textScale
            dpi: set.dpi
        }
        // The message of an alert.
        readonly property FontRole alertMessage: FontRole {
            basePt: 15
            weight: 400
            lineHeight: 1.45
            textScale: set.textScale
            dpi: set.dpi
        }
        // The label of a compact button ("Add", the toast's "Undo").
        readonly property FontRole buttonCompact: FontRole {
            basePt: 15
            weight: 600
            textScale: set.textScale
            dpi: set.dpi
        }
        readonly property FontRole tileLabel: FontRole {
            basePt: 13.5
            weight: 600
            lineHeight: 1.2
            largeLetterSpacing: -0.2
            maxLines: 4
            scaleCap: 1.3
            textScale: set.textScale
            dpi: set.dpi
        }
        readonly property FontRole caption: FontRole {
            basePt: 13
            weight: 400
            selectedWeight: 600
            textScale: set.textScale
            dpi: set.dpi
        }
        // A note under a group or a page.
        readonly property FontRole footnote: FontRole {
            basePt: 13
            weight: 400
            lineHeight: 1.4
            textScale: set.textScale
            dpi: set.dpi
        }
        // The hint or the refusal under a field.
        readonly property FontRole fieldMessage: FontRole {
            basePt: 13
            weight: 400
            lineHeight: 1.35
            textScale: set.textScale
            dpi: set.dpi
        }
        // The name on a template chip.
        readonly property FontRole chip: FontRole {
            basePt: 13
            weight: 500
            textScale: set.textScale
            dpi: set.dpi
        }
        // A log line as the export writes it, in a monospaced face: Menlo (ui-monospace) on the phone, Cascadia Mono or
        // Consolas on Windows. The list is one family string; Qt takes the first one it has.
        readonly property FontRole monospace: FontRole {
            basePt: 13
            weight: 400
            family: "Menlo, Cascadia Mono, Consolas, monospace"
            textScale: set.textScale
            dpi: set.dpi
        }
        // The right column of the header card: the date in 600, the facilitator in 400.
        readonly property FontRole captionSmall: FontRole {
            basePt: 12
            weight: 400
            selectedWeight: 600
            textScale: set.textScale
            dpi: set.dpi
        }
        readonly property FontRole sectionLabel: FontRole {
            basePt: 12
            weight: 600
            letterSpacing: 0.5
            upperCase: true
            textScale: set.textScale
            dpi: set.dpi
        }
        readonly property FontRole tabLabel: FontRole {
            basePt: 10.5
            weight: 500
            selectedWeight: 600
            scaleCap: 1.3
            textScale: set.textScale
            dpi: set.dpi
        }
        // The count badge of a tile: it keeps its 24 pt pill, so the number does not grow past the default size.
        readonly property FontRole badge: FontRole {
            basePt: 13
            weight: 700
            scaleCap: 1.0
            textScale: set.textScale
            dpi: set.dpi
        }
        readonly property FontRole timeColumn: FontRole {
            basePt: 15
            weight: 400
            tabularNumbers: true
            textScale: set.textScale
            dpi: set.dpi
        }
        // The time on a time chip.
        readonly property FontRole timeChip: FontRole {
            basePt: 16
            weight: 500
            tabularNumbers: true
            textScale: set.textScale
            dpi: set.dpi
        }
        // The rows of a time tumbler, and its selected row and colon.
        // Capped at 1.5, as the columns are a fixed width.
        readonly property FontRole tumbler: FontRole {
            basePt: 20
            weight: 400
            scaleCap: 1.5
            tabularNumbers: true
            textScale: set.textScale
            dpi: set.dpi
        }
        readonly property FontRole tumblerSelected: FontRole {
            basePt: 24
            weight: 600
            scaleCap: 1.5
            tabularNumbers: true
            textScale: set.textScale
            dpi: set.dpi
        }
    }

    readonly property FontSet font: FontSet {
        textScale: root.clampedScale
        dpi: root.fontDpi
    }

    // ---- motion ----

    // The values of tokens.json, "motion". Reduce Motion replaces every slide and spring by a fade of at most
    // reducedMotionMs; the tile press keeps its scale, because it is feedback and not decoration. The sheet, toast,
    // page and swipe values are starting points that package 4.14 tunes on a device. Animators, not property
    // animations, move things per frame (docs/plan/stage-4.md, Motion).
    component Motion: QtObject {
        required property bool reduceMotion
        readonly property real tilePressScale: 0.96
        readonly property int tilePressMs: 120
        readonly property int undoToastVisibleMs: 5000
        readonly property int reducedMotionMs: 100
        readonly property int sheetPresentMs: reduceMotion ? reducedMotionMs : 320
        readonly property int toastMs: reduceMotion ? reducedMotionMs : 240
        readonly property int pageMs: reduceMotion ? reducedMotionMs : 280
        readonly property int swipeReleaseMs: reduceMotion ? reducedMotionMs : 200
        // The easing of the tile press ("ease"), and of presentations: a spring-like overshoot, plain when reduced.
        readonly property int easingPress: Easing.InOutQuad
        readonly property int easingSpring: reduceMotion ? Easing.Linear : Easing.OutBack

        // How far a thing slides: the distance, or 0 when Reduce Motion is on, so that it fades in place.
        function travel(distance: real): real {
            return reduceMotion ? 0 : distance;
        }
    }

    readonly property Motion motion: Motion {
        reduceMotion: root.reduceMotion
    }

    // ---- contrast ----

    // Every pair of colours that a component puts one on the other, resolved for the current scheme, with the ratio
    // it has to reach: 4.5 for text and 3 for a non-text indicator (a border, the badge, the selected tab). The
    // contrast test walks this list in both schemes. An entry with an `exception` is a pair of an approved light value
    // that falls short: its `minimum` is the ratio it has today, so that it cannot get worse, and the note says what
    // would fix it.
    readonly property var contrastPairs: {
        const c = colour;
        const pairs = [];
        function text(label, foreground, background) {
            pairs.push({
                "label": label,
                "foreground": foreground,
                "background": background,
                "minimum": 4.5
            });
        }
        function indicator(label, foreground, background) {
            pairs.push({
                "label": label,
                "foreground": foreground,
                "background": background,
                "minimum": 3.0
            });
        }
        function exception(label, foreground, background, minimum, note) {
            pairs.push({
                "label": label,
                "foreground": foreground,
                "background": background,
                "minimum": minimum,
                "exception": note
            });
        }
        text("Title on the screen", c.textPrimary, c.background);
        text("Row text on a card", c.textPrimary, c.surface);
        text("Text on a raised card", c.textPrimary, c.surfaceRaised);
        text("Text on a sheet", c.textPrimary, c.sheetBackground);
        text("Text typed in a field", c.textPrimary, c.fieldBackground);
        text("Text in the search field", c.textPrimary, c.searchBackground);
        text("Text on the time chip", c.textPrimary, c.timeChipBackground);
        text("Caption on the screen", c.textSecondary, c.background);
        text("Row value on a card", c.textSecondary, c.surface);
        text("Caption on a raised card", c.textSecondary, c.surfaceRaised);
        text("Caption on a sheet", c.textSecondary, c.sheetBackground);
        text("Secondary text on the time chip", c.textSecondary, c.timeChipBackground);
        text("Body text on a card", c.textBody, c.surface);
        text("Link on the screen", c.primaryText, c.background);
        text("Link on a card", c.primaryText, c.surface);
        text("Link on a sheet", c.primaryText, c.sheetBackground);
        text("Secondary button label", c.primaryText, c.primaryTint);
        text("Primary button label", c.textOnPrimary, c.primary);
        text("Primary button label, pressed", c.textOnPrimary, c.primaryPressed);
        text("Header card unit and date", c.textOnHeaderCard, c.headerCard);
        text("Header card site, version and facilitator", c.headerCardTextSecondary, c.headerCard);
        text("Delete label on the screen", c.destructive, c.background);
        text("Delete label on a card", c.destructive, c.surface);
        text("Delete label on a sheet", c.destructive, c.sheetBackground);
        text("Delete swipe label", c.textOnDestructive, c.destructiveFill);
        text("Toast text", c.toastText, c.toastBackground);
        text("Toast tick", c.toastAccent, c.toastBackground);
        text("Undo button label", c.toastAccent, c.toastButton);
        text("Count badge number", c.badgeText, c.badgeBackground);
        text("Review lines", c.reviewText, c.reviewBackground);
        text("Review times and markers", c.reviewTextSecondary, c.reviewBackground);
        text("Review lines on a review card", c.reviewText, c.reviewSurface);
        text("Warning banner text", c.textPrimary, c.bannerWarning);
        text("Error banner text", c.textPrimary, c.bannerError);
        text("Information banner text", c.textPrimary, c.primaryTint);
        text("Other tile label", c.tileText, c.otherTileFill);
        for (let slot = 1; slot <= tileSlots; ++slot)
            text("Tile label, slot " + slot, c.tileText, tileColour(slot));
        indicator("Selected tab, check marks and the focus ring", c.primaryText, c.background);
        indicator("Selected tab on the tab bar", c.primaryText, c.surface);
        indicator("Primary button fill on the screen", c.primary, c.background);
        indicator("Warning icon and outline", c.bannerWarningBorder, c.bannerWarning);
        indicator("Error icon and outline", c.bannerErrorBorder, c.bannerError);
        indicator("Information icon and outline", c.primaryText, c.primaryTint);
        for (let slot = 1; slot <= tileSlots; ++slot)
            indicator("Count badge on tile, slot " + slot, c.badgeBackground, tileColour(slot));
        if (dark) {
            indicator("Other tile border on the screen", c.otherTileBorder, c.background);
            indicator("Other tile border on a card", c.otherTileBorder, c.surface);
            text("Placeholder in a field", c.textPlaceholder, c.fieldBackground);
            text("Placeholder on a card", c.textPlaceholder, c.surface);
        } else {
            // The approved light values fall short here (docs/design/contrast.md covers dark only). The label carries
            // the meaning of the Other tile and a placeholder is a hint, but both want a darker value.
            exception("Other tile border on the screen", c.otherTileBorder, c.background, 2.2, "the approved light border is about 2.2 to 1; 3 to 1 needs a darker border");
            exception("Other tile border on a card", c.otherTileBorder, c.surface, 2.4, "the approved light border is about 2.4 to 1; 3 to 1 needs a darker border");
            exception("Placeholder in a field", c.textPlaceholder, c.fieldBackground, 4.1, "the approved light placeholder is about 4.2 to 1; 4.5 to 1 needs a darker placeholder");
            exception("Placeholder on a card", c.textPlaceholder, c.surface, 4.4, "the approved light placeholder is about 4.4 to 1; 4.5 to 1 needs a darker placeholder");
        }
        return pairs;
    }
}
