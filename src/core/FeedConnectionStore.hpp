#pragma once

#include "core/FeedConnection.hpp"

#include <QtCore/QString>
#include <QtCore/QVector>

namespace tc {

class FeedConnectionStore {
public:
    QVector<FeedConnection> load(QString* errorMessage = nullptr) const;
    bool save(const QVector<FeedConnection>& connections, QString* errorMessage = nullptr) const;
    QString storageDescription() const;
};

} // namespace tc
