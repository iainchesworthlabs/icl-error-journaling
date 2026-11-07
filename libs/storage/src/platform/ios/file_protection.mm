// SPDX-License-Identifier: GPL-3.0-or-later
// Compiled with -fobjc-arc.
#include "../file_protection.hpp"

#import <Foundation/Foundation.h>

namespace ej::platform
{

namespace
{

NSString* path_string(const std::filesystem::path& path)
{
    return [NSString stringWithUTF8String:path.c_str()];
}

NSFileProtectionType protection_type(ProtectionClass wanted)
{
    switch (wanted)
    {
    case ProtectionClass::COMPLETE:
        return NSFileProtectionComplete;
    case ProtectionClass::COMPLETE_UNLESS_OPEN:
        return NSFileProtectionCompleteUnlessOpen;
    case ProtectionClass::COMPLETE_UNTIL_FIRST_USER_AUTHENTICATION:
        return NSFileProtectionCompleteUntilFirstUserAuthentication;
    case ProtectionClass::NONE:
    case ProtectionClass::OTHER:
    case ProtectionClass::UNSUPPORTED:
        break;
    }
    return NSFileProtectionNone;
}

class IosFileProtection final : public FileProtection
{
public:
    Result<void, StorageError> apply(const std::filesystem::path& path, ProtectionClass wanted) override
    {
        NSError* error = nil;
        const BOOL done = [[NSFileManager defaultManager] setAttributes:@{NSFileProtectionKey : protection_type(wanted)}
                                                           ofItemAtPath:path_string(path)
                                                                  error:&error];
        if (!done)
        {
            return fail(make_error(StorageErrorKind::IO, "the data protection of a file could not be set",
                                   error != nil ? static_cast<int>(error.code) : 0));
        }
        return {};
    }

    Result<ProtectionClass, StorageError> read_back(const std::filesystem::path& path) override
    {
        NSError* error = nil;
        NSDictionary* attributes = [[NSFileManager defaultManager] attributesOfItemAtPath:path_string(path) error:&error];
        if (attributes == nil)
        {
            return fail(make_error(StorageErrorKind::IO, "the data protection of a file could not be read",
                                   error != nil ? static_cast<int>(error.code) : 0));
        }
        NSString* value = attributes[NSFileProtectionKey];
        if (value == nil || [value isEqualToString:NSFileProtectionNone])
        {
            return ProtectionClass::NONE;
        }
        if ([value isEqualToString:NSFileProtectionComplete])
        {
            return ProtectionClass::COMPLETE;
        }
        if ([value isEqualToString:NSFileProtectionCompleteUnlessOpen])
        {
            return ProtectionClass::COMPLETE_UNLESS_OPEN;
        }
        if ([value isEqualToString:NSFileProtectionCompleteUntilFirstUserAuthentication])
        {
            return ProtectionClass::COMPLETE_UNTIL_FIRST_USER_AUTHENTICATION;
        }
        return ProtectionClass::OTHER;
    }
};

}  // namespace

FileProtection& system_file_protection()
{
    static IosFileProtection instance;
    return instance;
}

}  // namespace ej::platform
