// SPDX-License-Identifier: GPL-3.0-or-later
// MiniBoard (docs/plan/stage-4.md, 4.5): mini_board.drag, mini_board.swap and mini_board.buttons, against the real
// BoardLayoutModel over the sample configuration. A relay stands for SettingsModel: it moves the tile in the
// configuration and refreshes the model, as moveBoardTile does, or refuses.
//
// The project and Stage 3 headers come before the Qt Quick ones, because the core headers define a name that the
// Windows headers behind Qt Quick define as a macro. clang-format would sort board_test_support.hpp first, so it is
// switched off.
// clang-format off
#include "storage_samples.hpp"
#include "support/test_environment.hpp"
#include "viewmodel/board_layout_model.hpp"

#include "board_test_support.hpp"
#include "rig/ui_checks.hpp"
#include "rig/ui_rig.hpp"
// clang-format on
