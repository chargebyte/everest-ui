// SPDX-License-Identifier: Apache-2.0

// Copyright 2026 chargebyte GmbH

#include "UiOccupancyTracker.hpp"

bool UiOccupancyTracker::isBusy() const {
    return hasOwner();
}

bool UiOccupancyTracker::tryClaim(const QString &peerAddress) {
    if (hasOwner()) {
        return false;
    }

    m_ownerPeerAddress = peerAddress;
    return true;
}

void UiOccupancyTracker::release() {
    m_ownerPeerAddress.clear();
}

bool UiOccupancyTracker::hasOwner() const { return !m_ownerPeerAddress.isEmpty(); }

QString UiOccupancyTracker::ownerPeerAddress() const {
    return m_ownerPeerAddress;
}
