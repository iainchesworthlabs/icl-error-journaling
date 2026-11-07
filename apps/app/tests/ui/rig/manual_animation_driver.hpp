// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QAnimationDriver>

namespace ej::ui
{

/// The animation clock of the UI tests. Qt's own driver ticks with the wall clock; this one moves only when the test says,
/// so an animation of 120 ms is at its end after `step(120)` and a test never waits for one. It replaces the driver for
/// the whole process from construction to destruction, which is why only one `UiRig` lives at a time.
class ManualAnimationDriver final : public QAnimationDriver
{
public:
    ManualAnimationDriver();
    ~ManualAnimationDriver() override;

    ManualAnimationDriver(const ManualAnimationDriver&) = delete;
    ManualAnimationDriver& operator=(const ManualAnimationDriver&) = delete;

    /// Moves the animation clock forward by `milliseconds` and lets every running animation see the new time.
    void step(qint64 milliseconds);

    [[nodiscard]] qint64 elapsed() const override
    {
        return m_Elapsed;
    }

    /// The driver advances only through `step`.
    void advance() override {}

private:
    qint64 m_Elapsed = 0;
};

} // namespace ej::ui
