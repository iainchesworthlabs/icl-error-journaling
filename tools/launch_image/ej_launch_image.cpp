// SPDX-License-Identifier: GPL-3.0-or-later
//
// Draws the launch image (Stage 5, package 5.8): the Log screen's skeleton (qml/screens/launch/LaunchSkeleton.qml)
// rendered offscreen at the size of the design frame, 402 x 874 pt, at a device pixel ratio of 3 (1206 x 2622 px), in
// light and in dark. The two PNGs are the image of the launch screen in the asset catalogue (LaunchBoard.imageset).
//
//     ej_launch_image [--out <directory>]     write launch-light.png and launch-dark.png (default: the image set)
//     ej_launch_image --check [<directory>]   render again and compare with the PNGs there; exit 1 with the command to
//     run ej_launch_image --self-test             check that the comparison fails on a drift and not on a rounding
//
// The skeleton is shapes only, and every size in it comes from the design tokens and not from a font, so the pictures
// come out the same on every machine. The comparison is exact inside the tile rectangles (corners apart) and allows a
// difference of 2 in a channel elsewhere, which is the rounding of anti-aliased edges and corners between renderers.
#include <QByteArray>
#include <QColor>
#include <QCoreApplication>
#include <QDir>
#include <QGuiApplication>
#include <QImage>
#include <QObject>
#include <QPoint>
#include <QQmlComponent>
#include <QQmlEngine>
#include <QQuickItem>
#include <QQuickWindow>
#include <QRect>
#include <QRectF>
#include <QSGRendererInterface>
#include <QSize>
#include <QString>
#include <QStringList>
#include <QtGlobal>
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <memory>
#include <optional>
#include <vector>

namespace
{

constexpr auto MODULE = "ErrorJournalingApp";
constexpr auto TYPE = "LaunchSkeleton";
/// The design frame (Theme.frame) and the scale of the iPhone 17.
constexpr int FRAME_WIDTH = 402;
constexpr int FRAME_HEIGHT = 874;
constexpr int SCALE = 3;
/// What the comparison lets pass outside the tile rectangles: the anti-aliasing of an edge or a corner.
constexpr int CHANNEL_TOLERANCE = 2;
/// The radius of a tile in points (Theme.radius.tile). The corners of a tile are anti-aliased and are compared with the
/// tolerance; the rest of the rectangle exactly.
constexpr int TILE_CORNER = 16;
constexpr std::size_t TILES = 12;

struct Appearance
{
    const char* m_Name;
    bool m_Dark;
};

constexpr Appearance APPEARANCES[] = {{"light", false}, {"dark", true}};

[[nodiscard]] QString file_name(const Appearance& appearance)
{
    return QStringLiteral("launch-%1.png").arg(QLatin1String(appearance.m_Name));
}

/// One rendering: the opaque image, and the rectangles of the tiles in its pixels.
struct Picture
{
    QImage m_Image;
    std::vector<QRect> m_Tiles;
};

void collect(QQuickItem* item, const QString& name, std::vector<QQuickItem*>& out)
{
    if (item->objectName() == name)
    {
        out.push_back(item);
    }
    for (QQuickItem* child : item->childItems())
    {
        collect(child, name, out);
    }
}

/// Renders the skeleton in one appearance. The window is the size of the frame, the scale factor is the iPhone's, and
/// the software renderer draws it, so no graphics hardware is involved.
[[nodiscard]] std::optional<Picture> render(const Appearance& appearance)
{
    QQuickWindow::setGraphicsApi(QSGRendererInterface::Software);
    QQmlEngine engine;
    QQuickWindow window;
    window.resize(FRAME_WIDTH, FRAME_HEIGHT);
    window.show();

    auto* theme = engine.singletonInstance<QObject*>(MODULE, "Theme");
    if (theme == nullptr)
    {
        std::fprintf(stderr, "error: the module %s has no Theme\n", MODULE);
        return std::nullopt;
    }
    theme->setProperty("colourScheme",
                       static_cast<int>(appearance.m_Dark ? Qt::ColorScheme::Dark : Qt::ColorScheme::Light));

    QQmlComponent component(&engine, MODULE, TYPE);
    std::unique_ptr<QObject> created(component.create());
    auto* skeleton = qobject_cast<QQuickItem*>(created.get());
    if (skeleton == nullptr)
    {
        std::fprintf(stderr, "error: cannot create %s: %s\n", TYPE, qPrintable(component.errorString()));
        return std::nullopt;
    }
    skeleton->setParentItem(window.contentItem());
    skeleton->setSize(QSizeF(FRAME_WIDTH, FRAME_HEIGHT));

    // Two frames: the first lays the item tree out, the second draws what the layout settled.
    (void)window.grabWindow();
    const QImage grabbed = window.grabWindow();
    const QSize expected(FRAME_WIDTH * SCALE, FRAME_HEIGHT * SCALE);
    if (grabbed.size() != expected)
    {
        std::fprintf(stderr, "error: the picture is %dx%d px, expected %dx%d (is QT_SCALE_FACTOR set?)\n",
                     grabbed.width(), grabbed.height(), expected.width(), expected.height());
        return std::nullopt;
    }

    Picture picture;
    // An opaque picture: the launch screen has nothing behind it, and a PNG without an alpha channel says so.
    picture.m_Image = grabbed.convertToFormat(QImage::Format_RGB32);
    std::vector<QQuickItem*> tiles;
    collect(skeleton, QStringLiteral("tileBackground"), tiles);
    for (QQuickItem* tile : tiles)
    {
        const QRectF rect = tile->mapRectToScene(QRectF(0, 0, tile->width(), tile->height()));
        picture.m_Tiles.push_back(
            QRectF(rect.x() * SCALE, rect.y() * SCALE, rect.width() * SCALE, rect.height() * SCALE).toAlignedRect());
    }
    return picture;
}

/// Whether the pixel is inside a tile and not in one of its rounded corners, where the match is exact.
[[nodiscard]] bool inside_tile(const std::vector<QRect>& tiles, int x, int y)
{
    constexpr int CORNER = TILE_CORNER * SCALE;
    return std::ranges::any_of(tiles,
                               [&](const QRect& tile)
                               {
                                   if (!tile.contains(x, y))
                                   {
                                       return false;
                                   }
                                   const bool across = x >= tile.left() + CORNER && x <= tile.right() - CORNER;
                                   const bool down = y >= tile.top() + CORNER && y <= tile.bottom() - CORNER;
                                   return across || down;
                               });
}

/// The first place where `actual` and `expected` differ more than the comparison allows, as text; empty when they
/// agree.
[[nodiscard]] QString difference(const QImage& actual, const QImage& expected, const std::vector<QRect>& tiles)
{
    if (actual.size() != expected.size())
    {
        return QStringLiteral("the size is %1x%2 px, expected %3x%4 px")
            .arg(actual.width())
            .arg(actual.height())
            .arg(expected.width())
            .arg(expected.height());
    }
    // Every pixel that is too far off, summarised: how many, where, how far, and the first one. One look at the CI log
    // then says whether a corner differs in its anti-aliasing or a shape has moved.
    int count = 0;
    int worst_delta = 0;
    QRect box;
    QString first;
    for (int y = 0; y < actual.height(); ++y)
    {
        for (int x = 0; x < actual.width(); ++x)
        {
            const QRgb a = actual.pixel(x, y);
            const QRgb e = expected.pixel(x, y);
            if (a == e)
            {
                continue;
            }
            const int delta =
                std::max({std::abs(qRed(a) - qRed(e)), std::abs(qGreen(a) - qGreen(e)), std::abs(qBlue(a) - qBlue(e))});
            const bool exact = inside_tile(tiles, x, y);
            if (!exact && delta <= CHANNEL_TOLERANCE)
            {
                continue;
            }
            if (count == 0)
            {
                first = QStringLiteral("the first is pixel (%1, %2), %3 and expected %4%5")
                            .arg(x)
                            .arg(y)
                            .arg(QColor(a).name())
                            .arg(QColor(e).name())
                            .arg(exact ? QStringLiteral(" (inside a tile, where the match is exact)") : QString());
            }
            ++count;
            worst_delta = std::max(worst_delta, delta);
            box = box.isNull() ? QRect(x, y, 1, 1) : box.united(QRect(x, y, 1, 1));
        }
    }
    if (count > 0)
    {
        return QStringLiteral("%1 pixels differ, by up to %2 levels, within (%3, %4) to (%5, %6); %7")
            .arg(count)
            .arg(worst_delta)
            .arg(box.left())
            .arg(box.top())
            .arg(box.right())
            .arg(box.bottom())
            .arg(first);
    }
    return {};
}

int write_all(const QString& directory)
{
    if (!QDir().mkpath(directory))
    {
        std::fprintf(stderr, "error: cannot create %s\n", qPrintable(directory));
        return 1;
    }
    for (const Appearance& appearance : APPEARANCES)
    {
        const auto picture = render(appearance);
        if (!picture)
        {
            return 1;
        }
        const QString path = QDir(directory).filePath(file_name(appearance));
        if (!picture->m_Image.save(path, "PNG"))
        {
            std::fprintf(stderr, "error: cannot write %s\n", qPrintable(path));
            return 1;
        }
        std::printf("wrote %s\n", qPrintable(path));
    }
    return 0;
}

int check_all(const QString& directory)
{
    int status = 0;
    for (const Appearance& appearance : APPEARANCES)
    {
        const auto picture = render(appearance);
        if (!picture)
        {
            return 1;
        }
        const QString path = QDir(directory).filePath(file_name(appearance));
        const QImage committed(path);
        if (committed.isNull())
        {
            std::fprintf(stderr, "error: cannot read %s\n", qPrintable(path));
            status = 1;
            continue;
        }
        const QString problem =
            difference(picture->m_Image, committed.convertToFormat(QImage::Format_RGB32), picture->m_Tiles);
        if (problem.isEmpty())
        {
            std::printf("ok: %s\n", qPrintable(path));
        }
        else
        {
            std::fprintf(stderr, "error: %s is not what the skeleton draws now: %s\n", qPrintable(path),
                         qPrintable(problem));
            status = 1;
        }
    }
    if (status != 0)
    {
        std::fprintf(
            stderr,
            "The launch image is out of date. Run `ej_launch_image` (it writes the PNGs in\n%s)\n"
            "and commit them. Delete the app from the phone before judging the launch screen: iOS caches it.\n",
            EJ_LAUNCH_IMAGE_DIR);
    }
    return status;
}

/// The comparison must fail on a tile that moved and pass on a rounding of one level; a check that cannot fail is no
/// check.
int self_test()
{
    const auto picture = render(APPEARANCES[0]);
    if (!picture || picture->m_Tiles.size() != TILES)
    {
        std::fprintf(stderr, "self-test: expected the image of %zu tiles\n", TILES);
        return 1;
    }
    int status = 0;
    const auto expect = [&](bool ok, const char* what)
    {
        if (!ok)
        {
            std::fprintf(stderr, "self-test failed: %s\n", what);
            status = 1;
        }
    };
    const QImage& image = picture->m_Image;
    expect(difference(image, image, picture->m_Tiles).isEmpty(), "an image differs from itself");

    const QRect tile = picture->m_Tiles.front();
    QImage inside = image;
    inside.setPixel(tile.center(), inside.pixel(tile.center()) ^ 0x000001);
    expect(!difference(inside, image, picture->m_Tiles).isEmpty(), "a level off inside a tile passed");

    QImage background = image;
    background.setPixel(2, 2, background.pixel(2, 2) ^ 0x000001);
    expect(difference(background, image, picture->m_Tiles).isEmpty(), "a level off in the background failed");

    QImage darker = image;
    darker.setPixel(2, 2, qRgb(0, 0, 0));
    expect(!difference(darker, image, picture->m_Tiles).isEmpty(), "a black pixel in the background passed");

    QImage higher = image.copy(0, SCALE, image.width(), image.height() - SCALE);
    higher = higher.copy(0, 0, image.width(), image.height());
    expect(!difference(higher, image, picture->m_Tiles).isEmpty(), "an image a point higher passed");
    if (status == 0)
    {
        std::printf("self-test ok\n");
    }
    return status;
}

} // namespace

int main(int argc, char* argv[])
{
    // The renderer and the scale are part of what makes the picture; a developer's environment must not change them.
    qputenv("QT_QPA_PLATFORM", "offscreen");
    qputenv("QT_QUICK_BACKEND", "software");
    qputenv("QSG_RENDER_LOOP", "basic");
    qputenv("QT_SCALE_FACTOR", QByteArray::number(SCALE));
    qputenv("QT_ENABLE_HIGHDPI_SCALING", "1");

    const QGuiApplication application(argc, argv);
    const QStringList arguments = QCoreApplication::arguments().mid(1);

    QString directory = QString::fromUtf8(EJ_LAUNCH_IMAGE_DIR);
    bool check = false;
    bool test = false;
    for (qsizetype i = 0; i < arguments.size(); ++i)
    {
        const QString& argument = arguments.at(i);
        if (argument == QLatin1String("--check"))
        {
            check = true;
        }
        else if (argument == QLatin1String("--self-test"))
        {
            test = true;
        }
        else if (argument == QLatin1String("--out") && i + 1 < arguments.size())
        {
            directory = arguments.at(++i);
        }
        else if (check && !argument.startsWith(QLatin1String("--")))
        {
            directory = argument;
        }
        else
        {
            std::fprintf(stderr, "usage: ej_launch_image [--out <directory>] | --check [<directory>] | --self-test\n");
            return 2;
        }
    }
    if (test)
    {
        return self_test();
    }
    return check ? check_all(directory) : write_all(directory);
}
