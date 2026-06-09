#include "adapters/SimulatedMarketDataAdapter.hpp"

#include <QtCore/QRandomGenerator>
#include <cmath>

namespace tc {

namespace {

double roundToTick(double value)
{
    return std::round(value * 4.0) / 4.0;
}

AggressorSide sideForIndex(int index)
{
    return index % 3 == 0 ? AggressorSide::Sell : AggressorSide::Buy;
}

} // namespace

SimulatedMarketDataAdapter::SimulatedMarketDataAdapter(QObject* parent)
    : QObject(parent)
{
    timer_.setInterval(250);
    QObject::connect(&timer_, &QTimer::timeout, this, [this] { onTimer(); });
    seed();
}

QString SimulatedMarketDataAdapter::name() const
{
    return "Simulator";
}

bool SimulatedMarketDataAdapter::connectAdapter(const ConnectionConfig& config)
{
    snapshot_.connected = true;
    snapshot_.connectionLabel = config.useRealRithmic ? "Rithmic SDK not linked - simulator active" : "Simulator";
    snapshot_.account.account = config.account.isEmpty() ? "SIM-ACCOUNT" : config.account;
    timer_.start();
    emitSnapshot();
    return true;
}

void SimulatedMarketDataAdapter::disconnectAdapter()
{
    timer_.stop();
    snapshot_.connected = false;
    emitSnapshot();
}

bool SimulatedMarketDataAdapter::isConnected() const
{
    return snapshot_.connected;
}

void SimulatedMarketDataAdapter::subscribe(const QString& symbol)
{
    snapshot_.symbol = symbol;
    snapshot_.position.symbol = symbol;
    seed();
    snapshot_.connected = true;
    timer_.start();
    emitSnapshot();
}

MarketSnapshot SimulatedMarketDataAdapter::snapshot() const
{
    return snapshot_;
}

ExecutionReport SimulatedMarketDataAdapter::submitMarketOrder(const OrderRequest& request)
{
    snapshot_.position.symbol = request.symbol;
    snapshot_.position.quantity = request.quantity;
    snapshot_.position.side = request.side == OrderSide::Buy ? "LONG" : "SHORT";
    snapshot_.position.averagePrice = snapshot_.last;
    snapshot_.position.unrealizedPnl = request.side == OrderSide::Buy ? 157.50 : -42.50;

    snapshot_.recentOrders.prepend({
        QDateTime::currentDateTimeUtc(),
        snapshot_.last,
        request.quantity,
        request.side == OrderSide::Buy ? AggressorSide::Buy : AggressorSide::Sell,
        "Market"
    });
    while (snapshot_.recentOrders.size() > 12) {
        snapshot_.recentOrders.removeLast();
    }

    emitSnapshot();
    return {true, "Accepted by simulator", snapshot_.position};
}

ExecutionReport SimulatedMarketDataAdapter::flatten()
{
    snapshot_.position.side = "FLAT";
    snapshot_.position.quantity = 0;
    snapshot_.position.unrealizedPnl = 0.0;
    emitSnapshot();
    return {true, "Flattened", snapshot_.position};
}

void SimulatedMarketDataAdapter::cancelAll()
{
    emitSnapshot();
}

void SimulatedMarketDataAdapter::setSnapshotHandler(SnapshotHandler handler)
{
    handler_ = std::move(handler);
}

void SimulatedMarketDataAdapter::seed()
{
    snapshot_.exchangeTime = QDateTime::currentDateTimeUtc();
    snapshot_.open = 18708.50;
    snapshot_.high = 18714.25;
    snapshot_.low = 18703.75;
    snapshot_.close = 18710.00;
    snapshot_.last = 18710.00;
    snapshot_.networkLatencyMs = 10;
    snapshot_.speedOfTape = 1246;
    snapshot_.account.equity = 152873.52;
    snapshot_.account.realizedPnl = 1098.75;
    snapshot_.account.dayPnl = 1313.75;
    snapshot_.account.drawdown = 1102.25;
    snapshot_.risk.stopLoss = 18686.25;
    snapshot_.risk.takeProfit = 18742.25;
    snapshot_.risk.usedLossPercent = 43.0;
    if (snapshot_.position.side.isEmpty() || snapshot_.position.side == "FLAT") {
        snapshot_.position = {snapshot_.symbol, "LONG", 3, 18711.50, 157.50};
    }

    snapshot_.candles.clear();
    double price = 18605.0;
    QDateTime start = QDateTime::currentDateTimeUtc().addSecs(-92 * 120);
    for (int i = 0; i < 92; ++i) {
        const double drift = i < 16 ? 4.7 : i < 35 ? 8.6 : i < 49 ? -2.6 : i < 61 ? 5.2 : i < 72 ? -10.2 : i < 85 ? 6.2 : 4.8;
        const double wiggle = std::sin(i * 0.92) * 11.0 + std::cos(i * 0.31) * 7.0;
        const double open = price;
        const double close = roundToTick(open + drift + wiggle * 0.48);
        const double high = roundToTick(std::max(open, close) + 8.0 + std::abs(std::sin(i * 1.7)) * 14.0);
        const double low = roundToTick(std::min(open, close) - 8.0 - std::abs(std::cos(i * 1.15)) * 12.0);
        const double volume = 20.0 + std::abs(std::sin(i * 0.54)) * 66.0 + (i % 13 == 0 ? 50.0 : 0.0);
        snapshot_.candles.push_back({start.addSecs(i * 120), open, high, low, close, volume, close >= open ? volume * 0.6 : -volume * 0.6});
        price = close;
    }

    snapshot_.bigTrades = {
        {24, 18630.0, 75, AggressorSide::Buy},
        {38, 18782.0, 200, AggressorSide::Sell},
        {56, 18776.0, 744, AggressorSide::Buy},
        {64, 18682.0, 300, AggressorSide::Buy},
        {72, 18618.0, 200, AggressorSide::Sell},
        {84, 18645.0, 75, AggressorSide::Buy},
    };

    snapshot_.recentOrders.clear();
    for (int i = 0; i < 8; ++i) {
        snapshot_.recentOrders.push_back({
            QDateTime::currentDateTimeUtc().addSecs(-i),
            roundToTick(snapshot_.last - i * 0.25),
            i == 2 ? 200 : 75 + i * 9,
            sideForIndex(i),
            "Market"
        });
    }

    rebuildProfile();
    rebuildDom();
}

void SimulatedMarketDataAdapter::onTimer()
{
    ++sequence_;
    snapshot_.exchangeTime = QDateTime::currentDateTimeUtc();
    const double step = std::sin(sequence_ * 0.28) * 0.75 + std::cos(sequence_ * 0.11) * 0.25;
    snapshot_.last = roundToTick(snapshot_.last + step);
    snapshot_.close = snapshot_.last;
    snapshot_.high = std::max(snapshot_.high, snapshot_.last);
    snapshot_.low = std::min(snapshot_.low, snapshot_.last);
    snapshot_.speedOfTape = 1050 + static_cast<int>(std::abs(std::sin(sequence_ * 0.19)) * 800);
    snapshot_.networkLatencyMs = 8 + (sequence_ % 8);

    if (!snapshot_.candles.isEmpty()) {
        auto& last = snapshot_.candles.last();
        last.close = snapshot_.last;
        last.high = std::max(last.high, snapshot_.last + 1.25);
        last.low = std::min(last.low, snapshot_.last - 1.0);
        last.volume += 4 + sequence_ % 9;
        last.delta += step >= 0 ? 8 : -8;
    }

    rebuildDom();
    emitSnapshot();
}

void SimulatedMarketDataAdapter::rebuildProfile()
{
    snapshot_.volumeProfile.clear();
    for (int i = 0; i < 54; ++i) {
        const double price = 18820.0 - i * 7.5;
        const double wave = std::abs(std::sin(i * 0.31)) * 120.0 + std::abs(std::cos(i * 0.17)) * 42.0;
        snapshot_.volumeProfile.push_back({
            roundToTick(price),
            wave + (i == 18 ? 90.0 : 0.0),
            std::sin(i * 0.37) * 500.0,
            i == 13,
            i == 25,
            i == 18,
            i == 31 || i == 43
        });
    }
}

void SimulatedMarketDataAdapter::rebuildDom()
{
    snapshot_.dom.clear();
    const double center = roundToTick(snapshot_.last);
    for (int i = -10; i <= 10; ++i) {
        const double price = roundToTick(center - i * 0.25);
        const int bid = std::max(10, 108 + i * 9 + static_cast<int>(std::sin(sequence_ * 0.2 + i) * 35.0));
        const int ask = std::max(10, 94 - i * 4 + static_cast<int>(std::cos(sequence_ * 0.17 + i) * 28.0));
        const int delta = bid - ask;
        snapshot_.dom.push_back({price, bid, ask, delta, delta * 14 + i * 27});
    }
}

void SimulatedMarketDataAdapter::emitSnapshot()
{
    if (handler_) {
        handler_(snapshot_);
    }
}

} // namespace tc
