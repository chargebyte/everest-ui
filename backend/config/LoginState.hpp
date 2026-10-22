// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 chargebyte GmbH
#ifndef EVEREST_UI_LOGIN_STATE_HPP
#define EVEREST_UI_LOGIN_STATE_HPP

#include <QString>

namespace UiRuntime {
struct LoginState {
    bool allowed = false;
    QString reason = QStringLiteral("login_state_unavailable");
    QString operation;
};

QString loginStatePath();
// Only the launcher initializes this state, once before starting either child.
bool initializeLoginState(QString& error);
LoginState readLoginState();
bool blockLoginForReboot(const QString& operation, QString& error);
} // namespace UiRuntime

#endif
