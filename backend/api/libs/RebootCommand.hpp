// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 chargebyte GmbH
#ifndef EVEREST_UI_REBOOT_COMMAND_HPP
#define EVEREST_UI_REBOOT_COMMAND_HPP

class QObject;
class QString;

void runDeferredRebootCommand(QObject* context, const QString& command);

#endif
