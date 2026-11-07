// SPDX-License-Identifier: GPL-3.0-or-later
#import <Foundation/Foundation.h>
#import <UIKit/UIKit.h>

#include "error_journaling/accessibility_settings.hpp"

#include <memory>
#include <utility>

namespace ej::bridge
{

namespace
{

// The handler lives in a shared box so that a notification block never holds the object itself: removing the observers
// in the destructor and emptying the box mean nothing is called once the object is gone.
using HandlerBox = std::shared_ptr<std::function<void()>>;

class IosAccessibilitySettings final : public AccessibilitySettings
{
public:
    IosAccessibilitySettings()
        : m_Handler(std::make_shared<std::function<void()>>())
    {
        // Content size category, Reduce Motion and VoiceOver. Delivered on the main queue, the thread of the Qt loop.
        NSNotificationName const names[] = {UIContentSizeCategoryDidChangeNotification,
                                            UIAccessibilityReduceMotionStatusDidChangeNotification,
                                            UIAccessibilityVoiceOverStatusDidChangeNotification};
        for (NSNotificationName name : names)
        {
            HandlerBox box = m_Handler;
            id token = [NSNotificationCenter.defaultCenter addObserverForName:name
                                                                       object:nil
                                                                        queue:NSOperationQueue.mainQueue
                                                                   usingBlock:^(NSNotification*) {
                                                                       if (box && *box)
                                                                       {
                                                                           (*box)();
                                                                       }
                                                                   }];
            [m_Tokens addObject:token];
        }
    }

    ~IosAccessibilitySettings() override
    {
        *m_Handler = nullptr;
        for (id token in m_Tokens)
        {
            [NSNotificationCenter.defaultCenter removeObserver:token];
        }
    }

    [[nodiscard]] AccessibilityState read() const override
    {
        AccessibilityState state;
        // UIApplication.sharedApplication is nil before the application object exists; the default size then.
        UIContentSizeCategory const category = UIApplication.sharedApplication.preferredContentSizeCategory;
        if (category != nil)
        {
            state.m_TextScale = text_scale_for_category(category.UTF8String);
        }
        state.m_ReduceMotion = UIAccessibilityIsReduceMotionEnabled();
        state.m_VoiceOverRunning = UIAccessibilityIsVoiceOverRunning();
        return state;
    }

    void set_change_handler(std::function<void()> changed) override
    {
        *m_Handler = std::move(changed);
    }

private:
    HandlerBox m_Handler;
    NSMutableArray* m_Tokens = [NSMutableArray array];
};

} // namespace

std::unique_ptr<AccessibilitySettings> make_accessibility_settings()
{
    return std::make_unique<IosAccessibilitySettings>();
}

} // namespace ej::bridge
