// SPDX-License-Identifier: GPL-3.0-or-later
#import <Foundation/Foundation.h>
#import <UIKit/UIKit.h>

#include "error_journaling/background_tasks.hpp"

#include <string>
#include <utility>

namespace ej::bridge
{

namespace
{

class IosBackgroundTasks final : public BackgroundTasks
{
public:
    Token begin(std::string_view name, std::function<void()> on_expiry) override
    {
        NSString* const label = [[NSString alloc] initWithBytes:name.data()
                                                         length:name.size()
                                                       encoding:NSUTF8StringEncoding];
        // The expiry handler runs on the main thread. It only reports; the caller ends the task.
        const UIBackgroundTaskIdentifier identifier =
            [UIApplication.sharedApplication beginBackgroundTaskWithName:label
                                                       expirationHandler:^{
                                                           if (on_expiry)
                                                           {
                                                               on_expiry();
                                                           }
                                                       }];
        return identifier == UIBackgroundTaskInvalid ? 0 : static_cast<Token>(identifier);
    }

    void end(Token token) override
    {
        if (token != 0)
        {
            [UIApplication.sharedApplication endBackgroundTask:static_cast<UIBackgroundTaskIdentifier>(token)];
        }
    }
};

} // namespace

std::unique_ptr<BackgroundTasks> make_background_tasks()
{
    return std::make_unique<IosBackgroundTasks>();
}

} // namespace ej::bridge
