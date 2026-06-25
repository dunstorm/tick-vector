#include "adapters/TradingAdapterFactory.hpp"

#include "adapters/RithmicMarketDataAdapter.hpp"
#include "adapters/SimulatedMarketDataAdapter.hpp"

namespace tc {

std::unique_ptr<ITradingAdapter> createTradingAdapter(const FeedConnection& connection, QObject* parent)
{
    if (connection.feedSource.compare("Rithmic", Qt::CaseInsensitive) == 0) {
        return std::make_unique<RithmicMarketDataAdapter>(parent);
    }

    return std::make_unique<SimulatedMarketDataAdapter>(parent);
}

} // namespace tc
