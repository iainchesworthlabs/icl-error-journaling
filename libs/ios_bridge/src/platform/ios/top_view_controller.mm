// SPDX-License-Identifier: GPL-3.0-or-later
#include "top_view_controller.hpp"

namespace ej::bridge
{

// Qt Quick on iOS owns a UIWindow whose root view controller is Qt's own. The bridge has no QWindow, so it takes the key
// window of the foreground scene and presents from the top of whatever is already presented.
UIViewController* top_view_controller()
{
    UIWindow* window = nil;
    for (UIScene* scene in UIApplication.sharedApplication.connectedScenes)
    {
        if (scene.activationState != UISceneActivationStateForegroundActive ||
            ![scene isKindOfClass:UIWindowScene.class])
        {
            continue;
        }
        for (UIWindow* candidate in static_cast<UIWindowScene*>(scene).windows)
        {
            if (candidate.isKeyWindow)
            {
                window = candidate;
                break;
            }
        }
    }
    UIViewController* controller = window.rootViewController;
    while (controller.presentedViewController != nil)
    {
        controller = controller.presentedViewController;
    }
    return controller;
}

}  // namespace ej::bridge
