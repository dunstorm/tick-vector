#include "core/ChartDataCache.hpp"

#include "app/AppConstants.hpp"

#include <QtCore/QDir>
#include <QtCore/QFile>
#include <QtCore/QFileInfo>
#include <QtCore/QJsonArray>
#include <QtCore/QJsonDocument>
#include <QtCore/QJsonObject>
#include <QtCore/QRegularExpression>
#include <QtCore/QSaveFile>
#include <QtCore/QStandardPaths>
#include <QtCore/QTimeZone>
#include <algorithm>
#include <cmath>
#include <utility>

namespace tc {

namespace {

constexpr int kCacheVersion = 2;
constexpr auto kCacheDirectoryName = "chart-cache";

QString normalizedText(const QString& value)
{
    return value.trimmed().toUpper();
}

QString cacheToken(const QString& value)
{
    QString token = normalizedText(value);
    static const QRegularExpression invalidCharacters("[^A-Z0-9._-]+");
    token.replace(invalidCharacters, "_");
    if (token.isEmpty()) {
        token = "UNKNOWN";
    }
    return token.left(64);
}

QString cacheFileName(const QString& exchange, const QString& symbol, int barMinutes, int lookbackDays)
{
    return cacheToken(exchange) + "_" + cacheToken(symbol) + "_" + QString::number(std::max(barMinutes, 1)) + "m_"
        + QString::number(std::max(lookbackDays, 1)) + "d.json";
}

bool isFinitePositive(double value)
{
    return std::isfinite(value) && value > 0.0;
}

bool isCacheableCandle(const Candle& candle)
{
    return candle.time.isValid()
        && isFinitePositive(candle.open)
        && isFinitePositive(candle.high)
        && isFinitePositive(candle.low)
        && isFinitePositive(candle.close)
        && candle.high >= candle.low;
}

QJsonArray candleToJson(const Candle& candle)
{
    QJsonArray values;
    values.append(static_cast<double>(candle.time.toMSecsSinceEpoch()));
    values.append(candle.open);
    values.append(candle.high);
    values.append(candle.low);
    values.append(candle.close);
    values.append(candle.volume);
    values.append(candle.delta);
    return values;
}

Candle candleFromJson(const QJsonArray& values)
{
    if (values.size() < 7) {
        return {};
    }

    const qint64 timestampMs = static_cast<qint64>(values.at(0).toDouble());
    return {
        QDateTime::fromMSecsSinceEpoch(timestampMs, QTimeZone::UTC),
        values.at(1).toDouble(),
        values.at(2).toDouble(),
        values.at(3).toDouble(),
        values.at(4).toDouble(),
        values.at(5).toDouble(),
        values.at(6).toDouble(),
    };
}

QVector<Candle> normalizedCandles(QVector<Candle> candles)
{
    candles.erase(std::remove_if(candles.begin(), candles.end(), [](const Candle& candle) {
        return !isCacheableCandle(candle);
    }), candles.end());
    std::sort(candles.begin(), candles.end(), [](const Candle& left, const Candle& right) {
        return left.time < right.time;
    });

    QVector<Candle> deduped;
    deduped.reserve(candles.size());
    for (const auto& candle : candles) {
        if (!deduped.isEmpty() && deduped.last().time.toMSecsSinceEpoch() == candle.time.toMSecsSinceEpoch()) {
            deduped.last() = candle;
            continue;
        }
        deduped.push_back(candle);
    }
    return deduped;
}

QString cachePath(const QString& directory, const QString& exchange, const QString& symbol, int barMinutes, int lookbackDays)
{
    return QDir(directory).filePath(cacheFileName(exchange, symbol, barMinutes, lookbackDays));
}

} // namespace

ChartDataCache::ChartDataCache(QString directory)
    : directory_(directory.trimmed().isEmpty() ? defaultDirectory() : std::move(directory))
{
}

ChartDataCacheEntry ChartDataCache::load(const QString& exchange, const QString& symbol, int barMinutes, int lookbackDays, QString* errorMessage) const
{
    ChartDataCacheEntry entry;
    entry.barMinutes = std::max(barMinutes, 1);
    entry.lookbackDays = std::max(lookbackDays, 1);

    const QString path = cachePath(directory_, exchange, symbol, entry.barMinutes, entry.lookbackDays);
    QFile file(path);
    if (!file.exists()) {
        return entry;
    }
    if (!file.open(QIODevice::ReadOnly)) {
        if (errorMessage) {
            *errorMessage = "Could not read chart cache: " + file.errorString();
        }
        return {};
    }

    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(file.readAll(), &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isObject()) {
        if (errorMessage) {
            *errorMessage = "Could not parse chart cache: " + parseError.errorString();
        }
        return {};
    }

    const QJsonObject root = document.object();
    if (root.value("version").toInt() != kCacheVersion
        || normalizedText(root.value("exchange").toString()) != normalizedText(exchange)
        || normalizedText(root.value("symbol").toString()) != normalizedText(symbol)
        || root.value("barMinutes").toInt() != entry.barMinutes
        || root.value("lookbackDays").toInt() != entry.lookbackDays) {
        return entry;
    }

    entry.refreshedAt = QDateTime::fromString(root.value("refreshedAt").toString(), Qt::ISODateWithMs);
    if (!entry.refreshedAt.isValid()) {
        entry.refreshedAt = QFileInfo(file).lastModified().toUTC();
    }
    entry.refreshedAt = entry.refreshedAt.toUTC();

    QVector<Candle> candles;
    const QJsonArray candleItems = root.value("candles").toArray();
    candles.reserve(candleItems.size());
    for (const auto& item : candleItems) {
        const Candle candle = candleFromJson(item.toArray());
        if (isCacheableCandle(candle)) {
            candles.push_back(candle);
        }
    }
    entry.candles = normalizedCandles(std::move(candles));
    return entry;
}

bool ChartDataCache::save(const QString& exchange, const QString& symbol, int barMinutes, int lookbackDays, const QVector<Candle>& candles, const QDateTime& refreshedAt, QString* errorMessage) const
{
    const int normalizedBarMinutes = std::max(barMinutes, 1);
    const int normalizedLookbackDays = std::max(lookbackDays, 1);
    const QVector<Candle> normalized = normalizedCandles(candles);
    if (normalized.isEmpty()) {
        return true;
    }

    QDir directory(directory_);
    if (!directory.exists() && !directory.mkpath(".")) {
        if (errorMessage) {
            *errorMessage = "Could not create chart cache directory: " + directory.absolutePath();
        }
        return false;
    }

    QJsonArray candleItems;
    for (const auto& candle : normalized) {
        candleItems.append(candleToJson(candle));
    }

    const QDateTime savedAt = (refreshedAt.isValid() ? refreshedAt : QDateTime::currentDateTimeUtc()).toUTC();
    QJsonObject root;
    root["version"] = kCacheVersion;
    root["exchange"] = normalizedText(exchange);
    root["symbol"] = normalizedText(symbol);
    root["barMinutes"] = normalizedBarMinutes;
    root["lookbackDays"] = normalizedLookbackDays;
    root["refreshedAt"] = savedAt.toString(Qt::ISODateWithMs);
    root["candles"] = candleItems;

    const QByteArray payload = QJsonDocument(root).toJson(QJsonDocument::Compact);
    QSaveFile file(cachePath(directory_, exchange, symbol, normalizedBarMinutes, normalizedLookbackDays));
    if (!file.open(QIODevice::WriteOnly)) {
        if (errorMessage) {
            *errorMessage = "Could not write chart cache: " + file.errorString();
        }
        return false;
    }
    if (file.write(payload) != payload.size()) {
        if (errorMessage) {
            *errorMessage = "Could not write chart cache completely.";
        }
        return false;
    }
    if (!file.commit()) {
        if (errorMessage) {
            *errorMessage = "Could not save chart cache: " + file.errorString();
        }
        return false;
    }
    return true;
}

QString ChartDataCache::directory() const
{
    return directory_;
}

QString ChartDataCache::defaultDirectory()
{
    QString directory = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    if (directory.isEmpty()) {
        directory = QDir::home().filePath(app::kDataDirectoryFallback);
    }
    return QDir(directory).filePath(kCacheDirectoryName);
}

} // namespace tc
