// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// What the Interactions board shows (Stage 14, package I-5): the interface that the board area of the Log screen reads,
// and the stand-in behind it until the storage and the board arrive (I-2, I-6). It has no types, so the board is empty
// and offers "Add interaction types". The real model (I-6) has the same name and the same property, and adds the tiles.

#include <QObject>

namespace ej::viewmodel
{

class InteractionBoardModel final : public QObject
{
    Q_OBJECT
    /// No interaction type is on the board: the board shows the empty state and the way to the setup.
    Q_PROPERTY(bool boardEmpty READ board_empty CONSTANT)

public:
    explicit InteractionBoardModel(QObject* parent = nullptr);

    bool board_empty() const;
};

} // namespace ej::viewmodel
