#pragma once

#include "core/FeedConnection.hpp"
#include "core/MarketDataAdapter.hpp"

#include <memory>

class QObject;

namespace tc {

std::unique_ptr<ITradingAdapter> createTradingAdapter(const FeedConnection& connection, QObject* parent = nullptr);

} // namespace tc
