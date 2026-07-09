// SPDX-License-Identifier: Apache-2.0

// Copyright 2026 chargebyte GmbH

#include "UiOccupancyTracker.hpp"

#include <QtGlobal>

bool UiOccupancyTracker::isBusyForSession(const QString &sessionId) const {
    Q_UNUSED(sessionId);
    return hasOwner();
}

bool UiOccupancyTracker::tryClaim(const QString &sessionId, const QString &peerAddress) {
    if (sessionId.isEmpty()) {
        return false;
    }

    if (hasOwner()) {
        return false;
    }

    m_ownerSessionId = sessionId;
    m_ownerPeerAddress = peerAddress;
    return true;
}

void UiOccupancyTracker::release(const QString &sessionId) {
    if (sessionId.isEmpty() || m_ownerSessionId != sessionId) {
        return;
    }

    m_ownerSessionId.clear();
    m_ownerPeerAddress.clear();
}

bool UiOccupancyTracker::hasOwner() const {
    return !m_ownerSessionId.isEmpty();
}

QString UiOccupancyTracker::ownerSessionId() const {
    return m_ownerSessionId;
}

QString UiOccupancyTracker::ownerPeerAddress() const {
    return m_ownerPeerAddress;
}
