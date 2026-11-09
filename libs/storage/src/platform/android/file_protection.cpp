// SPDX-License-Identifier: GPL-3.0-or-later
// Android encrypts the app's internal storage per file (credential-encrypted storage, readable after the first unlock),
// with no call to make. Until Stage 9 package 9.5 reports that class, this behaves as the Linux stub: it applies nothing
// and reads back UNSUPPORTED (docs/plan/stage-9.md, "File protection and storage location").
#include "../file_protection.hpp"

namespace ej::platform
{

namespace
{

class AndroidFileProtection final : public FileProtection
{
public:
    Result<void, StorageError> apply(const std::filesystem::path&, ProtectionClass) override
    {
        return {};  // the system protects every file in the app's storage
    }

    Result<ProtectionClass, StorageError> read_back(const std::filesystem::path&) override
    {
        return ProtectionClass::UNSUPPORTED;
    }
};

}  // namespace

FileProtection& system_file_protection()
{
    static AndroidFileProtection instance;
    return instance;
}

}  // namespace ej::platform
