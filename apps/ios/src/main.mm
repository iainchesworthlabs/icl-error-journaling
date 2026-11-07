// SPDX-License-Identifier: GPL-3.0-or-later
#import <UIKit/UIKit.h>

#include <string>

#include "icl_template/greeting.hpp"

// The smallest application that can be signed, uploaded to TestFlight and opened: one screen showing the greeting of
// the core library. A project replaces it with its own (Qt Quick, UIKit or anything else); what stays is the packaging
// around it (CMakeLists.txt, packaging/) and the workflows.

@interface IclAppDelegate : UIResponder <UIApplicationDelegate>
@property(nonatomic, strong) UIWindow* window;
@end

@implementation IclAppDelegate

- (BOOL)application:(UIApplication*)application didFinishLaunchingWithOptions:(NSDictionary*)launchOptions
{
    (void)application;
    (void)launchOptions;

    UIViewController* controller = [[UIViewController alloc] init];
    controller.view.backgroundColor = UIColor.systemBackgroundColor;

    const icl_template::Greeter greeter("world");
    UILabel* label = [[UILabel alloc] init];
    label.text = [NSString stringWithUTF8String:greeter.greet().c_str()];
    label.numberOfLines = 0;
    label.textAlignment = NSTextAlignmentCenter;
    label.font = [UIFont preferredFontForTextStyle:UIFontTextStyleTitle2];
    label.adjustsFontForContentSizeCategory = YES;
    label.translatesAutoresizingMaskIntoConstraints = NO;
    [controller.view addSubview:label];
    // The label stays inside the safe area (the notch, the home indicator, the rounded corners).
    UILayoutGuide* safe = controller.view.safeAreaLayoutGuide;
    [NSLayoutConstraint activateConstraints:@[
        [label.centerYAnchor constraintEqualToAnchor:safe.centerYAnchor],
        [label.leadingAnchor constraintEqualToAnchor:safe.leadingAnchor constant:24],
        [label.trailingAnchor constraintEqualToAnchor:safe.trailingAnchor constant:-24],
    ]];

    self.window = [[UIWindow alloc] initWithFrame:UIScreen.mainScreen.bounds];
    self.window.rootViewController = controller;
    [self.window makeKeyAndVisible];
    return YES;
}

@end

int main(int argc, char* argv[])
{
    @autoreleasepool
    {
        return UIApplicationMain(argc, argv, nil, NSStringFromClass([IclAppDelegate class]));
    }
}
