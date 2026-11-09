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

// The Qt Quick classes are only declared here. The headers of the core define a name (`STRICT`) that the Windows
// headers behind Qt Quick define as a macro, so a test includes its own headers first and <QQuickItem> and the rest
// after them, which clang-format's include order gives; this header must not bring Qt Quick in ahead of them.
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
    QString m_TypeName; ///< the C++ class name, such as `QQuickRectangle` or `QQuickText`
    QString m_ObjectName;
    QRectF m_Rect;          ///< in the window
    bool m_Visible = false; ///< visible, with an opacity above zero and a size
    bool m_Enabled = false;
    bool m_Interactive = false;      ///< takes a press: a mouse area, a pointer handler, or a control
    bool m_InAccessibleTree = false; ///< a screen reader can reach it (not `Accessible.ignored`)
    int m_Role = 0;                  ///< a QAccessible::Role; 0 is NoRole
    QString m_Name;                  ///< `Accessible.name`, or the text of a `Text`
    QString m_Description;
    QString m_Value; ///< the accessible value, such as a count or a position
};

/// The common fixture of the QML tests (docs/plan/stage-4.md, "Tests"): a window of an iPhone 17 offscreen, an engine
/// that has the module `ErrorJournalingApp`, and the means to load a component, drive it with touch and keys, advance
/// time without waiting, and read back the item tree with its accessibility data. One `UiRig` is one engine, so the
/// `Theme` singleton is a fresh one in each.
///
///     UiRig rig;
///     auto* tile = rig.load("UiStubGood", {{"title", "Sam Example"}});
///     rig.tap(rig.find("activate"));
///     rig.advance(300ms);
///
/// Time is manual. The animation clock is replaced by `ManualAnimationDriver`, so an animation of 120 ms is finished
/// after `advance(120ms)` and not before, and a test never sleeps. `attach` hands the rig the `TestClock` and the
/// `ManualStorageGateway` of Stage 3, so that `advance` moves them too and `settle` runs the jobs that are waiting.
///
/// What runs on which clock. Property animations and animators (`ScaleAnimator`, `XAnimator`, `OpacityAnimator`) move
/// with `advance`, because Qt drives them from the animation clock that `ManualAnimationDriver` replaces; an animator
/// writes its value back to the item when it ends, so look at an animator's result after its whole duration. The
/// services of Stage 3 run on the `TestClock`, which `advance` moves when it is attached (`attach`, or `use` in
/// `rig/services.hpp`). A QML `Timer` and a `QTimer` run on the wall clock and `advance` does not move them: the UI has
/// none, because the plan keeps all time arithmetic in C++ of Stage 3, and a test that must wait for one calls
/// `wait_real`, which does wait.
class UiRig
{
public:
    explicit UiRig(QSize window_size = QSize(402, 874));
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
    ///
    /// A press renders frames until the item has stopped moving, then aims at it, and delivers the press without
    /// turning the event loop. The window asks for its frames with a timer on the wall clock, so on a busy machine a
    /// frame can run in the middle of any call that processes events, polish what the test just changed (a message
    /// under a field makes a sheet taller) and move the item from under the finger; a test must not depend on when.
    int press(QQuickItem* item, std::optional<QPointF> at = std::nullopt);
    void move(int finger, QQuickItem* item, QPointF to);
    void release(int finger);
    void tap(QQuickItem* item, std::optional<QPointF> at = std::nullopt);
    /// A finger down on the item, kept still while `duration` of virtual time passes (`advance`: the animation clock and
    /// the attached clock, a frame at a time), and up again: a long press. Nothing is run on the gateway in between, so a
    /// commit that was asked for at touch-down is still waiting when the hold ends, as a slow one is. Use `press`,
    /// `advance` and `release` when the test must look in the middle.
    void hold(QQuickItem* item, std::chrono::milliseconds duration, std::optional<QPointF> at = std::nullopt);
    /// A finger down at `from`, over `steps` moves to `to`, and up, all in the item's coordinates. Each update is
    /// delivered before `move` returns: the test main turns off the compression of touch updates, which would deliver
    /// only the last.
    void drag(QQuickItem* item, QPointF from, QPointF to, int steps = 8);
    void swipe(QQuickItem* item, QPointF from, QPointF to)
    {
        drag(item, from, to, 6);
    }
    /// Types characters into the focused item, as the keyboard does.
    void type(const QString& text);
    void key(Qt::Key key);

    // ---- time ----

    /// Moves the animation clock by `by` in steps of one frame (16 ms, the last one shorter), and after each step moves
    /// the attached clocks, processes the events it leaves and renders a frame. An animator starts on the frame after
    /// the one that asked for it, so it too is finished after `advance` of its duration plus at most one frame.
    void advance(std::chrono::milliseconds by);
    /// Keeps the animation driver ticking between animations, for a test that times a hold with a QML animation (a tile
    /// held for 500 ms): Qt credits an animation that starts after an idle stretch with all the virtual time that went by
    /// while none ran, so without this a hold that follows an `advance` of a second ends in the first frame. Call it once
    /// after the screen is loaded. Off by default, because it also makes a QML `Timer` follow `advance` (a `Timer` otherwise
    /// runs on the wall clock, see `wait_real`).
    void steady_time(bool on = true);
    /// Lets real time pass for `by`, processing events, for the one thing `advance` cannot move: a QML `Timer` or a
    /// `QTimer` (wall clock). It sleeps, so use it for a component that owns such a timer and for nothing else.
    void wait_real(std::chrono::milliseconds by);
    /// Runs the manual gateway until its queue is empty, then processes events. With nothing attached, only the events.
    void settle();
    /// Called by `advance` for each step of at most one frame, in place of the attached clock: for a service rig (Stage
    /// 3's `Rig`), which moves its own clocks.
    void on_advance(std::function<void(std::chrono::milliseconds)> hook);
    /// Called by `settle`, after the attached gateway has run: for a service rig (Stage 3's `Rig`) that settles itself.
    void on_settle(std::function<void()> hook);
    void attach(test::TestClock& clock);
    void attach(test::ManualStorageGateway& gateway);

    // ---- looking at the result ----

    /// Every item under `from` (the loaded item by default), the item first, then its children in order.
    [[nodiscard]] std::vector<Node> tree(QQuickItem* from = nullptr);
    /// The item and the items that the accessibility tree lists under it, in the order a screen reader reads them.
    [[nodiscard]] std::vector<Node> accessible_tree(QQuickItem* from = nullptr);
    [[nodiscard]] QRectF geometry(QQuickItem* item) const;
    /// The first item under the root whose objectName is `name`, found through the QObject tree; null when there is
    /// none. It does not see the delegates of a Repeater or a view, or the pages of a StackView, which are not QObject
    /// children of the item that made them: use `find_item` for those.
    [[nodiscard]] QQuickItem* find(const QString& name, QQuickItem* from = nullptr) const;
    /// The first item under `from` (the root by default) whose objectName is `name`, found through the item tree
    /// (`childItems`, depth first), which includes the delegates of a Repeater or a view and the pages of a StackView.
    [[nodiscard]] QQuickItem* find_item(const QString& name, QQuickItem* from = nullptr) const;
    /// Every such item, in the order of `tree()`: for the delegates of a Repeater that share an objectName.
    [[nodiscard]] std::vector<QQuickItem*> find_items(const QString& name, QQuickItem* from = nullptr) const;
    /// Lays the item tree out and paints one frame (software renderer), so that sizes that depend on polish are final.
    void layout();
    /// The rendered window, for the few tests that look at pixels.
    [[nodiscard]] QImage grab();
    /// Makes the nodes of `item` again, so that they are painted after everything that was added to the scene graph
    /// before this call. The software renderer paints the glyphs (the `Shape` of an Icon) of a page that was built
    /// after the first frame, and that lie wholly outside the clip of their list, over an item declared after the page,
    /// such as the tab bar over a tab that is built when it is first chosen. Qt's other renderers, the one of the phone
    /// included, do not. Call it for that item before a `grab`, as the visual capture does. It hides and shows the
    /// item, one frame each.
    void repaint_over(QQuickItem* item);

private:
    struct Impl;
    QQuickItem* adopt(QObject* created);
    void settle_geometry(QQuickItem* item);
    Node describe(QQuickItem* item, int depth) const;
    void walk(QQuickItem* item, int depth, std::vector<Node>& out) const;

    std::unique_ptr<Impl> m_Impl;
};

/// Takes an item out of what a screen reader reads for as long as the object lives, for a test of the reading order of
/// a sheet or an alert that is open over a screen. Qt cannot hide what is under a modal from a screen reader, and
/// `Popup` does not either, so the test hides the base layer itself: the item is made invisible, which every check
/// skips, and shown again when the object goes. Do not use it on an ancestor of the thing being tested.
///
///     HideBase hidden(rig.find_item("logScreen"));
///     expect_clean(check_reading_order(rig));
class HideBase
{
public:
    explicit HideBase(QQuickItem* base);
    ~HideBase();

    HideBase(const HideBase&) = delete;
    HideBase& operator=(const HideBase&) = delete;

private:
    QQuickItem* m_Base;
    bool m_WasVisible;
};

/// True when `type` is `QQuickPointerHandler` or derives from it, by walking the meta objects, which keeps the private
/// Qt Quick headers out of the tests.
[[nodiscard]] bool is_pointer_handler(const QObject* object);

} // namespace ej::ui
