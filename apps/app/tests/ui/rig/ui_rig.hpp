// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QPointF>
#include <QRectF>
#include <QSize>
#include <QString>
#include <QVariantMap>

#include <chrono>
#include <functional>
#include <memory>
#include <optional>
#include <vector>

// The Qt Quick classes are only declared here. The headers of the core define a name (`STRICT`) that the Windows headers
// behind Qt Quick define as a macro, so a test includes its own headers first and <QQuickItem> and the rest after them,
// which clang-format's include order gives; this header must not bring Qt Quick in ahead of them.
class QImage;
class QObject;
class QQmlEngine;
class QQuickItem;
class QQuickWindow;

namespace ej::test
{
class TestClock;
class ManualStorageGateway;
} // namespace ej::test

namespace ej::ui
{

/// One item of the item tree with what a test asks about it. Positions are in points of the window (the QML units).
struct Node
{
    QQuickItem* m_Item = nullptr;
    int m_Depth = 0;
    QString m_TypeName;       ///< the C++ class name, such as `QQuickRectangle` or `QQuickText`
    QString m_ObjectName;
    QRectF m_Rect;            ///< in the window
    bool m_Visible = false;   ///< visible, with an opacity above zero and a size
    bool m_Enabled = false;
    bool m_Interactive = false; ///< takes a press: a mouse area, a pointer handler, or a control
    bool m_InAccessibleTree = false; ///< a screen reader can reach it (not `Accessible.ignored`)
    int m_Role = 0;           ///< a QAccessible::Role; 0 is NoRole
    QString m_Name;           ///< `Accessible.name`, or the text of a `Text`
    QString m_Description;
    QString m_Value;          ///< the accessible value, such as a count or a position
};

/// The common fixture of the QML tests (docs/plan/stage-4.md, "Tests"): a window of an iPhone 17 offscreen, an engine that
/// has the module `ErrorJournalingApp`, and the means to load a component, drive it with touch and keys, advance time
/// without waiting, and read back the item tree with its accessibility data. One `UiRig` is one engine, so the `Theme`
/// singleton is a fresh one in each.
///
///     UiRig rig;
///     auto* tile = rig.load("UiStubGood", {{"title", "Sam Example"}});
///     rig.tap(rig.find("activate"));
///     rig.advance(300ms);
///
/// Time is manual. The animation clock is replaced by `ManualAnimationDriver`, so an animation of 120 ms is finished
/// after `advance(120ms)` and not before, and a test never sleeps. `attach` hands the rig the `TestClock` and the
/// `ManualStorageGateway` of Stage 3, so that `advance` moves them too and `settle` runs the jobs that are waiting.
class UiRig
{
public:
    explicit UiRig(QSize window = QSize(402, 874));
    ~UiRig();

    UiRig(const UiRig&) = delete;
    UiRig& operator=(const UiRig&) = delete;

    [[nodiscard]] QQmlEngine& engine();
    [[nodiscard]] QQuickWindow& window();
    [[nodiscard]] QQuickItem* content() const;

    /// The `Theme` singleton of this engine.
    [[nodiscard]] QObject* theme();
    /// `Qt.Light` or `Qt.Dark`.
    void set_dark(bool dark);
    void set_text_scale(double scale);
    void set_reduce_motion(bool reduce);

    /// Creates the QML type `type` of the module `ErrorJournalingApp` with `properties` (required ones included), makes
    /// it a child of the window's content item and sizes it to the window. Returns null and records the errors in
    /// `errors()` when it cannot. The item is owned by the rig. Each call replaces the previous one.
    QQuickItem* load(const QString& type, const QVariantMap& properties = {});
    /// The same for QML source text, for the few cases that need a one-off wrapper. `source` imports what it uses.
    QQuickItem* load_source(const QByteArray& source, const QVariantMap& properties = {});
    [[nodiscard]] QQuickItem* root() const;
    [[nodiscard]] const QString& errors() const;

    // ---- touch and keys ----

    /// A finger down at `at` in the item's coordinates (the middle by default), and up again. Several fingers may be
    /// down at once: each `press` returns the finger's id for `release`.
    int press(QQuickItem* item, std::optional<QPointF> at = std::nullopt);
    void move(int finger, QQuickItem* item, QPointF to);
    void release(int finger);
    void tap(QQuickItem* item, std::optional<QPointF> at = std::nullopt);
    /// A finger down at `from`, over `steps` moves to `to`, and up, all in the item's coordinates.
    void drag(QQuickItem* item, QPointF from, QPointF to, int steps = 8);
    void swipe(QQuickItem* item, QPointF from, QPointF to)
    {
        drag(item, from, to, 6);
    }
    /// Types characters into the focused item, as the keyboard does.
    void type(const QString& text);
    void key(Qt::Key key);

    // ---- time ----

    /// Moves the animation clock by `by`, in steps of one frame so that an animation sees each, then the `TestClock` and
    /// the events it leaves.
    void advance(std::chrono::milliseconds by);
    /// Runs the manual gateway until its queue is empty, then processes events. With nothing attached, only the events.
    void settle();
    void attach(test::TestClock& clock);
    void attach(test::ManualStorageGateway& gateway);

    // ---- looking at the result ----

    /// Every item under `from` (the loaded item by default), the item first, then its children in order.
    [[nodiscard]] std::vector<Node> tree(QQuickItem* from = nullptr);
    /// The item and the items that the accessibility tree lists under it, in the order a screen reader reads them.
    [[nodiscard]] std::vector<Node> accessible_tree(QQuickItem* from = nullptr);
    [[nodiscard]] QRectF geometry(QQuickItem* item) const;
    /// The first item under the root whose objectName is `name`; null when there is none.
    [[nodiscard]] QQuickItem* find(const QString& name, QQuickItem* from = nullptr) const;
    /// Lays the item tree out and paints one frame (software renderer), so that sizes that depend on polish are final.
    void layout();
    /// The rendered window, for the few tests that look at pixels.
    [[nodiscard]] QImage grab();

private:
    struct Impl;
    QQuickItem* adopt(QObject* created);
    Node describe(QQuickItem* item, int depth) const;
    void walk(QQuickItem* item, int depth, std::vector<Node>& out) const;

    std::unique_ptr<Impl> m_Impl;
};

/// True when `type` is `QQuickPointerHandler` or derives from it, by walking the meta objects, which keeps the private
/// Qt Quick headers out of the tests.
[[nodiscard]] bool is_pointer_handler(const QObject* object);

} // namespace ej::ui
