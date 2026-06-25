#include "core/ConnectionConfig.hpp"

#include "app/AppConstants.hpp"

#include <QtCore/qtenvironmentvariables.h>

namespace tc {

ConnectionConfig ConnectionConfig::fromEnvironment()
{
    ConnectionConfig config;
    config.username = qEnvironmentVariable("RITHMIC_USER");
    config.password = qEnvironmentVariable("RITHMIC_PASSWORD");
    config.system = qEnvironmentVariable("RITHMIC_SYSTEM");
    config.gateway = qEnvironmentVariable("RITHMIC_GATEWAY");
    config.account = qEnvironmentVariable("RITHMIC_ACCOUNT");
    const QString appName = qEnvironmentVariable("RITHMIC_APP_NAME");
    if (!appName.isEmpty()) {
        config.appName = appName;
    }
    const QString appVersion = qEnvironmentVariable("RITHMIC_APP_VERSION");
    if (!appVersion.isEmpty()) {
        config.appVersion = appVersion;
    }
    config.useRealRithmic = qEnvironmentVariableIntValue(app::kUseRithmicEnv) == 1
        || qEnvironmentVariableIntValue(app::kLegacyUseRithmicEnv) == 1;
    return config;
}

QStringList ConnectionConfig::missingRequiredFields() const
{
    QStringList missing;
    if (username.isEmpty()) missing << "RITHMIC_USER";
    if (password.isEmpty()) missing << "RITHMIC_PASSWORD";
    if (system.isEmpty()) missing << "RITHMIC_SYSTEM";
    if (gateway.isEmpty()) missing << "RITHMIC_GATEWAY";
    if (account.isEmpty()) missing << "RITHMIC_ACCOUNT";
    return missing;
}

QString ConnectionConfig::maskedUsername() const
{
    if (username.size() <= 2) {
        return username.isEmpty() ? "not set" : username.left(1) + "***";
    }
    return username.left(2) + "***" + username.right(1);
}

} // namespace tc
