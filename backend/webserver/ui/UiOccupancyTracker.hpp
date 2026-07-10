// SPDX-License-Identifier: Apache-2.0

// Copyright 2026 chargebyte GmbH

#ifndef UI_OCCUPANCY_TRACKER_HPP
#define UI_OCCUPANCY_TRACKER_HPP

#include <QString>

class UiOccupancyTracker {
public:
    bool isBusy() const;
    bool tryClaim(const QString &peerAddress);
    void release();

    bool hasOwner() const;
    QString ownerPeerAddress() const;

private:
    QString m_ownerPeerAddress;
};

#endif // UI_OCCUPANCY_TRACKER_HPP
