// SPDX-License-Identifier: GPL-3.0-or-later
#import <Foundation/Foundation.h>
#import <UIKit/UIKit.h>

#include "error_journaling/window_limits.hpp"

namespace ej::bridge
{

namespace
{

class IosWindowLimits final : public WindowLimits
{
public:
    void set_minimum_size(double width, double height) override
    {
        const CGSize minimum = CGSizeMake(width, height);
        for (UIScene* scene in UIApplication.sharedApplication.connectedScenes)
        {
            if (![scene isKindOfClass:UIWindowScene.class])
            {
                continue;
            }
            UIWindowScene* const window_scene = static_cast<UIWindowScene*>(scene);
            // UISceneSizeRestrictions is iPadOS 16 and later, and nil on an iPhone, where a message to it does nothing. The
            // check is the compiler's, not the preprocessor's.
            if (@available(iOS 16.0, *))
            {
                window_scene.sizeRestrictions.minimumSize = minimum;
            }
        }
    }
};

} // namespace

std::unique_ptr<WindowLimits> make_window_limits()
{
    return std::make_unique<IosWindowLimits>();
}

} // namespace ej::bridge
