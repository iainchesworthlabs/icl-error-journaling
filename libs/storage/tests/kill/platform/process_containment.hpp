// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

class QProcess;

namespace ej::test::kill
{

/// Puts a worker that has just been started where it ends with the driver. On Windows it joins a Job Object with
/// kill-on-close, so an aborted test run leaves no stray worker (section 4.2 of docs/plan/stage-2.md). Elsewhere there
/// is nothing to do: the worker ends by itself after a bounded number of operations, and CI ends what is left.
void contain(QProcess& process);

} // namespace ej::test::kill
