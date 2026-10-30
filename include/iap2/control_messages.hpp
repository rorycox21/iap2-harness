// Builds iAP2 control subscriptions sent after MFi authentication. Endpoint
// IDs and fields follow the local decompiled DiPlay reference; live-phone
// acceptance remains unverified.
#pragma once

#include "iap2/message.hpp"
#include <vector>

namespace iap2 {

std::vector<Message> controlSubscriptions();

}  // namespace iap2
