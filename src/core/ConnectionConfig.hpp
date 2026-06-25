#pragma once

#include "app/AppConstants.hpp"

#include <QtCore/QString>
#include <QtCore/QStringList>

namespace tc {

struct ConnectionConfig {
    QString username;
    QString password;
    QString system;
    QString gateway;
    QString appName{app::kRithmicAppName};
    QString appVersion{"1.0"};
    QString account;
    bool useRealRithmic{false};

    static ConnectionConfig fromEnvironment();
    QStringList missingRequiredFields() const;
    QString maskedUsername() const;
};

} // namespace tc
