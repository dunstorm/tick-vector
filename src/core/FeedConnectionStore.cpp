#include "core/FeedConnectionStore.hpp"

#include "core/MacKeychain.hpp"

#include <QtCore/QJsonArray>
#include <QtCore/QJsonDocument>
#include <QtCore/QJsonObject>
#include <QtCore/QDir>
#include <QtCore/QFile>
#include <QtCore/QSaveFile>
#include <QtCore/QStandardPaths>

namespace tc {

namespace {

constexpr auto kKeychainService = "com.tradingclient.desktop.feed-connections";
constexpr auto kKeychainAccount = "profiles-v1";
constexpr auto kPlaintextFileName = "feed-connections.json";

enum class PlaintextLoadStatus {
    Found,
    NotFound,
    Error,
};

QJsonObject toJson(const FeedConnection& connection)
{
    QJsonObject object;
    object["id"] = connection.id;
    object["name"] = connection.name;
    object["workspaceName"] = connection.workspaceName;
    object["workspaceLocation"] = connection.workspaceLocation;
    object["feedSource"] = connection.feedSource;
    object["gateway"] = connection.gateway;
    object["server"] = connection.server;
    object["system"] = connection.system;
    object["marketData"] = connection.marketData;
    object["account"] = connection.account;
    object["username"] = connection.username;
    object["password"] = connection.password;
    object["appName"] = connection.appName;
    object["useDemoCredentials"] = connection.useDemoCredentials;
    object["connectOnStartup"] = connection.connectOnStartup;
    return object;
}

FeedConnection fromJson(const QJsonObject& object)
{
    FeedConnection connection;
    connection.id = object.value("id").toString();
    connection.name = object.value("name").toString();
    connection.workspaceName = object.value("workspaceName").toString("Lucid Trading");
    connection.workspaceLocation = object.value("workspaceLocation").toString("Local");
    connection.feedSource = object.value("feedSource").toString("Rithmic");
    connection.gateway = object.value("gateway").toString();
    connection.server = object.value("server").toString();
    connection.system = object.value("system").toString();
    connection.marketData = object.value("marketData").toString("Non Aggregated");
    connection.account = object.value("account").toString();
    connection.username = object.value("username").toString();
    connection.password = object.value("password").toString();
    connection.appName = object.value("appName").toString("TradingClient");
    connection.useDemoCredentials = object.value("useDemoCredentials").toBool(false);
    connection.connectOnStartup = object.value("connectOnStartup").toBool(false);
    return connection;
}

QByteArray serializeConnections(const QVector<FeedConnection>& connections)
{
    QJsonArray items;
    for (const auto& connection : connections) {
        items.append(toJson(connection));
    }

    QJsonObject root;
    root["version"] = 1;
    root["connections"] = items;
    return QJsonDocument(root).toJson(QJsonDocument::Compact);
}

QVector<FeedConnection> deserializeConnections(const QByteArray& payload)
{
    QVector<FeedConnection> connections;
    const QJsonDocument document = QJsonDocument::fromJson(payload);
    const QJsonArray items = document.object().value("connections").toArray();
    for (const auto& item : items) {
        connections.push_back(fromJson(item.toObject()));
    }
    return connections;
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
            *errorMessage = "Could not read plaintext connection profile: " + file.errorString();
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
            *errorMessage = "Could not write plaintext connection profile: " + file.errorString();
        }
        return false;
    }
    if (file.write(payload) != payload.size()) {
        if (errorMessage) {
            *errorMessage = "Could not write plaintext connection profile completely.";
        }
        return false;
    }
    if (!file.commit()) {
        if (errorMessage) {
            *errorMessage = "Could not save plaintext connection profile: " + file.errorString();
        }
        return false;
    }
    return true;
}

} // namespace

QVector<FeedConnection> FeedConnectionStore::load(QString* errorMessage) const
{
    QByteArray payload;
    const PlaintextLoadStatus plaintextStatus = loadPlaintext(&payload, errorMessage);
    if (plaintextStatus == PlaintextLoadStatus::Found) {
        return deserializeConnections(payload);
    }
    if (plaintextStatus == PlaintextLoadStatus::Error) {
        return {};
    }

#if defined(TC_ENABLE_KEYCHAIN_BACKUP)
    const auto result = mac_keychain::loadGenericPassword(kKeychainService, kKeychainAccount);
    if (result.status == mac_keychain::LoadStatus::NotFound) {
        return {};
    }
    if (result.status == mac_keychain::LoadStatus::Error) {
        if (errorMessage) {
            *errorMessage = result.error;
        }
        return {};
    }
    return deserializeConnections(result.payload);
#else
    return {};
#endif
}

bool FeedConnectionStore::save(const QVector<FeedConnection>& connections, QString* errorMessage) const
{
    const QByteArray payload = serializeConnections(connections);
    if (!savePlaintext(payload, errorMessage)) {
        return false;
    }

#if defined(TC_ENABLE_KEYCHAIN_BACKUP)
    QString ignoredBackupError;
    mac_keychain::saveGenericPassword(kKeychainService, kKeychainAccount, payload, &ignoredBackupError);
#endif
    return true;
}

QString FeedConnectionStore::storageDescription() const
{
    return "Connections are saved as plaintext JSON at " + plaintextPath() + ". Keychain backup is disabled unless TC_ENABLE_KEYCHAIN_BACKUP=ON.";
}

} // namespace tc
