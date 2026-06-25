#include "core/MacKeychain.hpp"

#include "app/AppConstants.hpp"

#include <QtCore/QtGlobal>

#ifdef Q_OS_MACOS
#include <CoreFoundation/CoreFoundation.h>
#include <Security/Security.h>
#endif

namespace tc::mac_keychain {

#ifdef Q_OS_MACOS
namespace {

CFStringRef makeCfString(const char* value)
{
    return CFStringCreateWithCString(kCFAllocatorDefault, value, kCFStringEncodingUTF8);
}

QString cfStringToQString(CFStringRef value)
{
    if (!value) {
        return {};
    }

    const CFIndex length = CFStringGetLength(value);
    const CFIndex maxSize = CFStringGetMaximumSizeForEncoding(length, kCFStringEncodingUTF8) + 1;
    QByteArray buffer(static_cast<qsizetype>(maxSize), Qt::Uninitialized);
    if (!CFStringGetCString(value, buffer.data(), maxSize, kCFStringEncodingUTF8)) {
        return {};
    }
    return QString::fromUtf8(buffer.constData());
}

QString keychainError(OSStatus status)
{
    CFStringRef message = SecCopyErrorMessageString(status, nullptr);
    const QString text = cfStringToQString(message);
    if (message) {
        CFRelease(message);
    }
    return text.isEmpty() ? QString("Keychain error %1").arg(status) : text;
}

CFMutableDictionaryRef makeQuery(const char* serviceName, const char* accountName)
{
    CFMutableDictionaryRef query = CFDictionaryCreateMutable(
        kCFAllocatorDefault,
        0,
        &kCFTypeDictionaryKeyCallBacks,
        &kCFTypeDictionaryValueCallBacks);

    CFStringRef service = makeCfString(serviceName);
    CFStringRef account = makeCfString(accountName);
    CFDictionarySetValue(query, kSecClass, kSecClassGenericPassword);
    CFDictionarySetValue(query, kSecAttrService, service);
    CFDictionarySetValue(query, kSecAttrAccount, account);
    CFRelease(service);
    CFRelease(account);
    return query;
}

#if defined(TC_KEYCHAIN_ALLOW_ANY_APP)

bool allowAnyAppForAuthorization(SecAccessRef access, CFTypeRef authorization, CFStringRef fallbackDescription, bool required, QString* errorMessage)
{
#if defined(__clang__)
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wdeprecated-declarations"
#endif
    CFArrayRef aclList = SecAccessCopyMatchingACLList(access, authorization);
    if (!aclList || CFArrayGetCount(aclList) == 0) {
        if (aclList) {
            CFRelease(aclList);
        }
        if (required && errorMessage) {
            *errorMessage = "Could not find the Keychain credential ACL to update.";
        }
        return !required;
    }

    bool ok = true;
    for (CFIndex index = 0; index < CFArrayGetCount(aclList); ++index) {
        auto* acl = static_cast<SecACLRef>(const_cast<void*>(CFArrayGetValueAtIndex(aclList, index)));
        CFArrayRef applicationList = nullptr;
        CFStringRef description = nullptr;
        SecKeychainPromptSelector promptSelector = 0;
        OSStatus status = SecACLCopyContents(acl, &applicationList, &description, &promptSelector);
        if (status == errSecSuccess) {
            status = SecACLSetContents(acl, nullptr, description ? description : fallbackDescription, promptSelector);
        }
        if (applicationList) {
            CFRelease(applicationList);
        }
        if (description) {
            CFRelease(description);
        }
        if (status != errSecSuccess) {
            ok = false;
            if (errorMessage) {
                *errorMessage = keychainError(status);
            }
            break;
        }
    }
    CFRelease(aclList);
#if defined(__clang__)
#pragma clang diagnostic pop
#endif
    return ok;
}

SecAccessRef makeCredentialAccess(const char* serviceName, const char* accountName, QString* errorMessage)
{
    const QByteArray descriptorText = QByteArray(app::kDisplayName) + " " + serviceName + ":" + accountName;
    CFStringRef descriptor = CFStringCreateWithCString(kCFAllocatorDefault, descriptorText.constData(), kCFStringEncodingUTF8);
    SecAccessRef access = nullptr;

#if defined(__clang__)
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wdeprecated-declarations"
#endif
    OSStatus status = SecAccessCreate(descriptor, nullptr, &access);
#if defined(__clang__)
#pragma clang diagnostic pop
#endif
    if (status != errSecSuccess) {
        if (errorMessage) {
            *errorMessage = keychainError(status);
        }
        CFRelease(descriptor);
        return nullptr;
    }

    // Development builds are unsigned and their binary hash changes on rebuild.
    // Setting these ACLs to nullptr is the Security.framework equivalent of the
    // `security add-generic-password -A` behavior: encrypted in Keychain, but
    // not bound to this specific binary image.
    if (!allowAnyAppForAuthorization(access, kSecACLAuthorizationDecrypt, descriptor, true, errorMessage)
        || !allowAnyAppForAuthorization(access, kSecACLAuthorizationKeychainItemRead, descriptor, false, errorMessage)
        || !allowAnyAppForAuthorization(access, kSecACLAuthorizationKeychainItemModify, descriptor, false, errorMessage)) {
        CFRelease(access);
        CFRelease(descriptor);
        return nullptr;
    }

    CFRelease(descriptor);
    return access;
}

#else

SecAccessRef makeCredentialAccess(const char*, const char*, QString*)
{
    return nullptr;
}

#endif

} // namespace
#endif

LoadResult loadGenericPassword(const char* service, const char* account)
{
#ifdef Q_OS_MACOS
    CFMutableDictionaryRef query = makeQuery(service, account);
    CFDictionarySetValue(query, kSecReturnData, kCFBooleanTrue);
    CFDictionarySetValue(query, kSecMatchLimit, kSecMatchLimitOne);

    CFTypeRef result = nullptr;
    const OSStatus status = SecItemCopyMatching(query, &result);
    CFRelease(query);

    if (status == errSecItemNotFound) {
        return {};
    }
    if (status != errSecSuccess) {
        return {LoadStatus::Error, {}, keychainError(status)};
    }

    auto* data = static_cast<CFDataRef>(result);
    QByteArray payload(
        reinterpret_cast<const char*>(CFDataGetBytePtr(data)),
        static_cast<qsizetype>(CFDataGetLength(data)));
    CFRelease(result);
    return {LoadStatus::Found, payload, {}};
#else
    Q_UNUSED(service);
    Q_UNUSED(account);
    return {};
#endif
}

bool saveGenericPassword(const char* service, const char* account, const QByteArray& payload, QString* errorMessage)
{
#ifdef Q_OS_MACOS
    CFMutableDictionaryRef query = makeQuery(service, account);
    CFDataRef data = CFDataCreate(
        kCFAllocatorDefault,
        reinterpret_cast<const UInt8*>(payload.constData()),
        static_cast<CFIndex>(payload.size()));

    QString accessError;
    SecAccessRef access = makeCredentialAccess(service, account, &accessError);
#if defined(TC_KEYCHAIN_ALLOW_ANY_APP)
    if (!access) {
        if (errorMessage) {
            *errorMessage = accessError.isEmpty() ? "Could not create the development Keychain access policy." : accessError;
        }
        CFRelease(data);
        CFRelease(query);
        return false;
    }
#endif

    CFMutableDictionaryRef attributes = CFDictionaryCreateMutable(
        kCFAllocatorDefault,
        0,
        &kCFTypeDictionaryKeyCallBacks,
        &kCFTypeDictionaryValueCallBacks);
    CFDictionarySetValue(attributes, kSecValueData, data);
    if (access) {
        CFDictionarySetValue(attributes, kSecAttrAccess, access);
    }

    OSStatus status = SecItemUpdate(query, attributes);
    if (status == errSecItemNotFound) {
        CFDictionarySetValue(query, kSecValueData, data);
        if (access) {
            CFDictionarySetValue(query, kSecAttrAccess, access);
        }
        status = SecItemAdd(query, nullptr);
    }

    CFRelease(attributes);
    if (access) {
        CFRelease(access);
    }
    CFRelease(data);
    CFRelease(query);

    if (status == errSecSuccess) {
        return true;
    }

    if (errorMessage) {
        *errorMessage = keychainError(status);
    }
    return false;
#else
    Q_UNUSED(service);
    Q_UNUSED(account);
    Q_UNUSED(payload);
    if (errorMessage) {
        *errorMessage = "Secure credential storage is not configured for the current platform.";
    }
    return false;
#endif
}

QString storageDescription()
{
#ifdef Q_OS_MACOS
#if defined(TC_KEYCHAIN_ALLOW_ANY_APP)
    return "Credentials are stored in macOS Keychain with a development access policy that survives rebuilds.";
#else
    return "Credentials are stored in macOS Keychain and restricted to this signed app identity.";
#endif
#else
    return "Secure credential storage is not configured for the current platform.";
#endif
}

} // namespace tc::mac_keychain
