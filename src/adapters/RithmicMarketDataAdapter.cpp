#include "adapters/RithmicMarketDataAdapter.hpp"

#include <QtCore/QDate>
#include <QtCore/QDateTime>
#include <QtCore/QRegularExpression>
#include <QtCore/QTimeZone>
#include <algorithm>
#include <cmath>
#include <utility>

namespace tc {

namespace {

constexpr quint32 kTickerUpdateBits = 1 | 2 | 4;
constexpr int kCandleSeconds = 120;
constexpr int kMaxCandles = 240;
constexpr int kBackfillBars = 240;
constexpr int kBackfillMinutes = 2;
constexpr int kCacheFreshSeconds = 300;
constexpr int kMaxProfileBins = 220;

QStringList splitQualifiedSymbol(const QString& symbol)
{
    const QString cleaned = symbol.trimmed();
    if (cleaned.contains(':')) {
        return cleaned.split(':', Qt::SkipEmptyParts);
    }
    if (cleaned.contains('/')) {
        return cleaned.split('/', Qt::SkipEmptyParts);
    }
    return {cleaned};
}

QString sideText(AggressorSide side)
{
    if (side == AggressorSide::Buy) {
        return "BUY";
    }
    if (side == AggressorSide::Sell) {
        return "SELL";
    }
    return "TRADE";
}

bool looksLikeDatedContract(const QString& symbol)
{
    static const QRegularExpression monthCodePattern("[FGHJKMNQUVXZ]\\d+$");
    return monthCodePattern.match(symbol.trimmed().toUpper()).hasMatch();
}

QString localFrontMonthContract(const QString& exchange, const QString& rootSymbol, QDate date = QDate::currentDate())
{
    const QString ex = exchange.trimmed().toUpper();
    const QString root = rootSymbol.trimmed().toUpper();
    if (ex != "COMEX" || (root != "GC" && root != "MGC")) {
        return {};
    }

    struct ContractMonth {
        int month;
        QChar code;
    };
    static constexpr ContractMonth goldMonths[] = {
        {2, 'G'},
        {4, 'J'},
        {6, 'M'},
        {8, 'Q'},
        {12, 'Z'},
    };

    int year = date.year();
    QChar monthCode = goldMonths[0].code;
    bool found = false;
    for (const auto& month : goldMonths) {
        if (month.month > date.month()) {
            monthCode = month.code;
            found = true;
            break;
        }
    }
    if (!found) {
        ++year;
    }
    return root + monthCode + QString::number(year % 10);
}

QString cacheKey(const QString& exchange, const QString& symbol)
{
    return exchange.trimmed().toUpper() + ":" + symbol.trimmed().toUpper() + ":2m";
}

bool sameInstrument(const QString& leftExchange, const QString& leftSymbol, const QString& rightExchange, const QString& rightSymbol)
{
    return leftExchange.trimmed().compare(rightExchange.trimmed(), Qt::CaseInsensitive) == 0
        && leftSymbol.trimmed().compare(rightSymbol.trimmed(), Qt::CaseInsensitive) == 0;
}

bool isFinitePositive(double value)
{
    return std::isfinite(value) && value > 0.0;
}

bool isValidCandle(const Candle& candle)
{
    return candle.time.isValid()
        && isFinitePositive(candle.open)
        && isFinitePositive(candle.high)
        && isFinitePositive(candle.low)
        && isFinitePositive(candle.close)
        && candle.high >= candle.low;
}

double tickSizeForSymbol(const QString& exchange, const QString& symbol)
{
    const QString ex = exchange.trimmed().toUpper();
    const QString sym = symbol.trimmed().toUpper();
    if (ex == "COMEX" && (sym.startsWith("GC") || sym.startsWith("MGC"))) {
        return 0.1;
    }
    if (ex == "COMEX" && (sym.startsWith("SI") || sym.startsWith("SIL"))) {
        return 0.005;
    }
    if (ex == "COMEX" && sym.startsWith("HG")) {
        return 0.0005;
    }
    if (sym.startsWith("ES") || sym.startsWith("MES") || sym.startsWith("NQ") || sym.startsWith("MNQ")) {
        return 0.25;
    }
    if (sym.startsWith("YM") || sym.startsWith("MYM")) {
        return 1.0;
    }
    if (sym.startsWith("CL") || sym.startsWith("MCL")) {
        return 0.01;
    }
    return 0.25;
}

double roundToTick(double price, double tickSize)
{
    if (!std::isfinite(price) || tickSize <= 0.0) {
        return price;
    }
    return std::round(price / tickSize) * tickSize;
}

Candle normalizedCandle(Candle candle)
{
    const double high = std::max({candle.open, candle.high, candle.low, candle.close});
    const double low = std::min({candle.open, candle.high, candle.low, candle.close});
    candle.high = high;
    candle.low = low;
    return candle;
}

} // namespace

RithmicMarketDataAdapter::RithmicMarketDataAdapter(QObject* parent)
    : QObject(parent)
{
    snapshot_.connectionLabel = "Rithmic feed is not connected";
}

QString RithmicMarketDataAdapter::name() const
{
    return "Rithmic Protocol";
}

bool RithmicMarketDataAdapter::connectAdapter(const ConnectionConfig& config)
{
    snapshot_.account.account = config.account;
    snapshot_.connected = false;
    snapshot_.connectionFailed = false;
    snapshot_.connectionLabel = "Connecting to Rithmic Protocol...";
    snapshot_.buildingChartData = false;
    snapshot_.chartBarsLoaded = 0;
    snapshot_.chartBarsExpected = 0;
    snapshot_.chartDataLabel.clear();
    connectionStarted_ = false;
    emitSnapshot();

    protocol_ = std::make_unique<RithmicProtocolClient>();
    protocol_->setStatusHandler([this](const QString& message) { handleStatus(message); });
    protocol_->setErrorHandler([this](const QString& message) { handleError(message); });
    protocol_->setConnectedHandler([this] { handleConnected(); });
    protocol_->setTradeHandler([this](const RithmicTradeTick& trade) { handleTrade(trade); });
    protocol_->setBboHandler([this](const RithmicBboTick& bbo) { handleBbo(bbo); });
    protocol_->setOrderBookHandler([this](const QVector<DomLevel>& levels) { handleOrderBook(levels); });
    protocol_->setHistoryBarHandler([this](const Candle& candle, int loaded, int expected) { handleHistoricalBar(candle, loaded, expected); });
    protocol_->setHistoryFinishedHandler([this](bool ok, const QString& message) { handleHistoryFinished(ok, message); });
    protocol_->setFrontMonthHandler([this](bool ok, const QString& symbol, const QString& exchange, const QString& message) {
        handleFrontMonth(ok, symbol, exchange, message);
    });

    connectionStarted_ = protocol_->connectToTickerPlant(config);
    return connectionStarted_;
}

void RithmicMarketDataAdapter::disconnectAdapter()
{
    if (protocol_) {
        protocol_->disconnectFromPlant();
    }
    snapshot_.connected = false;
    snapshot_.connectionFailed = false;
    snapshot_.buildingChartData = false;
    snapshot_.connectionLabel = "Rithmic feed disconnected";
    connectionStarted_ = false;
    emitSnapshot();
}

bool RithmicMarketDataAdapter::isConnected() const
{
    return protocol_ && protocol_->isConnected();
}

void RithmicMarketDataAdapter::subscribe(const QString& symbol)
{
    const QStringList parts = splitQualifiedSymbol(symbol);
    if (parts.size() >= 2) {
        pendingExchange_ = parts.at(0).trimmed();
        pendingSymbol_ = parts.at(1).trimmed();
    } else {
        pendingExchange_ = "CME";
        pendingSymbol_ = parts.value(0).trimmed();
    }

    requestedRootSymbol_ = pendingSymbol_;
    resolvingFrontMonth_ = false;
    snapshot_.symbol = pendingExchange_ + ":" + requestedRootSymbol_;
    snapshot_.position.symbol = snapshot_.symbol;
    snapshot_.last = 0.0;
    snapshot_.open = 0.0;
    snapshot_.high = 0.0;
    snapshot_.low = 0.0;
    snapshot_.close = 0.0;
    snapshot_.candles.clear();
    snapshot_.bigTrades.clear();
    snapshot_.volumeProfile.clear();
    snapshot_.dom.clear();
    snapshot_.recentOrders.clear();
    snapshot_.speedOfTape = 0;
    snapshot_.chartBarsLoaded = 0;
    snapshot_.chartBarsExpected = kBackfillBars;
    snapshot_.chartDataLabel.clear();
    resolvePendingSymbol();
    emitSnapshot();
}

MarketSnapshot RithmicMarketDataAdapter::snapshot() const
{
    return snapshot_;
}

ExecutionReport RithmicMarketDataAdapter::submitMarketOrder(const OrderRequest& request)
{
    return {false, "Rithmic Protocol order routing is not wired yet.", {request.symbol, "FLAT", 0, 0.0, 0.0}};
}

ExecutionReport RithmicMarketDataAdapter::flatten()
{
    return {false, "Rithmic Protocol flatten is not wired yet.", snapshot_.position};
}

void RithmicMarketDataAdapter::cancelAll()
{
    snapshot_.connectionLabel = "Rithmic Protocol cancel-all is not wired yet.";
    emitSnapshot();
}

void RithmicMarketDataAdapter::setSnapshotHandler(SnapshotHandler handler)
{
    handlers_.clear();
    handlers_.push_back({{}, std::move(handler), false});
}

void RithmicMarketDataAdapter::addSnapshotHandler(QObject* context, SnapshotHandler handler)
{
    handlers_.push_back({QPointer<QObject>(context), std::move(handler), context != nullptr});
}

void RithmicMarketDataAdapter::handleStatus(const QString& message)
{
    snapshot_.connectionFailed = false;
    if (snapshot_.buildingChartData && message.startsWith("Building chart data")) {
        snapshot_.chartDataLabel = message;
    } else {
        snapshot_.connectionLabel = message;
    }
    emitSnapshot();
}

void RithmicMarketDataAdapter::handleError(const QString& message)
{
    snapshot_.connected = false;
    snapshot_.connectionFailed = true;
    snapshot_.buildingChartData = false;
    snapshot_.connectionLabel = message;
    connectionStarted_ = false;
    emitSnapshot();
}

void RithmicMarketDataAdapter::handleConnected()
{
    snapshot_.connected = true;
    snapshot_.connectionFailed = false;
    snapshot_.connectionLabel = "Rithmic Protocol connected.";
    resolvePendingSymbol();
    emitSnapshot();
}

void RithmicMarketDataAdapter::handleTrade(const RithmicTradeTick& trade)
{
    if (!isFinitePositive(trade.price) || trade.size <= 0) {
        return;
    }
    if (!sameInstrument(trade.exchange, trade.symbol, pendingExchange_, pendingSymbol_)) {
        return;
    }

    snapshot_.connected = true;
    snapshot_.connectionFailed = false;
    snapshot_.connectionLabel = "Live Rithmic feed";
    snapshot_.symbol = trade.exchange + ":" + trade.symbol;
    snapshot_.exchangeTime = trade.time.isValid() ? trade.time : QDateTime::currentDateTimeUtc();
    snapshot_.last = trade.price;
    snapshot_.close = trade.price;
    if (snapshot_.open == 0.0) {
        snapshot_.open = trade.price;
        snapshot_.high = trade.price;
        snapshot_.low = trade.price;
    } else {
        snapshot_.high = std::max(snapshot_.high, trade.price);
        snapshot_.low = std::min(snapshot_.low, trade.price);
    }

    snapshot_.recentOrders.prepend({
        snapshot_.exchangeTime,
        trade.price,
        trade.size,
        trade.aggressor,
        sideText(trade.aggressor)
    });
    while (snapshot_.recentOrders.size() > 20) {
        snapshot_.recentOrders.removeLast();
    }

    upsertCandle(trade);
    updateProfileFromTrade(trade);
    emitSnapshot();
}

void RithmicMarketDataAdapter::handleBbo(const RithmicBboTick& bbo)
{
    if (!sameInstrument(bbo.exchange, bbo.symbol, pendingExchange_, pendingSymbol_)) {
        return;
    }

    snapshot_.connected = true;
    snapshot_.connectionFailed = false;
    snapshot_.exchangeTime = bbo.time.isValid() ? bbo.time : QDateTime::currentDateTimeUtc();
    const auto upsertBboLevel = [this](double price, int bidSize, int askSize) {
        if (!isFinitePositive(price)) {
            return;
        }
        const double tick = tickSizeForSymbol(pendingExchange_, pendingSymbol_);
        const double roundedPrice = roundToTick(price, tick);
        for (auto& level : snapshot_.dom) {
            if (std::abs(level.price - roundedPrice) < tick * 0.25) {
                level.bidSize = bidSize > 0 ? bidSize : level.bidSize;
                level.askSize = askSize > 0 ? askSize : level.askSize;
                level.delta = level.bidSize - level.askSize;
                return;
            }
        }
        snapshot_.dom.push_back({roundedPrice, bidSize, askSize, bidSize - askSize, 0});
    };
    upsertBboLevel(bbo.bidPrice, bbo.bidSize, 0);
    upsertBboLevel(bbo.askPrice, 0, bbo.askSize);
    std::sort(snapshot_.dom.begin(), snapshot_.dom.end(), [](const DomLevel& left, const DomLevel& right) {
        return left.price > right.price;
    });
    while (snapshot_.dom.size() > 80) {
        snapshot_.dom.removeLast();
    }
    emitSnapshot();
}

void RithmicMarketDataAdapter::handleOrderBook(const QVector<DomLevel>& levels)
{
    snapshot_.connected = true;
    snapshot_.connectionFailed = false;
    snapshot_.dom = levels;
    std::sort(snapshot_.dom.begin(), snapshot_.dom.end(), [](const DomLevel& left, const DomLevel& right) {
        return left.price > right.price;
    });
    emitSnapshot();
}

void RithmicMarketDataAdapter::handleHistoricalBar(const Candle& candle, int loaded, int expected)
{
    const Candle normalized = normalizedCandle(candle);
    if (!isValidCandle(normalized)) {
        return;
    }

    snapshot_.connected = true;
    snapshot_.connectionFailed = false;
    snapshot_.buildingChartData = true;
    snapshot_.chartBarsLoaded = loaded;
    snapshot_.chartBarsExpected = expected;
    snapshot_.chartDataLabel = "Building chart data";
    snapshot_.connectionLabel = "Live Rithmic feed";
    mergeCandle(normalized);
    updateProfileFromCandle(normalized);
    if (snapshot_.last == 0.0) {
        snapshot_.last = normalized.close;
        snapshot_.open = normalized.open;
        snapshot_.high = normalized.high;
        snapshot_.low = normalized.low;
        snapshot_.close = normalized.close;
        snapshot_.exchangeTime = normalized.time;
    }
    emitSnapshot();
}

void RithmicMarketDataAdapter::handleHistoryFinished(bool ok, const QString& message)
{
    snapshot_.buildingChartData = false;
    snapshot_.chartDataLabel.clear();
    if (!ok) {
        snapshot_.connectionLabel = snapshot_.connected ? "Backfill unavailable: " + message : message;
    } else {
        snapshot_.connectionLabel = snapshot_.connected ? "Live Rithmic feed" : message;
    }
    emitSnapshot();
}

void RithmicMarketDataAdapter::handleFrontMonth(bool ok, const QString& symbol, const QString& exchange, const QString& message)
{
    resolvingFrontMonth_ = false;
    if (!ok || symbol.trimmed().isEmpty()) {
        snapshot_.connectionLabel = message.trimmed().isEmpty() ? "Could not resolve best contract for " + pendingExchange_ + ":" + requestedRootSymbol_ + "." : message;
        emitSnapshot();
        return;
    }

    pendingSymbol_ = symbol.trimmed();
    if (!exchange.trimmed().isEmpty()) {
        pendingExchange_ = exchange.trimmed();
    }
    snapshot_.symbol = pendingExchange_ + ":" + pendingSymbol_;
    snapshot_.position.symbol = snapshot_.symbol;
    snapshot_.connectionLabel = "Selected best contract " + snapshot_.symbol + ".";
    restoreCachedCandles();
    subscribePendingSymbol();
    startChartBackfill();
    emitSnapshot();
}

void RithmicMarketDataAdapter::resolvePendingSymbol()
{
    if (!protocol_ || !connectionStarted_ || !protocol_->isConnected() || pendingSymbol_.isEmpty() || pendingExchange_.isEmpty()) {
        return;
    }

    if (looksLikeDatedContract(pendingSymbol_)) {
        snapshot_.symbol = pendingExchange_ + ":" + pendingSymbol_;
        snapshot_.position.symbol = snapshot_.symbol;
        restoreCachedCandles();
        subscribePendingSymbol();
        startChartBackfill();
        return;
    }

    const QString localContract = localFrontMonthContract(pendingExchange_, pendingSymbol_);
    if (!localContract.isEmpty()) {
        requestedRootSymbol_ = pendingSymbol_;
        pendingSymbol_ = localContract;
        snapshot_.symbol = pendingExchange_ + ":" + pendingSymbol_;
        snapshot_.position.symbol = snapshot_.symbol;
        snapshot_.connectionLabel = "Selected local best contract " + snapshot_.symbol + ".";
        restoreCachedCandles();
        subscribePendingSymbol();
        startChartBackfill();
        emitSnapshot();
        return;
    }

    if (resolvingFrontMonth_) {
        return;
    }

    resolvingFrontMonth_ = true;
    requestedRootSymbol_ = pendingSymbol_;
    snapshot_.connectionLabel = "Resolving best contract for " + pendingExchange_ + ":" + requestedRootSymbol_ + "...";
    protocol_->requestFrontMonthContract(requestedRootSymbol_, pendingExchange_);
}

void RithmicMarketDataAdapter::subscribePendingSymbol()
{
    if (!protocol_ || !connectionStarted_ || !protocol_->isConnected() || pendingSymbol_.isEmpty() || pendingExchange_.isEmpty()) {
        return;
    }
    protocol_->subscribeMarketData(pendingSymbol_, pendingExchange_, kTickerUpdateBits);
}

void RithmicMarketDataAdapter::startChartBackfill()
{
    if (!protocol_ || !connectionStarted_ || !protocol_->isConnected() || pendingSymbol_.isEmpty() || pendingExchange_.isEmpty()) {
        return;
    }
    if (resolvingFrontMonth_) {
        return;
    }

    const QString key = cacheKey(pendingExchange_, pendingSymbol_);
    const QDateTime refreshedAt = candleCacheRefreshedAt_.value(key);
    if (!candleCache_.value(key).isEmpty() && refreshedAt.isValid() && refreshedAt.secsTo(QDateTime::currentDateTimeUtc()) < kCacheFreshSeconds) {
        snapshot_.buildingChartData = false;
        snapshot_.chartDataLabel.clear();
        snapshot_.connectionLabel = "Using cached chart data for " + pendingExchange_ + ":" + pendingSymbol_ + ".";
        emitSnapshot();
        return;
    }

    snapshot_.buildingChartData = true;
    snapshot_.chartBarsLoaded = 0;
    snapshot_.chartBarsExpected = kBackfillBars;
    snapshot_.chartDataLabel = "Building chart data";
    protocol_->requestTimeBarBackfill(pendingSymbol_, pendingExchange_, kBackfillMinutes, kBackfillBars);
}

void RithmicMarketDataAdapter::restoreCachedCandles()
{
    const QString key = cacheKey(pendingExchange_, pendingSymbol_);
    const auto cached = candleCache_.value(key);
    if (cached.isEmpty()) {
        return;
    }

    snapshot_.candles = cached;
    snapshot_.volumeProfile.clear();
    for (const auto& candle : snapshot_.candles) {
        updateProfileFromCandle(candle);
    }
    snapshot_.chartBarsLoaded = cached.size();
    snapshot_.chartBarsExpected = std::max(static_cast<int>(cached.size()), kBackfillBars);
    if (!snapshot_.candles.isEmpty()) {
        const auto& last = snapshot_.candles.last();
        snapshot_.last = last.close;
        snapshot_.open = snapshot_.candles.first().open;
        snapshot_.high = last.high;
        snapshot_.low = last.low;
        snapshot_.close = last.close;
        snapshot_.exchangeTime = last.time;
        for (const auto& candle : snapshot_.candles) {
            snapshot_.high = std::max(snapshot_.high, candle.high);
            snapshot_.low = std::min(snapshot_.low, candle.low);
        }
    }
}

void RithmicMarketDataAdapter::mergeCandle(const Candle& candle)
{
    const Candle normalized = normalizedCandle(candle);
    if (!isValidCandle(normalized)) {
        return;
    }

    const qint64 candleSeconds = normalized.time.toSecsSinceEpoch();
    auto it = std::lower_bound(snapshot_.candles.begin(), snapshot_.candles.end(), candleSeconds, [](const Candle& existing, qint64 time) {
        return existing.time.toSecsSinceEpoch() < time;
    });
    if (it != snapshot_.candles.end() && it->time.toSecsSinceEpoch() == candleSeconds) {
        *it = normalized;
    } else {
        snapshot_.candles.insert(it, normalized);
    }

    while (snapshot_.candles.size() > kMaxCandles) {
        snapshot_.candles.removeFirst();
    }

    if (!pendingSymbol_.isEmpty() && !pendingExchange_.isEmpty()) {
        const QString key = cacheKey(pendingExchange_, pendingSymbol_);
        candleCache_[key] = snapshot_.candles;
        candleCacheRefreshedAt_[key] = QDateTime::currentDateTimeUtc();
    }
}

void RithmicMarketDataAdapter::upsertCandle(const RithmicTradeTick& trade)
{
    if (!isFinitePositive(trade.price) || trade.size <= 0) {
        return;
    }

    const QDateTime tradeTime = trade.time.isValid() ? trade.time : QDateTime::currentDateTimeUtc();
    const qint64 seconds = tradeTime.toSecsSinceEpoch();
    const qint64 bucketSeconds = seconds - (seconds % kCandleSeconds);
    const QDateTime bucketTime = QDateTime::fromSecsSinceEpoch(bucketSeconds, QTimeZone::UTC);
    const double signedVolume = trade.aggressor == AggressorSide::Sell ? -trade.size : trade.size;

    const Candle liveCandle{
            bucketTime,
            trade.price,
            trade.price,
            trade.price,
            trade.price,
            static_cast<double>(trade.size),
            signedVolume
    };

    if (snapshot_.candles.isEmpty() || snapshot_.candles.last().time.toSecsSinceEpoch() < bucketSeconds) {
        snapshot_.candles.push_back(liveCandle);
    } else if (snapshot_.candles.last().time.toSecsSinceEpoch() == bucketSeconds) {
        auto& candle = snapshot_.candles.last();
        candle.high = std::max(candle.high, trade.price);
        candle.low = std::min(candle.low, trade.price);
        candle.close = trade.price;
        candle.volume += trade.size;
        candle.delta += signedVolume;
    } else {
        auto it = std::lower_bound(snapshot_.candles.begin(), snapshot_.candles.end(), bucketSeconds, [](const Candle& existing, qint64 time) {
            return existing.time.toSecsSinceEpoch() < time;
        });
        if (it != snapshot_.candles.end() && it->time.toSecsSinceEpoch() == bucketSeconds) {
            it->high = std::max(it->high, trade.price);
            it->low = std::min(it->low, trade.price);
            it->close = trade.price;
            it->volume += trade.size;
            it->delta += signedVolume;
        } else {
            snapshot_.candles.insert(it, liveCandle);
        }
    }

    while (snapshot_.candles.size() > kMaxCandles) {
        snapshot_.candles.removeFirst();
    }

    if (!pendingSymbol_.isEmpty() && !pendingExchange_.isEmpty()) {
        const QString key = cacheKey(pendingExchange_, pendingSymbol_);
        candleCache_[key] = snapshot_.candles;
        candleCacheRefreshedAt_[key] = QDateTime::currentDateTimeUtc();
    }
}

void RithmicMarketDataAdapter::updateProfileFromTrade(const RithmicTradeTick& trade)
{
    if (!isFinitePositive(trade.price) || trade.size <= 0) {
        return;
    }

    const double tick = tickSizeForSymbol(trade.exchange.isEmpty() ? pendingExchange_ : trade.exchange, trade.symbol.isEmpty() ? pendingSymbol_ : trade.symbol);
    const double price = roundToTick(trade.price, tick);
    const double signedVolume = trade.aggressor == AggressorSide::Sell ? -trade.size : trade.size;
    for (auto& bin : snapshot_.volumeProfile) {
        if (std::abs(bin.price - price) < tick * 0.25) {
            bin.volume += trade.size;
            bin.delta += signedVolume;
            recomputeProfileFlags();
            return;
        }
    }

    snapshot_.volumeProfile.push_back({price, static_cast<double>(trade.size), signedVolume, false, false, false, false});
    std::sort(snapshot_.volumeProfile.begin(), snapshot_.volumeProfile.end(), [](const ProfileBin& left, const ProfileBin& right) {
        return left.price > right.price;
    });
    while (snapshot_.volumeProfile.size() > kMaxProfileBins) {
        snapshot_.volumeProfile.removeLast();
    }
    recomputeProfileFlags();
}

void RithmicMarketDataAdapter::updateProfileFromCandle(const Candle& candle)
{
    if (!isValidCandle(candle) || candle.volume <= 0.0) {
        return;
    }

    const double tick = tickSizeForSymbol(pendingExchange_, pendingSymbol_);
    const double price = roundToTick(candle.close, tick);
    const double signedVolume = candle.delta == 0.0 ? (candle.close >= candle.open ? candle.volume * 0.35 : -candle.volume * 0.35) : candle.delta;
    for (auto& bin : snapshot_.volumeProfile) {
        if (std::abs(bin.price - price) < tick * 0.25) {
            bin.volume += candle.volume;
            bin.delta += signedVolume;
            recomputeProfileFlags();
            return;
        }
    }

    snapshot_.volumeProfile.push_back({price, candle.volume, signedVolume, false, false, false, false});
    std::sort(snapshot_.volumeProfile.begin(), snapshot_.volumeProfile.end(), [](const ProfileBin& left, const ProfileBin& right) {
        return left.price > right.price;
    });
    while (snapshot_.volumeProfile.size() > kMaxProfileBins) {
        snapshot_.volumeProfile.removeLast();
    }
    recomputeProfileFlags();
}

void RithmicMarketDataAdapter::recomputeProfileFlags()
{
    if (snapshot_.volumeProfile.isEmpty()) {
        return;
    }

    double totalVolume = 0.0;
    double maxVolume = 0.0;
    int pocIndex = 0;
    for (int i = 0; i < snapshot_.volumeProfile.size(); ++i) {
        auto& bin = snapshot_.volumeProfile[i];
        bin.valueAreaHigh = false;
        bin.valueAreaLow = false;
        bin.pointOfControl = false;
        bin.lowVolumeNode = false;
        totalVolume += std::max(bin.volume, 0.0);
        if (bin.volume > maxVolume) {
            maxVolume = bin.volume;
            pocIndex = i;
        }
    }

    if (maxVolume <= 0.0) {
        return;
    }

    snapshot_.volumeProfile[pocIndex].pointOfControl = true;
    for (auto& bin : snapshot_.volumeProfile) {
        bin.lowVolumeNode = bin.volume <= maxVolume * 0.18;
    }

    double covered = snapshot_.volumeProfile[pocIndex].volume;
    int lowIndex = pocIndex;
    int highIndex = pocIndex;
    const double target = totalVolume * 0.70;
    while (covered < target && (lowIndex + 1 < snapshot_.volumeProfile.size() || highIndex > 0)) {
        const double lowerVolume = lowIndex + 1 < snapshot_.volumeProfile.size() ? snapshot_.volumeProfile[lowIndex + 1].volume : -1.0;
        const double higherVolume = highIndex > 0 ? snapshot_.volumeProfile[highIndex - 1].volume : -1.0;
        if (higherVolume >= lowerVolume && highIndex > 0) {
            --highIndex;
            covered += std::max(snapshot_.volumeProfile[highIndex].volume, 0.0);
        } else if (lowIndex + 1 < snapshot_.volumeProfile.size()) {
            ++lowIndex;
            covered += std::max(snapshot_.volumeProfile[lowIndex].volume, 0.0);
        } else {
            break;
        }
    }

    snapshot_.volumeProfile[highIndex].valueAreaHigh = true;
    snapshot_.volumeProfile[lowIndex].valueAreaLow = true;
}

void RithmicMarketDataAdapter::emitSnapshot()
{
    for (int i = handlers_.size() - 1; i >= 0; --i) {
        const auto& listener = handlers_.at(i);
        if (listener.hasContext && listener.context.isNull()) {
            handlers_.removeAt(i);
            continue;
        }
        if (listener.handler) {
            listener.handler(snapshot_);
        }
    }
}

} // namespace tc
