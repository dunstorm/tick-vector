#pragma once

#include <QtCore/QByteArray>
#include <QtCore/QString>

namespace tc::mac_keychain {

enum class LoadStatus {
    Found,
    NotFound,
    Error,
};

struct LoadResult {
    LoadStatus status{LoadStatus::NotFound};
    QByteArray payload;
    QString error;
};

LoadResult loadGenericPassword(const char* service, const char* account);
bool saveGenericPassword(const char* service, const char* account, const QByteArray& payload, QString* errorMessage = nullptr);
QString storageDescription();

} // namespace tc::mac_keychain
