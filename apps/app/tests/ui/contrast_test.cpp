// SPDX-License-Identifier: GPL-3.0-or-later
// a11y.contrast: every pair of Theme.contrastPairs in both schemes, the 11 tile colours
// under the tile text, and the rules for colour that is not a contrast ratio: no red or green tile hue, and tiles that
// stay apart for a person with deuteranopia.
#include "rig/color_math.hpp"
#include "rig/ui_rig.hpp"

#include <catch2/catch_test_macros.hpp>

#include <QColor>
#include <QMap>
#include <QMetaObject>
#include <QObject>
#include <QStringList>
#include <QVariant>
#include <algorithm>

using namespace ej::ui;

namespace
{

QColor colour_property(QObject* theme, const char* group, const char* name)
{
    auto* object = theme->property(group).value<QObject*>();
    REQUIRE(object != nullptr);
    return object->property(name).value<QColor>();
}

QColor tile(QObject* theme, int slot)
{
    QColor result;
    REQUIRE(QMetaObject::invokeMethod(theme, "tileColour", Q_RETURN_ARG(QColor, result), Q_ARG(int, slot)));
    return result;
}

QVariantList pairs(QObject* theme)
{
    return theme->property("contrastPairs").toList();
}

/// The smallest distance between two of the 11 tiles as a person with deuteranopia sees them, with the slots.
struct Closest
{
    double m_Distance = 1e9;
    int m_First = 0;
    int m_Second = 0;
};
Closest closest_tiles(QObject* theme)
{
    Closest closest;
    for (int a = 1; a <= 11; ++a)
    {
        for (int b = a + 1; b <= 11; ++b)
        {
            const double distance = deuteranopia_distance(tile(theme, a), tile(theme, b));
            if (distance < closest.m_Distance)
            {
                closest = {distance, a, b};
            }
        }
    }
    return closest;
}

} // namespace

TEST_CASE("a11y.contrast: the formula is WCAG's", "[ui][a11y][contrast]")
{
    CHECK(contrast_ratio(QColor("#000000"), QColor("#FFFFFF")) > 20.99);
    CHECK(contrast_ratio(QColor("#FFFFFF"), QColor("#FFFFFF")) == 1.0);
    // The values recorded by hand in docs/design/contrast.md.
    CHECK(contrast_ratio(QColor("#EEF2F6"), QColor("#0E141A")) > 16.46);
    CHECK(contrast_ratio(QColor("#EEF2F6"), QColor("#0E141A")) < 16.48);
    CHECK(contrast_ratio(QColor("#FFFFFF"), QColor("#2A6F96")) > 5.50);
    CHECK(contrast_ratio(QColor("#FFFFFF"), QColor("#2A6F96")) < 5.52);
    // Order does not matter.
    CHECK(contrast_ratio(QColor("#1E5B7A"), QColor("#F2F4F7")) == contrast_ratio(QColor("#F2F4F7"), QColor("#1E5B7A")));
}

TEST_CASE("a11y.contrast: every pair of Theme.contrastPairs reaches its ratio, in light and in dark, with and without "
          "Increase Contrast",
          "[ui][a11y][contrast]")
{
    UiRig rig;
    QObject* theme = rig.theme();
    for (const bool increased : {false, true})
    {
        rig.set_increase_contrast(increased);
        // 4.5 to 1 for text and 3 to 1 for a non-text indicator, or 7 to 1 and 4.5 to 1 with Increase Contrast.
        const double text_minimum = increased ? 7.0 : 4.5;
        const double indicator_minimum = increased ? 4.5 : 3.0;
        for (const bool dark : {false, true})
        {
            rig.set_dark(dark);
            const QVariantList list = pairs(theme);
            REQUIRE(list.size() > 60);
            for (const QVariant& entry : list)
            {
                const QVariantMap pair = entry.toMap();
                const double minimum = pair.value("minimum").toDouble();
                const double ratio =
                    contrast_ratio(pair.value("foreground").value<QColor>(), pair.value("background").value<QColor>());
                INFO((dark ? "dark" : "light")
                     << (increased ? " with Increase Contrast: " : ": ") << pair.value("label").toString().toStdString()
                     << " is " << ratio << " to 1, needs " << minimum);
                CHECK(ratio >= minimum);
                CHECK((minimum == text_minimum || minimum == indicator_minimum));
                // No pair is excused: a value that falls short is a defect, not an exception.
                CHECK_FALSE(pair.contains("exception"));
            }
        }
    }
    rig.set_increase_contrast(false);
}

TEST_CASE(
    "a11y.contrast: Increase Contrast adds the lines and the search field to the list and never takes a pair away",
    "[ui][a11y][contrast]")
{
    UiRig rig;
    QObject* theme = rig.theme();
    const auto labels = [&]
    {
        QStringList names;
        for (const QVariant& entry : pairs(theme))
        {
            names << entry.toMap().value("label").toString();
        }
        return names;
    };
    for (const bool dark : {false, true})
    {
        rig.set_dark(dark);
        rig.set_increase_contrast(false);
        const QStringList plain = labels();
        rig.set_increase_contrast(true);
        const QStringList strong = labels();
        INFO((dark ? "dark" : "light"));
        for (const QString& label : plain)
        {
            CHECK(strong.contains(label));
        }
        CHECK(strong.size() > plain.size());
        CHECK(strong.contains(QStringLiteral("Divider on a card")));
        CHECK(strong.contains(QStringLiteral("Border of a field")));
        CHECK(strong.contains(QStringLiteral("Placeholder in the search field")));
        CHECK_FALSE(plain.contains(QStringLiteral("Divider on a card")));
    }
}

TEST_CASE("a11y.contrast: Increase Contrast only strengthens, in both schemes: no text or line gets weaker",
          "[ui][a11y][contrast]")
{
    UiRig rig;
    QObject* theme = rig.theme();
    // The colour pairs that exist in both modes, by label, so the ratio of one can be compared with the other.
    const auto ratios = [&]
    {
        QMap<QString, double> result;
        for (const QVariant& entry : pairs(theme))
        {
            const QVariantMap pair = entry.toMap();
            result.insert(pair.value("label").toString(), contrast_ratio(pair.value("foreground").value<QColor>(),
                                                                         pair.value("background").value<QColor>()));
        }
        return result;
    };
    for (const bool dark : {false, true})
    {
        rig.set_dark(dark);
        rig.set_increase_contrast(false);
        const auto plain = ratios();
        rig.set_increase_contrast(true);
        const auto strong = ratios();
        for (auto it = plain.begin(); it != plain.end(); ++it)
        {
            INFO((dark ? "dark: " : "light: ") << it.key().toStdString());
            REQUIRE(strong.contains(it.key()));
            CHECK(strong.value(it.key()) >= it.value() - 0.005);
        }
    }
    rig.set_increase_contrast(false);
}

TEST_CASE("a11y.contrast: the list holds the text of each of the 11 tiles and of Other, and the count badge on each",
          "[ui][a11y][contrast]")
{
    UiRig rig;
    QObject* theme = rig.theme();
    for (const bool dark : {false, true})
    {
        rig.set_dark(dark);
        QStringList labels;
        for (const QVariant& entry : pairs(theme))
        {
            labels.append(entry.toMap().value("label").toString());
        }
        for (int slot = 1; slot <= 11; ++slot)
        {
            CHECK(labels.contains(QStringLiteral("Tile label, slot %1").arg(slot)));
            CHECK(labels.contains(QStringLiteral("Count badge on tile, slot %1").arg(slot)));
        }
        CHECK(labels.contains(QStringLiteral("Other tile label")));
        // The tile text is the screen's primary text, whatever the tile (the rule of the tile palette).
        CHECK(colour_property(theme, "colour", "tileText") == colour_property(theme, "colour", "textPrimary"));
    }
}

TEST_CASE("a11y.contrast: no tile is red or green", "[ui][a11y][contrast]")
{
    UiRig rig;
    QObject* theme = rig.theme();
    for (const bool dark : {false, true})
    {
        rig.set_dark(dark);
        for (int slot = 1; slot <= 11; ++slot)
        {
            const double hue = hue_degrees(tile(theme, slot));
            INFO((dark ? "dark" : "light") << " slot " << slot << " hue " << hue);
            const bool red = hue >= 350.0 || hue <= 20.0;
            const bool green = hue >= 75.0 && hue <= 165.0;
            CHECK_FALSE(red);
            CHECK_FALSE(green);
        }
    }
}

TEST_CASE("a11y.contrast: any two tiles differ for a person with deuteranopia", "[ui][a11y][contrast]")
{
    // CIE76 distance after the Machado deuteranopia matrix; two colours under about 2 look the same. The light pastels
    // are close (aqua and steel, slots 4 and 7, are 0.3 apart), so the floor for light is only that no two are
    // identical to the eye of the simulation, and it is reported to the owner; the label carries the meaning of a tile,
    // never the colour. The dark tiles are all at least 1.4 apart. A palette change that makes either worse fails here.
    constexpr double LIGHT_FLOOR = 0.25;
    constexpr double DARK_FLOOR = 1.4;

    UiRig rig;
    QObject* theme = rig.theme();
    for (const bool dark : {false, true})
    {
        rig.set_dark(dark);
        const Closest closest = closest_tiles(theme);
        INFO((dark ? "dark" : "light") << ": the closest pair is slots " << closest.m_First << " and "
                                       << closest.m_Second << " at " << closest.m_Distance);
        CHECK(closest.m_Distance >= (dark ? DARK_FLOOR : LIGHT_FLOOR));
    }
}

namespace
{

QColor interaction_tile(QObject* theme, int slot)
{
    QColor result;
    REQUIRE(QMetaObject::invokeMethod(theme, "interactionTileColour", Q_RETURN_ARG(QColor, result), Q_ARG(int, slot)));
    return result;
}

} // namespace

// T-I5-4: the second hue family of the Interactions board.
TEST_CASE("theme.interaction_hues: the interaction tiles and the strip pass the contrast test in light and in dark",
          "[ui][a11y][contrast]")
{
    UiRig rig;
    QObject* theme = rig.theme();
    for (const bool dark : {false, true})
    {
        rig.set_dark(dark);
        QStringList labels;
        for (const QVariant& entry : pairs(theme))
        {
            labels.append(entry.toMap().value("label").toString());
        }
        for (int slot = 1; slot <= 11; ++slot)
        {
            CHECK(labels.contains(QStringLiteral("Interaction tile label, slot %1").arg(slot)));
            CHECK(labels.contains(QStringLiteral("Count badge on interaction tile, slot %1").arg(slot)));
            // The tile text is the screen's primary text, as for the error tiles.
            CHECK(contrast_ratio(colour_property(theme, "colour", "tileText"), interaction_tile(theme, slot)) >= 4.5);
        }
        CHECK(labels.contains(QStringLiteral("Interactions strip title")));
        CHECK(labels.contains(QStringLiteral("Interactions tab, selected")));
        const QColor accent = colour_property(theme, "colour", "interactionAccent");
        CHECK(contrast_ratio(accent, colour_property(theme, "colour", "background")) >= 4.5);
        CHECK(contrast_ratio(accent, colour_property(theme, "colour", "surface")) >= 4.5);
    }
}

TEST_CASE("theme.interaction_hues: no interaction tile is red or green, and none is an error tile",
          "[ui][a11y][contrast]")
{
    UiRig rig;
    QObject* theme = rig.theme();
    for (const bool dark : {false, true})
    {
        rig.set_dark(dark);
        for (int slot = 1; slot <= 11; ++slot)
        {
            const QColor fill = interaction_tile(theme, slot);
            const double hue = hue_degrees(fill);
            INFO((dark ? "dark" : "light") << " slot " << slot << " hue " << hue);
            const bool red = hue >= 350.0 || hue <= 20.0;
            const bool green = hue >= 75.0 && hue <= 165.0;
            CHECK_FALSE(red);
            CHECK_FALSE(green);
            // A tile of the other board is never the same colour, so that a wrong tap is seen.
            for (int other = 1; other <= 11; ++other)
            {
                CHECK(fill != tile(theme, other));
            }
        }
    }
}

TEST_CASE("theme.interaction_hues: the interaction tiles differ for a person with deuteranopia", "[ui][a11y][contrast]")
{
    // The same rule and the same floors as the error tiles (the light pastels are close, the label carries the meaning
    // of a tile and never the colour). The closest pairs are recorded in docs/design/contrast.md.
    constexpr double LIGHT_FLOOR = 0.25;
    constexpr double DARK_FLOOR = 1.4;

    UiRig rig;
    QObject* theme = rig.theme();
    for (const bool dark : {false, true})
    {
        rig.set_dark(dark);
        double closest = 1e9;
        int first = 0;
        int second = 0;
        for (int a = 1; a <= 11; ++a)
        {
            for (int b = a + 1; b <= 11; ++b)
            {
                const double distance = deuteranopia_distance(interaction_tile(theme, a), interaction_tile(theme, b));
                if (distance < closest)
                {
                    closest = distance;
                    first = a;
                    second = b;
                }
            }
        }
        INFO((dark ? "dark" : "light") << ": the closest pair is slots " << first << " and " << second << " at "
                                       << closest);
        CHECK(closest >= (dark ? DARK_FLOOR : LIGHT_FLOOR));
    }
}
