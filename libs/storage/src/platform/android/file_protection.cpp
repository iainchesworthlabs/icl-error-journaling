// SPDX-License-Identifier: GPL-3.0-or-later
// Android encrypts the app's internal storage per file: by default it is credential-encrypted storage, readable after the
// person has unlocked the phone once since it started, which is the guarantee of iOS's
// NSFileProtectionCompleteUntilFirstUserAuthentication. There is no call to make, so apply() changes nothing, and
// read_back() reports the class the file's place gives it (android_protection.cpp, tested on the desktop). The application
// is not direct-boot aware, so it never runs before the first unlock and never meets a locked store at launch
// (docs/plan/stage-9.md, "File protection and storage location").
#include "../android_protection.hpp"
#include "../file_protection.hpp"

#include <system_error>

namespace ej::platform
{

namespace
{

class AndroidFileProtection final : public FileProtection
{
public:
    Result<void, StorageError> apply(const std::filesystem::path&, ProtectionClass) override
    {
        return {}; // the system protects every file in the app's storage
    }

    Result<ProtectionClass, StorageError> read_back(const std::filesystem::path& path) override
    {
        std::error_code code;
        if (!std::filesystem::exists(path, code))
        {
            return fail(make_error(StorageErrorKind::IO, "the data protection of a file could not be read", code.value()));
        }
        return android_protection_class(path);
    }
};

} // namespace

FileProtection& system_file_protection()
{
    static AndroidFileProtection instance;
    return instance;
}

} // namespace ej::platform
