// SPDX-License-Identifier: GPL-3.0-or-later
#import <Foundation/Foundation.h>
#import <UIKit/UIKit.h>
#import <os/log.h>

#include "error_journaling/share.hpp"
#include "top_view_controller.hpp"

#include <string>
#include <utility>

namespace ej::bridge
{

namespace
{

os_log_t share_log()
{
    static os_log_t const log = os_log_create("com.iainchesworthlabs.errorjournaling", "share");
    return log;
}

const char* outcome_name(ShareOutcome outcome)
{
    switch (outcome)
    {
    case ShareOutcome::SHARED:
        return "shared";
    case ShareOutcome::CANCELLED:
        return "cancelled";
    case ShareOutcome::UNAVAILABLE:
        return "unavailable";
    case ShareOutcome::FAILED:
        break;
    }
    return "failed";
}

// One line per outcome: outcome and activity type, never content. The public specifier keeps a release build from
// redacting the dynamic strings.
void log_outcome(const ShareResult& result)
{
    os_log(share_log(), "share: %{public}s %{public}s", outcome_name(result.m_Outcome),
           result.m_ActivityType.c_str());
}

void deliver_later(ShareCallback done, ShareResult result)
{
    // The completion handler never runs on a path that did not present, so deliver the failure on the next turn of the
    // main queue to keep the promise that the callback comes after the call returns.
    dispatch_async(dispatch_get_main_queue(), ^{
        log_outcome(result);
        done(result);
    });
}

class IosShare final : public Share
{
public:
    [[nodiscard]] bool available() const noexcept override
    {
        return true;
    }

    void present(ShareRequest request, ShareCallback done) override
    {
        UIViewController* const presenter = top_view_controller();
        id item = nil;
        if (!request.m_File.empty())
        {
            // A file is an NSURL item, so Mail, Files and AirDrop get a document and not a string.
            item = [NSURL fileURLWithPath:[NSString stringWithUTF8String:request.m_File.c_str()]];
        }
        else
        {
            // The portable SharePresenter has checked the bytes; a nil here is still reported, not assumed away.
            item = [[NSString alloc] initWithBytes:request.m_Text.data()
                                            length:request.m_Text.size()
                                          encoding:NSUTF8StringEncoding];
        }
        if (presenter == nil || item == nil)
        {
            deliver_later(std::move(done), ShareResult{ShareOutcome::FAILED, {}, {}});
            return;
        }

        UIActivityViewController* const sheet = [[UIActivityViewController alloc] initWithActivityItems:@[ item ]
                                                                                  applicationActivities:nil];
        // The completion handler runs on the main thread, which is the thread the Qt event loop runs on, so no hop is
        // needed. It runs once, whether the user shared, cancelled or an activity failed.
        sheet.completionWithItemsHandler = ^(UIActivityType activityType, BOOL completed, NSArray*, NSError* error) {
            ShareResult result;
            result.m_Outcome = error != nil ? ShareOutcome::FAILED
                                            : (completed ? ShareOutcome::SHARED : ShareOutcome::CANCELLED);
            if (activityType != nil)
            {
                result.m_ActivityType = activityType.UTF8String;
            }
            if (error != nil)
            {
                // Domain and code only: an error's description can carry a path or text.
                result.m_Detail = std::string(error.domain.UTF8String) + ":" + std::to_string(error.code);
            }
            log_outcome(result);
            done(result);
        };
        // Unused on an iPhone; an iPad raises an exception without an anchor.
        sheet.popoverPresentationController.sourceView = presenter.view;
        [presenter presentViewController:sheet animated:YES completion:nil];
    }
};

}  // namespace

std::unique_ptr<Share> make_share()
{
    return std::make_unique<IosShare>();
}

}  // namespace ej::bridge
