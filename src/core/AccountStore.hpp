#pragma once

#include "core/ConnectionConfig.hpp"

#include <QtCore/QString>

namespace tc {

class AccountStore {
public:
    ConnectionConfig load() const;
    bool save(const ConnectionConfig& config, QString* errorMessage = nullptr) const;
    QString passwordStorageDescription() const;

private:
    bool loadSecureProfile(ConnectionConfig* config, QString* errorMessage = nullptr) const;
    bool saveSecureProfile(const ConnectionConfig& config, QString* errorMessage = nullptr) const;
};

} // namespace tc
