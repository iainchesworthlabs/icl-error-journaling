// SPDX-License-Identifier: GPL-3.0-or-later
#import <Foundation/Foundation.h>
#import <UIKit/UIKit.h>

#include "error_journaling/ios_bridge.hpp"

#include <utility>

namespace error_journaling::bridge
{

namespace
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

void deliver_later(share_callback done, share_result result)
{
    // The completion handler never runs on a path that did not present, so deliver the failure on the next turn of the
    // main queue to keep the promise that the callback comes after the call returns.
    dispatch_async(dispatch_get_main_queue(), ^{
        done(result);
    });
}

class ios_services final : public platform_services
{
public:
    ios_services()
        : m_Impact([[UIImpactFeedbackGenerator alloc] initWithStyle:UIImpactFeedbackStyleMedium])
        , m_Notification([[UINotificationFeedbackGenerator alloc] init])
    {
    }

    void present_share_sheet(std::string_view text, share_callback done) override
    {
        NSString* const string = [[NSString alloc] initWithBytes:text.data()
                                                          length:text.size()
                                                        encoding:NSUTF8StringEncoding];
        UIViewController* const presenter = top_view_controller();
        if (presenter == nil || string == nil)
        {
            deliver_later(std::move(done), share_result{share_outcome::failed, {}});
            return;
        }

        UIActivityViewController* const sheet = [[UIActivityViewController alloc] initWithActivityItems:@[ string ]
                                                                                  applicationActivities:nil];
        // The completion handler runs on the main thread, which is the thread the Qt event loop runs on, so no hop is
        // needed. It runs once, whether the user shared, cancelled or an activity failed.
        sheet.completionWithItemsHandler = ^(UIActivityType activityType, BOOL completed, NSArray*, NSError* error) {
            share_result result;
            result.outcome = error != nil ? share_outcome::failed
                                          : (completed ? share_outcome::shared : share_outcome::cancelled);
            if (activityType != nil)
            {
                result.activity_type = activityType.UTF8String;
            }
            done(result);
        };
        // Unused on an iPhone; an iPad raises an exception without an anchor.
        sheet.popoverPresentationController.sourceView = presenter.view;
        [presenter presentViewController:sheet animated:YES completion:nil];
    }

    void prepare_haptics() override
    {
        [m_Impact prepare];
        [m_Notification prepare];
    }

    void fire_haptic(haptic_kind kind) override
    {
        if (kind == haptic_kind::success)
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

std::string_view implementation_name() noexcept
{
    return "ios";
}

std::unique_ptr<platform_services> make_platform_services()
{
    return std::make_unique<ios_services>();
}

}  // namespace error_journaling::bridge
