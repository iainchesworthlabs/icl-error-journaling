// SPDX-License-Identifier: GPL-3.0-or-later
#import <Foundation/Foundation.h>
#import <UIKit/UIKit.h>

#include "error_journaling/haptics.hpp"

namespace ej::bridge
{

namespace
{

class IosHaptics final : public Haptics
{
public:
    IosHaptics()
        : m_Impact([[UIImpactFeedbackGenerator alloc] initWithStyle:UIImpactFeedbackStyleMedium])
        , m_Notification([[UINotificationFeedbackGenerator alloc] init])
    {
    }

    void prepare() override
    {
        [m_Impact prepare];
        [m_Notification prepare];
    }

    void fire(HapticKind kind) override
    {
        // Fire first, prepare afterwards, so the prepare never sits in the latency path.
        if (kind == HapticKind::SUCCESS)
        {
            [m_Notification notificationOccurred:UINotificationFeedbackTypeSuccess];
            [m_Notification prepare];
        }
        else
        {
            [m_Impact impactOccurred];
            [m_Impact prepare];
        }
    }

private:
    UIImpactFeedbackGenerator* m_Impact;
    UINotificationFeedbackGenerator* m_Notification;
};

}  // namespace

std::unique_ptr<Haptics> make_haptics()
{
    return std::make_unique<IosHaptics>();
}

}  // namespace ej::bridge
