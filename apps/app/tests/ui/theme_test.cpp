// SPDX-License-Identifier: GPL-3.0-or-later
// The Theme singleton against docs/design/tokens.json: theme.matches_tokens, theme.dark_resolves and the rules of the
// text scale, the tile colours and the motion values.
#include "rig/ui_rig.hpp"

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <QColor>
#include <QFile>
#include <QFont>
#include <QGuiApplication>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMetaMethod>
#include <QMetaProperty>
#include <QObject>
#include <QScreen>
#include <QSet>
#include <cmath>

using namespace ej::ui;
using Catch::Matchers::WithinAbs;

namespace
{

QJsonObject tokens()
{
    QFile file(QStringLiteral(EJ_TOKENS_JSON));
    REQUIRE(file.open(QIODevice::ReadOnly));
    QJsonParseError error;
    const QJsonDocument document = QJsonDocument::fromJson(file.readAll(), &error);
    INFO(error.errorString().toStdString());
    REQUIRE(document.isObject());
    return document.object();
}

QObject* child(QObject* parent, const char* name)
{
    const QVariant value = parent->property(name);
    REQUIRE(value.isValid());
    auto* object = value.value<QObject*>();
    REQUIRE(object != nullptr);
    return object;
}

/// The name of the Theme property for a key of tokens.json: the unit suffix `Pt` is not part of it.
QString property_name(QString key)
{
    if (key.endsWith(QStringLiteral("Pt")))
    {
        key.chop(2);
    }
    return key;
}

/// The properties a QML object declares itself (not `objectName`).
QSet<QString> own_properties(const QObject* object)
{
    QSet<QString> names;
    // A QML object's own properties belong to its generated meta object and to the ones it derives from, down to
    // QObject.
    for (const QMetaObject* meta = object->metaObject(); meta != nullptr && meta != &QObject::staticMetaObject;
         meta = meta->superClass())
    {
        for (int i = meta->propertyOffset(); i < meta->propertyCount(); ++i)
        {
            names.insert(QString::fromLatin1(meta->property(i).name()));
        }
    }
    names.remove(QStringLiteral("objectName"));
    return names;
}

double number(const QObject* object, const QString& name)
{
    const QVariant value = object->property(name.toLatin1().constData());
    INFO(name.toStdString());
    REQUIRE(value.isValid());
    return value.toDouble();
}

/// Every number of a group in tokens.json equals the same-named property of the Theme object.
void check_numbers(const QJsonObject& json, const QObject* group, const QSet<QString>& skip = {})
{
    for (auto it = json.begin(); it != json.end(); ++it)
    {
        if (!it.value().isDouble() || skip.contains(it.key()))
        {
            continue;
        }
        const QString name = property_name(it.key());
        INFO("token " << it.key().toStdString());
        REQUIRE(own_properties(group).contains(name));
        CHECK_THAT(number(group, name), WithinAbs(it.value().toDouble(), 1e-9));
    }
}

QColor colour_of(const QObject* group, const QString& name)
{
    return group->property(name.toLatin1().constData()).value<QColor>();
}

QColor tile_colour(QObject* theme, int slot)
{
    QColor result;
    REQUIRE(QMetaObject::invokeMethod(theme, "tileColour", Q_RETURN_ARG(QColor, result), Q_ARG(int, slot)));
    return result;
}

double scaled(QObject* theme, const QString& role)
{
    double result = 0;
    REQUIRE(QMetaObject::invokeMethod(theme, "scaled", Q_RETURN_ARG(double, result), Q_ARG(QString, role)));
    return result;
}

double travel(QObject* motion, double distance)
{
    double result = 0;
    REQUIRE(QMetaObject::invokeMethod(motion, "travel", Q_RETURN_ARG(double, result), Q_ARG(double, distance)));
    return result;
}

} // namespace

TEST_CASE("theme.matches_tokens: every value of tokens.json is in the singleton, and the singleton has nothing else",
          "[ui][theme]")
{
    const QJsonObject json = tokens();
    UiRig rig;
    QObject* theme = rig.theme();
    REQUIRE(theme != nullptr);

    SECTION("colours, light and dark, name for name")
    {
        const QJsonObject colours = json.value(QStringLiteral("colour")).toObject();
        QObject* palette = child(theme, "colour");
        for (const bool dark : {false, true})
        {
            rig.set_dark(dark);
            const QJsonObject expected =
                colours.value(dark ? QStringLiteral("dark") : QStringLiteral("light")).toObject();
            REQUIRE_FALSE(expected.isEmpty());
            for (auto it = expected.begin(); it != expected.end(); ++it)
            {
                INFO((dark ? "dark " : "light ") << it.key().toStdString());
                REQUIRE(own_properties(palette).contains(it.key()));
                CHECK(colour_of(palette, it.key()) == QColor(it.value().toString()));
            }
        }
        // The singleton has no colour the contract lacks, except the three derived from the Other tile and tile text.
        const QSet<QString> derived{QStringLiteral("otherTileFill"), QStringLiteral("otherTileBorder"),
                                    QStringLiteral("tileText"), QStringLiteral("dark")};
        QSet<QString> extra = own_properties(palette);
        for (const QString& key : colours.value(QStringLiteral("light")).toObject().keys())
        {
            extra.remove(key);
        }
        extra.subtract(derived);
        INFO("not in tokens.json: " << extra.values().join(QStringLiteral(", ")).toStdString());
        CHECK(extra.isEmpty());
        // Light and dark list the same colours.
        CHECK(colours.value(QStringLiteral("light")).toObject().keys() ==
              colours.value(QStringLiteral("dark")).toObject().keys());
    }

    SECTION("the tile palette, 11 slots and Other")
    {
        const QJsonArray palette = json.value(QStringLiteral("tilePalette")).toArray();
        REQUIRE(palette.size() == 12);
        CHECK(theme->property("tileSlots").toInt() == 11);
        for (const bool dark : {false, true})
        {
            rig.set_dark(dark);
            for (int i = 0; i < 11; ++i)
            {
                const QJsonObject slot = palette.at(i).toObject();
                REQUIRE(slot.value(QStringLiteral("slot")).toInt() == i + 1);
                INFO((dark ? "dark " : "light ") << slot.value(QStringLiteral("name")).toString().toStdString());
                CHECK(tile_colour(theme, i + 1) ==
                      QColor(slot.value(dark ? QStringLiteral("darkHex") : QStringLiteral("hex")).toString()));
            }
            const QJsonObject other = palette.at(11).toObject();
            QObject* colours = child(theme, "colour");
            CHECK(colour_of(colours, QStringLiteral("otherTileFill")) ==
                  QColor(other.value(dark ? QStringLiteral("darkHex") : QStringLiteral("hex")).toString()));
            CHECK(colour_of(colours, QStringLiteral("otherTileBorder")) ==
                  QColor(other.value(dark ? QStringLiteral("darkBorder") : QStringLiteral("border")).toString()));
            CHECK(tile_colour(theme, 0) == colour_of(colours, QStringLiteral("otherTileFill")));
            CHECK_THAT(number(child(theme, "border"), QStringLiteral("otherWidth")),
                       WithinAbs(other.value(QStringLiteral("borderWidthPt")).toDouble(), 1e-9));
        }
    }

    SECTION("sizes, radii, spacing and the frame")
    {
        check_numbers(json.value(QStringLiteral("size")).toObject(), child(theme, "size"));
        check_numbers(json.value(QStringLiteral("radius")).toObject(), child(theme, "radius"));
        check_numbers(json.value(QStringLiteral("spacing")).toObject(), child(theme, "space"));
        check_numbers(json.value(QStringLiteral("frame")).toObject(), child(theme, "frame"));
        check_numbers(json.value(QStringLiteral("miniBoard")).toObject(), child(theme, "miniBoard"));
        const QJsonObject scale =
            json.value(QStringLiteral("typography")).toObject().value(QStringLiteral("textScale")).toObject();
        CHECK_THAT(number(child(theme, "size"), QStringLiteral("textScaleMin")),
                   WithinAbs(scale.value(QStringLiteral("min")).toDouble(), 1e-9));
        CHECK_THAT(number(child(theme, "size"), QStringLiteral("textScaleMax")),
                   WithinAbs(scale.value(QStringLiteral("max")).toDouble(), 1e-9));
        CHECK_THAT(number(theme, QStringLiteral("textScale")),
                   WithinAbs(scale.value(QStringLiteral("default")).toDouble(), 1e-9));

        // Nothing in the singleton that the contract does not have.
        for (const auto& [group, key] : {std::pair{"size", "size"}, std::pair{"radius", "radius"},
                                         std::pair{"space", "spacing"}, std::pair{"miniBoard", "miniBoard"}})
        {
            QSet<QString> have;
            for (const QString& name : json.value(QString::fromLatin1(key)).toObject().keys())
            {
                have.insert(property_name(name));
            }
            QSet<QString> extra = own_properties(child(theme, group));
            extra.subtract(have);
            if (QString::fromLatin1(group) == QStringLiteral("size"))
            {
                extra.remove(QStringLiteral("textScaleMin"));
                extra.remove(QStringLiteral("textScaleMax"));
            }
            INFO(group << " has, and tokens.json lacks: " << extra.values().join(QStringLiteral(", ")).toStdString());
            CHECK(extra.isEmpty());
        }
    }

    SECTION("type roles")
    {
        const QJsonObject typography = json.value(QStringLiteral("typography")).toObject();
        QObject* fonts = child(theme, "font");
        QSet<QString> roles;
        for (auto it = typography.begin(); it != typography.end(); ++it)
        {
            if (!it.value().isObject() || it.key() == QStringLiteral("textScale"))
            {
                continue;
            }
            roles.insert(it.key());
            const QJsonObject json_role = it.value().toObject();
            INFO("role " << it.key().toStdString());
            REQUIRE(own_properties(fonts).contains(it.key()));
            QObject* role = child(fonts, it.key().toLatin1().constData());
            CHECK_THAT(number(role, QStringLiteral("basePt")),
                       WithinAbs(json_role.value(QStringLiteral("sizePt")).toDouble(), 1e-9));
            CHECK(static_cast<int>(number(role, QStringLiteral("weight"))) ==
                  json_role.value(QStringLiteral("weight")).toInt(400));
            CHECK_THAT(number(role, QStringLiteral("letterSpacing")),
                       WithinAbs(json_role.value(QStringLiteral("letterSpacingPt")).toDouble(0), 1e-9));
            CHECK_THAT(number(role, QStringLiteral("lineHeight")),
                       WithinAbs(json_role.value(QStringLiteral("lineHeight")).toDouble(1.0), 1e-9));
            CHECK(static_cast<int>(number(role, QStringLiteral("maxLines"))) ==
                  json_role.value(QStringLiteral("maxLines")).toInt(0));
            CHECK_THAT(number(role, QStringLiteral("scaleCap")),
                       WithinAbs(json_role.value(QStringLiteral("scaleCap")).toDouble(0), 1e-9));
            CHECK(static_cast<int>(number(role, QStringLiteral("selectedWeight"))) ==
                  json_role.value(QStringLiteral("selectedWeight"))
                      .toInt(json_role.value(QStringLiteral("weight")).toInt(400)));
            CHECK(role->property("upperCase").toBool() == json_role.value(QStringLiteral("uppercase")).toBool(false));
            CHECK(role->property("tabularNumbers").toBool() ==
                  (json_role.value(QStringLiteral("numerals")).toString() == QStringLiteral("tabular")));
            CHECK_THAT(number(role, QStringLiteral("largeLetterSpacing")),
                       WithinAbs(json_role.value(QStringLiteral("largeLetterSpacingPt"))
                                     .toDouble(json_role.value(QStringLiteral("letterSpacingPt")).toDouble(0)),
                                 1e-9));
            CHECK(role->property("family").toString() == json_role.value(QStringLiteral("family")).toString());
        }
        QSet<QString> declared = own_properties(fonts);
        declared.remove(QStringLiteral("textScale"));
        declared.remove(QStringLiteral("dpi"));
        CHECK(declared == roles);
    }

    SECTION("shadows, light and dark")
    {
        const QJsonObject shadows = json.value(QStringLiteral("shadow")).toObject();
        QObject* group = child(theme, "shadow");
        QSet<QString> names;
        for (const bool dark : {false, true})
        {
            rig.set_dark(dark);
            for (auto it = shadows.begin(); it != shadows.end(); ++it)
            {
                names.insert(it.key());
                const QJsonObject token = it.value().toObject();
                INFO((dark ? "dark " : "light ") << it.key().toStdString());
                REQUIRE(own_properties(group).contains(it.key()));
                QObject* shadow = child(group, it.key().toLatin1().constData());
                CHECK_THAT(number(shadow, QStringLiteral("offsetY")),
                           WithinAbs(token.value(QStringLiteral("offsetYPt")).toDouble(), 1e-9));
                CHECK_THAT(number(shadow, QStringLiteral("blur")),
                           WithinAbs(token.value(QStringLiteral("blurPt")).toDouble(), 1e-9));
                QColor expected(token.value(dark ? QStringLiteral("dark") : QStringLiteral("light")).toString());
                expected.setAlphaF(static_cast<float>(
                    token.value(dark ? QStringLiteral("darkOpacity") : QStringLiteral("lightOpacity")).toDouble()));
                const QColor actual = colour_of(shadow, QStringLiteral("colour"));
                CHECK(actual.rgb() == expected.rgb());
                CHECK_THAT(static_cast<double>(actual.alphaF()),
                           WithinAbs(static_cast<double>(expected.alphaF()), 0.01));
            }
        }
        QSet<QString> declared = own_properties(group);
        declared.remove(QStringLiteral("dark"));
        CHECK(declared == names);
    }

    SECTION("motion")
    {
        const QJsonObject motion = json.value(QStringLiteral("motion")).toObject();
        QObject* values = child(theme, "motion");
        const QJsonObject press = motion.value(QStringLiteral("tilePress")).toObject();
        CHECK_THAT(number(values, QStringLiteral("tilePressScale")),
                   WithinAbs(press.value(QStringLiteral("scale")).toDouble(), 1e-9));
        CHECK(static_cast<int>(number(values, QStringLiteral("tilePressMs"))) ==
              press.value(QStringLiteral("durationMs")).toInt());
        for (const char* key :
             {"undoToastVisibleMs", "reducedMotionMs", "sheetPresentMs", "toastMs", "pageMs", "swipeReleaseMs"})
        {
            INFO(key);
            CHECK(static_cast<int>(number(values, QString::fromLatin1(key))) ==
                  motion.value(QString::fromLatin1(key)).toInt());
        }
    }

    SECTION("the design frame is the iPhone 17 the safe-area numbers are for")
    {
        const QJsonObject frame = json.value(QStringLiteral("frame")).toObject();
        CHECK(frame.value(QStringLiteral("widthPt")).toInt() == 402);
        CHECK(frame.value(QStringLiteral("safeAreaTopPt")).toInt() == 59);
        CHECK(frame.value(QStringLiteral("homeIndicatorPt")).toInt() == 34);
    }
}

TEST_CASE("theme.dark_resolves: switching the scheme changes the colours, and switching back restores them",
          "[ui][theme]")
{
    UiRig rig;
    QObject* theme = rig.theme();
    QObject* colours = child(theme, "colour");
    const QSet<QString> names = [&]
    {
        QSet<QString> all = own_properties(colours);
        all.remove(QStringLiteral("dark"));
        return all;
    }();
    REQUIRE(names.size() > 40);

    QHash<QString, QColor> light;
    for (const QString& name : names)
    {
        light.insert(name, colour_of(colours, name));
    }
    rig.set_dark(true);
    QStringList unchanged;
    for (const QString& name : names)
    {
        if (colour_of(colours, name) == light.value(name))
        {
            unchanged.append(name);
        }
    }
    unchanged.sort();
    INFO("the same in both schemes: " << unchanged.join(QStringLiteral(", ")).toStdString());
    // The plum header card and the white text on fills are the same in both, by design; nothing else may be.
    const QStringList expected_same{QStringLiteral("headerCard"),        QStringLiteral("headerCardTextSecondary"),
                                    QStringLiteral("textOnDestructive"), QStringLiteral("textOnHeaderCard"),
                                    QStringLiteral("textOnPrimary"),     QStringLiteral("toastAccent"),
                                    QStringLiteral("toastText")};
    CHECK(unchanged == expected_same);
    CHECK(theme->property("dark").toBool());
    CHECK(tile_colour(theme, 1) != QColor(QStringLiteral("#DCE8F7")));

    rig.set_dark(false);
    for (const QString& name : names)
    {
        INFO(name.toStdString());
        CHECK(colour_of(colours, name) == light.value(name));
    }
    // An unknown scheme is light.
    theme->setProperty("colourScheme", static_cast<int>(Qt::ColorScheme::Unknown));
    CHECK_FALSE(theme->property("dark").toBool());
}

TEST_CASE("theme.tile_colour: slot 0 and a slot out of range are Other, never a type colour", "[ui][theme]")
{
    UiRig rig;
    QObject* theme = rig.theme();
    const QColor other = colour_of(child(theme, "colour"), QStringLiteral("otherTileFill"));
    for (const int slot : {0, -1, 12, 99})
    {
        INFO("slot " << slot);
        CHECK(tile_colour(theme, slot) == other);
    }
    CHECK(tile_colour(theme, 11) == QColor(QStringLiteral("#F5D9E0")));
    // All 11 are different from one another in each scheme.
    for (const bool dark : {false, true})
    {
        rig.set_dark(dark);
        QSet<QRgb> seen;
        for (int slot = 1; slot <= 11; ++slot)
        {
            seen.insert(tile_colour(theme, slot).rgb());
        }
        CHECK(seen.size() == 11);
    }
}

TEST_CASE("theme.text_scale: Dynamic Type scales list and note text freely and caps the chrome", "[ui][theme]")
{
    UiRig rig;
    QObject* theme = rig.theme();
    QObject* fonts = child(theme, "font");
    const auto size_of = [&](const char* role) { return number(child(fonts, role), QStringLiteral("size")); };

    SECTION("at 1.0 a role is its token size")
    {
        CHECK_THAT(size_of("body"), WithinAbs(16, 1e-9));
        CHECK_THAT(size_of("tileLabel"), WithinAbs(13.5, 1e-9));
        CHECK_THAT(size_of("largeTitle"), WithinAbs(30, 1e-9));
    }

    SECTION("at 1.3 nothing is capped yet, and the tile label shows the 17.5 pt of the design")
    {
        rig.set_text_scale(1.3);
        CHECK_THAT(size_of("body"), WithinAbs(16 * 1.3, 1e-9));
        CHECK_THAT(size_of("tileLabel"), WithinAbs(13.5 * 1.3, 1e-9));
        CHECK_THAT(size_of("tileLabel"), WithinAbs(17.55, 1e-9));
    }

    SECTION("at 3.1 list and note text follow, and the chrome stops at its cap")
    {
        rig.set_text_scale(3.1);
        CHECK_THAT(size_of("body"), WithinAbs(16 * 3.1, 1e-9));
        CHECK_THAT(size_of("bodySmall"), WithinAbs(15 * 3.1, 1e-9));
        CHECK_THAT(size_of("sheetTitle"), WithinAbs(17 * 3.1, 1e-9));
        CHECK_THAT(size_of("tileLabel"), WithinAbs(13.5 * 1.3, 1e-9));
        CHECK_THAT(size_of("badge"), WithinAbs(13, 1e-9));
        CHECK_THAT(size_of("tabLabel"), WithinAbs(10.5 * 1.3, 1e-9));
        CHECK_THAT(size_of("headerUnit"), WithinAbs(20 * 1.5, 1e-9));
        CHECK_THAT(scaled(theme, QStringLiteral("tileLabel")), WithinAbs(1.3, 1e-9));
        CHECK_THAT(scaled(theme, QStringLiteral("body")), WithinAbs(3.1, 1e-9));
        CHECK_THAT(scaled(theme, QStringLiteral("noSuchRole")), WithinAbs(3.1, 1e-9));
    }

    SECTION("a value outside the iOS range is clamped to it")
    {
        rig.set_text_scale(0.2);
        CHECK_THAT(size_of("body"), WithinAbs(16 * 0.82, 1e-9));
        rig.set_text_scale(10);
        CHECK_THAT(size_of("body"), WithinAbs(16 * 3.1, 1e-9));
    }

    SECTION("a font is the role's size, unrounded, a weight and a letter spacing, ready to assign to a Text")
    {
        // A whole size is a pixel size; a fractional one is a point size that comes to the same pixels at the dpi the
        // theme reads, so 13.5 pt is drawn at 13.5 and not 14 (which wrapped "Sequence Out of Order" differently).
        const double dpi = number(theme, QStringLiteral("fontDpi"));
        // The theme measures the dpi that Qt makes point sizes with; it is the primary screen's.
        const QScreen* screen = QGuiApplication::primaryScreen();
        REQUIRE(screen != nullptr);
        CHECK_THAT(dpi, WithinAbs(screen->logicalDotsPerInchY(), 0.5));
        const auto pixels = [&](const QFont& font)
        { return font.pixelSize() > 0 ? font.pixelSize() : font.pointSizeF() * dpi / 72.0; };
        CHECK_THAT(pixels(child(fonts, "tileLabel")->property("font").value<QFont>()), WithinAbs(13.5, 1e-3));
        CHECK_THAT(pixels(child(fonts, "tabLabel")->property("font").value<QFont>()), WithinAbs(10.5, 1e-3));
        CHECK(child(fonts, "body")->property("font").value<QFont>().pixelSize() == 16);
        rig.set_text_scale(1.3);
        const QFont font = child(fonts, "tileLabel")->property("font").value<QFont>();
        CHECK_THAT(pixels(font), WithinAbs(17.55, 1e-3));
        CHECK(font.weight() == QFont::DemiBold);
        // The tile label tightens above the default size.
        CHECK_THAT(font.letterSpacing(), WithinAbs(-0.2, 0.02)); // QFont keeps it in 1/64 pt steps
        const QFont selected = child(fonts, "tabLabel")->property("selectedFont").value<QFont>();
        CHECK(selected.weight() == QFont::DemiBold);
        CHECK(child(fonts, "tabLabel")->property("font").value<QFont>().weight() == QFont::Medium);
        // The emphasised form of the text roles is 600, not the role's own 400.
        for (const char* name : {"body", "bodySmall", "caption", "captionSmall", "navAction"})
        {
            INFO(name);
            CHECK(child(fonts, name)->property("selectedFont").value<QFont>().weight() == QFont::DemiBold);
            CHECK(child(fonts, name)->property("font").value<QFont>().weight() == QFont::Normal);
        }
        // A role with a line height has its pitch as CSS does, a multiple of the size.
        CHECK_THAT(number(child(fonts, "tileLabel"), QStringLiteral("linePitch")), WithinAbs(17.55 * 1.2, 1e-6));
        // Tabular figures are set on the font, not only declared.
        CHECK(child(fonts, "timeColumn")->property("font").value<QFont>().featureTags().contains(QFont::Tag("tnum")));
    }
}

TEST_CASE("theme.motion: Reduce Motion makes every presentation a fade of at most 100 ms and keeps the tile press",
          "[ui][theme]")
{
    UiRig rig;
    QObject* theme = rig.theme();
    QObject* motion = child(theme, "motion");

    const QStringList presentations{QStringLiteral("sheetPresentMs"), QStringLiteral("toastMs"),
                                    QStringLiteral("pageMs"), QStringLiteral("swipeReleaseMs")};
    for (const QString& name : presentations)
    {
        INFO(name.toStdString());
        CHECK(number(motion, name) > 100);
    }
    CHECK(travel(motion, 200) == 200);

    rig.set_reduce_motion(true);
    for (const QString& name : presentations)
    {
        INFO(name.toStdString());
        CHECK(number(motion, name) <= 100);
    }
    CHECK(travel(motion, 200) == 0);
    // The press is feedback and keeps its scale and its 120 ms.
    CHECK_THAT(number(motion, QStringLiteral("tilePressScale")), WithinAbs(0.96, 1e-9));
    CHECK(number(motion, QStringLiteral("tilePressMs")) == 120);
}
