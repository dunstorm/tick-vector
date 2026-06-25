#pragma once

#include "core/Types.hpp"

#include <QtCore/QDateTime>
#include <QtCore/QString>
#include <QtCore/QVector>

namespace tc {

struct ChartDataCacheEntry {
    QVector<Candle> candles;
    QDateTime refreshedAt;
    int lookbackDays{};
    int barMinutes{};
};

class ChartDataCache {
public:
    explicit ChartDataCache(QString directory = {});

    ChartDataCacheEntry load(const QString& exchange, const QString& symbol, int barMinutes, int lookbackDays, QString* errorMessage = nullptr) const;
    bool save(const QString& exchange, const QString& symbol, int barMinutes, int lookbackDays, const QVector<Candle>& candles, const QDateTime& refreshedAt = {}, QString* errorMessage = nullptr) const;

    QString directory() const;
    static QString defaultDirectory();

private:
    QString directory_;
};

} // namespace tc
