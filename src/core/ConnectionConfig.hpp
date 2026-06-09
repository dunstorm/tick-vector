#pragma once

#include <QtCore/QString>
#include <QtCore/QStringList>

namespace tc {

struct ConnectionConfig {
    QString username;
    QString password;
    QString system;
    QString gateway;
    QString appName{"TradingClient"};
    QString account;
    bool useRealRithmic{false};

    static ConnectionConfig fromEnvironment();
    QStringList missingRequiredFields() const;
    QString maskedUsername() const;
};

} // namespace tc
