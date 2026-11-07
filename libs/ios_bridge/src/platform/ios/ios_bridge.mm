// SPDX-License-Identifier: GPL-3.0-or-later
#import <Foundation/Foundation.h>

#include "error_journaling/ios_bridge.hpp"

namespace error_journaling::bridge
{

std::string_view implementation_name() noexcept
{
    // A placeholder in Objective-C++. The share sheet and the haptics (the Stage 0 spikes, then Stage 5) go in this
    // directory.
    static NSString* const name = @"ios";
    return std::string_view(name.UTF8String);
}

}  // namespace error_journaling::bridge
