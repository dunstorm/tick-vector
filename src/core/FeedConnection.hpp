#pragma once

#include "core/ConnectionConfig.hpp"

#include <QtCore/QString>

namespace tc {

struct FeedConnection {
    QString id;
    QString name;
    QString workspaceName{"Lucid Trading"};
    QString workspaceLocation{"Local"};
    QString feedSource{"Rithmic"};
    QString gateway;
    QString server;
    QString system;
    QString marketData{"Non Aggregated"};
    QString account;
    QString username;
    QString password;
    QString appName{"TradingClient"};
    bool useDemoCredentials{false};
    bool connectOnStartup{false};

    bool isComplete() const
    {
        return !name.trimmed().isEmpty()
            && !username.trimmed().isEmpty()
            && !password.isEmpty()
            && !gateway.trimmed().isEmpty()
            && !system.trimmed().isEmpty();
    }

    ConnectionConfig toConnectionConfig() const
    {
        ConnectionConfig config;
        config.username = username;
        config.password = password;
        config.system = system;
        config.gateway = gateway.isEmpty() ? server : gateway;
        config.account = account;
        config.appName = appName.trimmed().isEmpty() ? "TradingClient" : appName.trimmed();
        config.useRealRithmic = feedSource.compare("Rithmic", Qt::CaseInsensitive) == 0;
        return config;
    }
};

} // namespace tc
