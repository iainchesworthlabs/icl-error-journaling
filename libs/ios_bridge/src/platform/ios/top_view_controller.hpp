// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#import <UIKit/UIKit.h>

namespace ej::bridge
{

/// The view controller to present from: the top of whatever is presented on the key window of the foreground scene.
/// Nil when there is none. Internal to the iOS implementations (share and, later, the document picker).
UIViewController* top_view_controller();

}  // namespace ej::bridge
