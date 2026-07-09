// SPDX-License-Identifier: Apache-2.0

// Copyright 2026 chargebyte GmbH

#ifndef UI_OCCUPANCY_TRACKER_HPP
#define UI_OCCUPANCY_TRACKER_HPP

#include <QString>

class UiOccupancyTracker {
public:
    bool isBusyForSession(const QString &sessionId) const;
    bool tryClaim(const QString &sessionId, const QString &peerAddress);
    void release(const QString &sessionId);

    bool hasOwner() const;
    QString ownerSessionId() const;
    QString ownerPeerAddress() const;

private:
    QString m_ownerSessionId;
    QString m_ownerPeerAddress;
};

#endif // UI_OCCUPANCY_TRACKER_HPP
