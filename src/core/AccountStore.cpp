#include "core/AccountStore.hpp"

#include "core/MacKeychain.hpp"

#include <QtCore/QJsonDocument>
#include <QtCore/QJsonObject>
#include <QtCore/QDir>
#include <QtCore/QFile>
#include <QtCore/QSaveFile>
#include <QtCore/QStandardPaths>
#include <QtCore/QtGlobal>
#include <QtCore/qtenvironmentvariables.h>

namespace tc {

namespace {

constexpr auto kKeychainService = "com.tradingclient.desktop.rithmic";
constexpr auto kKeychainAccount = "profile-v1";
constexpr auto kPlaintextFileName = "rithmic-profile.json";

enum class PlaintextLoadStatus {
    Found,
    NotFound,
    Error,
};

QByteArray serializeConfig(const ConnectionConfig& config)
{
    QJsonObject root;
    root["username"] = config.username;
    root["password"] = config.password;
    root["system"] = config.system;
    root["gateway"] = config.gateway;
    root["appName"] = config.appName.trimmed().isEmpty() ? "TradingClient" : config.appName.trimmed();
    root["account"] = config.account;
    root["useRealRithmic"] = config.useRealRithmic;
    return QJsonDocument(root).toJson(QJsonDocument::Compact);
}

ConnectionConfig deserializeConfig(const QByteArray& payload)
{
    ConnectionConfig config;
    const QJsonDocument document = QJsonDocument::fromJson(payload);
    const QJsonObject root = document.object();
    config.username = root.value("username").toString();
    config.password = root.value("password").toString();
    config.system = root.value("system").toString();
    config.gateway = root.value("gateway").toString();
    config.appName = root.value("appName").toString("TradingClient");
    config.account = root.value("account").toString();
    config.useRealRithmic = root.value("useRealRithmic").toBool(false);
    return config;
}

void overlayEnvironment(ConnectionConfig* config)
{
    const ConnectionConfig env = ConnectionConfig::fromEnvironment();
    if (qEnvironmentVariableIsSet("RITHMIC_USER")) config->username = env.username;
    if (qEnvironmentVariableIsSet("RITHMIC_PASSWORD")) config->password = env.password;
    if (qEnvironmentVariableIsSet("RITHMIC_SYSTEM")) config->system = env.system;
    if (qEnvironmentVariableIsSet("RITHMIC_GATEWAY")) config->gateway = env.gateway;
    if (qEnvironmentVariableIsSet("RITHMIC_ACCOUNT")) config->account = env.account;
    if (qEnvironmentVariableIsSet("RITHMIC_APP_NAME")) config->appName = env.appName;
    if (qEnvironmentVariableIsSet("TRADING_CLIENT_USE_RITHMIC")) {
        config->useRealRithmic = env.useRealRithmic;
    }
    if (config->appName.trimmed().isEmpty()) {
        config->appName = "TradingClient";
    }
}

QString appDataDirectory()
{
    QString directory = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    if (directory.isEmpty()) {
        directory = QDir::home().filePath(".trading-client");
    }
    return directory;
}

QString plaintextPath()
{
    return QDir(appDataDirectory()).filePath(kPlaintextFileName);
}

PlaintextLoadStatus loadPlaintext(QByteArray* payload, QString* errorMessage)
{
    QFile file(plaintextPath());
    if (!file.exists()) {
        return PlaintextLoadStatus::NotFound;
    }
    if (!file.open(QIODevice::ReadOnly)) {
        if (errorMessage) {
            *errorMessage = "Could not read plaintext Rithmic profile: " + file.errorString();
        }
        return PlaintextLoadStatus::Error;
    }
    if (payload) {
        *payload = file.readAll();
    }
    return PlaintextLoadStatus::Found;
}

bool savePlaintext(const QByteArray& payload, QString* errorMessage)
{
    QDir directory(appDataDirectory());
    if (!directory.exists() && !directory.mkpath(".")) {
        if (errorMessage) {
            *errorMessage = "Could not create credential directory: " + directory.absolutePath();
        }
        return false;
    }

    QSaveFile file(plaintextPath());
    if (!file.open(QIODevice::WriteOnly)) {
        if (errorMessage) {
            *errorMessage = "Could not write plaintext Rithmic profile: " + file.errorString();
        }
        return false;
    }
    if (file.write(payload) != payload.size()) {
        if (errorMessage) {
            *errorMessage = "Could not write plaintext Rithmic profile completely.";
        }
        return false;
    }
    if (!file.commit()) {
        if (errorMessage) {
            *errorMessage = "Could not save plaintext Rithmic profile: " + file.errorString();
        }
        return false;
    }
    return true;
}

} // namespace

ConnectionConfig AccountStore::load() const
{
    ConnectionConfig config;
    if (!loadSecureProfile(&config)) {
        config = ConnectionConfig::fromEnvironment();
    }

    overlayEnvironment(&config);
    return config;
}

bool AccountStore::save(const ConnectionConfig& config, QString* errorMessage) const
{
    return saveSecureProfile(config, errorMessage);
}

QString AccountStore::passwordStorageDescription() const
{
    return "The Rithmic profile is stored as plaintext JSON at " + plaintextPath() + ". Layout and study state are stored separately in app settings.";
}

bool AccountStore::loadSecureProfile(ConnectionConfig* config, QString* errorMessage) const
{
    if (config) {
        *config = {};
    }

    QByteArray payload;
    const PlaintextLoadStatus plaintextStatus = loadPlaintext(&payload, errorMessage);
    if (plaintextStatus == PlaintextLoadStatus::Found) {
        if (config) {
            *config = deserializeConfig(payload);
        }
        return true;
    }
    if (plaintextStatus == PlaintextLoadStatus::Error) {
        return false;
    }

#if defined(TC_ENABLE_KEYCHAIN_BACKUP)
    const auto result = mac_keychain::loadGenericPassword(kKeychainService, kKeychainAccount);
    if (result.status == mac_keychain::LoadStatus::NotFound) {
        return true;
    }
    if (result.status == mac_keychain::LoadStatus::Error) {
        if (errorMessage) {
            *errorMessage = result.error;
        }
        return false;
    }

    if (config) {
        *config = deserializeConfig(result.payload);
    }
    return true;
#else
    return true;
#endif
}

bool AccountStore::saveSecureProfile(const ConnectionConfig& config, QString* errorMessage) const
{
    const QByteArray payload = serializeConfig(config);
    if (!savePlaintext(payload, errorMessage)) {
        return false;
    }

#if defined(TC_ENABLE_KEYCHAIN_BACKUP)
    QString ignoredBackupError;
    mac_keychain::saveGenericPassword(kKeychainService, kKeychainAccount, payload, &ignoredBackupError);
#endif
    return true;
}

} // namespace tc
