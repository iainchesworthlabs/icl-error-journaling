// SPDX-License-Identifier: GPL-3.0-or-later
//
// FileConfigStore on real files in a temporary folder, and SettingsModel on a real storage thread with it (work package
// 3.3). The unit tests of the model use a fake store; these show that the file is replaced as a unit and that a file
// the build cannot read is never lost.
#include "error_journaling/configuration.hpp"
#include "error_journaling/memory_log_repository.hpp"
#include "support/test_environment.hpp"
#include "viewmodel/file_config_store.hpp"
#include "viewmodel/settings_model.hpp"
#include "viewmodel/storage_gateway.hpp"

#include <catch2/catch_test_macros.hpp>

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QSignalSpy>
#include <QStringList>
#include <QTemporaryDir>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <thread>

using namespace ej;
using namespace ej::test;
using namespace ej::viewmodel;
using namespace std::chrono_literals;

namespace
{

std::filesystem::path path_in(const QTemporaryDir& dir, const char* name)
{
    return std::filesystem::path(dir.path().toStdU16String()) / name;
}

std::string read_all(const std::filesystem::path& path)
{
    std::ifstream in(path, std::ios::binary);
    return std::string(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
}

void write_all(const std::filesystem::path& path, const std::string& text)
{
    std::ofstream out(path, std::ios::binary);
    out << text;
}

/// The names of the files in the folder, sorted.
QStringList files_in(const QTemporaryDir& dir)
{
    return QDir(dir.path()).entryList(QDir::Files | QDir::Hidden, QDir::Name);
}

template <typename Done>
bool wait_for(Done&& done)
{
    const auto deadline = std::chrono::steady_clock::now() + 5s;
    while (!done() && std::chrono::steady_clock::now() < deadline)
    {
        QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
        std::this_thread::sleep_for(1ms);
    }
    return done();
}

std::string valid_text()
{
    return export_configuration(sample_configuration());
}

} // namespace

// ---------------------------------------------------------------------------------------------------- FileConfigStore

TEST_CASE("There is no saved configuration until one is saved, and then it reads back as written",
          "[viewmodel][settings][file]")
{
    const QTemporaryDir dir;
    REQUIRE(dir.isValid());
    FileConfigStore store(path_in(dir, "configuration.json"));

    const auto none = store.load();
    REQUIRE(none.has_value());
    CHECK_FALSE(none->has_value());

    REQUIRE(store.save(valid_text()).has_value());
    const auto saved = store.load();
    REQUIRE(saved.has_value());
    REQUIRE(saved->has_value());
    CHECK(**saved == valid_text());

    REQUIRE(store.save("{}").has_value());
    CHECK(**store.load() == "{}");
}

TEST_CASE("The folder of the file is made on the first save", "[viewmodel][settings][file]")
{
    const QTemporaryDir dir;
    FileConfigStore store(path_in(dir, "a") / "b" / "configuration.json");

    REQUIRE(store.save(valid_text()).has_value());

    CHECK(read_all(store.file()) == valid_text());
}

TEST_CASE("A save that stops between the temporary file and the rename leaves the old file whole",
          "[viewmodel][settings][file]")
{
    const QTemporaryDir dir;
    bool stop = false;
    FileConfigStore store(path_in(dir, "configuration.json"),
                          [&stop]() -> std::optional<StorageError>
                          {
                              if (stop)
                              {
                                  return make_error(StorageErrorKind::IO, "the rename failed");
                              }
                              return std::nullopt;
                          });
    REQUIRE(store.save(valid_text()).has_value());

    stop = true;
    const auto failed = store.save("something else entirely");

    REQUIRE_FALSE(failed.has_value());
    CHECK(failed.error().m_Kind == StorageErrorKind::IO);
    CHECK(**store.load() == valid_text());
    // Nothing is left beside it: the temporary file is gone.
    CHECK(files_in(dir) == QStringList{"configuration.json"});

    stop = false;
    REQUIRE(store.save("{}").has_value());
    CHECK(**store.load() == "{}");
}

TEST_CASE("A temporary file left by a crash does not disturb the saved file", "[viewmodel][settings][file]")
{
    const QTemporaryDir dir;
    FileConfigStore store(path_in(dir, "configuration.json"));
    REQUIRE(store.save(valid_text()).has_value());
    write_all(path_in(dir, "configuration.json.tmp123"), "half a fi");

    CHECK(**store.load() == valid_text());
    REQUIRE(store.save("{}").has_value());
    CHECK(**store.load() == "{}");
}

TEST_CASE("A file this build cannot read is copied aside before it is replaced, never deleted",
          "[viewmodel][settings][file]")
{
    const QTemporaryDir dir;
    FileConfigStore store(path_in(dir, "configuration.json"));

    write_all(store.file(), "this is not a configuration");
    REQUIRE(store.save(valid_text()).has_value());
    CHECK(read_all(store.file()) == valid_text());
    CHECK(read_all(path_in(dir, "configuration.json.invalid")) == "this is not a configuration");

    // A file from a newer build is the same case; the earlier copy is not overwritten.
    write_all(store.file(), "{\"schema_version\": 9}");
    REQUIRE(store.save(valid_text()).has_value());
    CHECK(read_all(path_in(dir, "configuration.json.invalid")) == "this is not a configuration");
    CHECK(read_all(path_in(dir, "configuration.json.invalid-2")) == "{\"schema_version\": 9}");

    // A valid file is simply replaced.
    REQUIRE(store.save(valid_text()).has_value());
    CHECK(files_in(dir) ==
          QStringList{"configuration.json", "configuration.json.invalid", "configuration.json.invalid-2"});
}

TEST_CASE("A folder that cannot be made, or a file that cannot be written, reports", "[viewmodel][settings][file]")
{
    const QTemporaryDir dir;
    write_all(path_in(dir, "in-the-way"), "a file where a folder should be");

    FileConfigStore blocked(path_in(dir, "in-the-way") / "configuration.json");
    const auto no_folder = blocked.save(valid_text());
    REQUIRE_FALSE(no_folder.has_value());
    CHECK(no_folder.error().m_Kind == StorageErrorKind::IO);

    FileConfigStore store(path_in(dir, "configuration.json"));
    const auto no_directory = store.write_file((path_in(dir, "missing") / "out.json").string(), "text");
    CHECK_FALSE(no_directory.has_value());
    CHECK(files_in(dir) == QStringList{"in-the-way"});
}

TEST_CASE("A file the person chose is read up to the limit and refused beyond it", "[viewmodel][settings][file]")
{
    const QTemporaryDir dir;
    FileConfigStore store(path_in(dir, "configuration.json"));
    const auto chosen = path_in(dir, "chosen.json");
    write_all(chosen, std::string(100, 'x'));

    const auto whole = store.read_file(chosen.string(), 100);
    REQUIRE(whole.has_value());
    CHECK(whole->size() == 100);

    const auto too_big = store.read_file(chosen.string(), 99);
    REQUIRE_FALSE(too_big.has_value());
    CHECK(too_big.error().m_Kind == StorageErrorKind::LIMIT_EXCEEDED);
}

TEST_CASE("A file that is not there, or has gone, reports not found", "[viewmodel][settings][file]")
{
    const QTemporaryDir dir;
    FileConfigStore store(path_in(dir, "configuration.json"));
    const auto chosen = path_in(dir, "chosen.json");

    const auto never = store.read_file(chosen.string(), 100);
    REQUIRE_FALSE(never.has_value());
    CHECK(never.error().m_Kind == StorageErrorKind::NOT_FOUND);

    write_all(chosen, "text");
    REQUIRE(store.read_file(chosen.string(), 100).has_value());
    std::filesystem::remove(chosen);
    const auto gone = store.read_file(chosen.string(), 100);
    REQUIRE_FALSE(gone.has_value());
    CHECK(gone.error().m_Kind == StorageErrorKind::NOT_FOUND);
}

TEST_CASE("A file is written for the person as a unit", "[viewmodel][settings][file]")
{
    const QTemporaryDir dir;
    FileConfigStore store(path_in(dir, "configuration.json"));
    const auto out = path_in(dir, "exported.json");

    REQUIRE(store.write_file(out.string(), "first").has_value());
    REQUIRE(store.write_file(out.string(), "second").has_value());

    CHECK(read_all(out) == "second");
    CHECK(files_in(dir) == QStringList{"exported.json"});
}

// ------------------------------------------------------------------------------------- SettingsModel on a real thread

namespace
{

/// A settings model on a real storage thread and a real file.
struct ThreadedSettings
{
    explicit ThreadedSettings(const std::filesystem::path& file)
        : m_Store(file), m_Context{m_Logs, m_Store}, m_Gateway(m_Context), m_Settings(m_Gateway, m_World.environment())
    {
    }

    TestEnvironment m_World;
    MemoryLogRepository m_Logs{m_World.m_Clock};
    FileConfigStore m_Store;
    StorageContext m_Context;
    ThreadedStorageGateway m_Gateway;
    SettingsModel m_Settings;
};

} // namespace

TEST_CASE("A first launch, a setup and a second launch through the real store", "[viewmodel][settings][file]")
{
    const QTemporaryDir dir;
    const auto file = path_in(dir, "configuration.json");
    {
        ThreadedSettings first(file);
        first.m_Settings.load();
        REQUIRE(wait_for([&] { return !first.m_Settings.loading(); }));
        CHECK_FALSE(first.m_Settings.configured());

        REQUIRE(first.m_Settings.setFacilitator("Sam Example").isEmpty());
        REQUIRE(first.m_Settings.setUnitPrefix("UNIT").isEmpty());
        REQUIRE(wait_for([&] { return first.m_Settings.configured() && !first.m_Settings.saving(); }));
        REQUIRE(first.m_Settings.addLocation("North Site").isEmpty());
        REQUIRE(wait_for([&] { return first.m_Settings.locations()->rowCount() == 1; }));
    }

    const auto on_disk = import_configuration(read_all(file));
    REQUIRE(on_disk.has_value());
    CHECK(on_disk->m_Facilitator == "Sam Example");
    CHECK(on_disk->m_Locations.size() == 1);

    ThreadedSettings second(file);
    second.m_Settings.load();
    REQUIRE(wait_for([&] { return !second.m_Settings.loading(); }));
    CHECK(second.m_Settings.configured());
    CHECK(second.m_Settings.facilitator() == "Sam Example");
    CHECK(second.m_Settings.locations()->rowCount() == 1);
}

TEST_CASE("A corrupt file at launch gives setup and is kept on disk", "[viewmodel][settings][file]")
{
    const QTemporaryDir dir;
    const auto file = path_in(dir, "configuration.json");
    write_all(file, "{ this is damaged");
    ThreadedSettings launch(file);

    launch.m_Settings.load();
    REQUIRE(wait_for([&] { return !launch.m_Settings.loading(); }));

    CHECK_FALSE(launch.m_Settings.configured());
    CHECK_FALSE(launch.m_Settings.load_problem().isEmpty());
    CHECK(read_all(file) == "{ this is damaged");

    REQUIRE(launch.m_Settings.setFacilitator("Sam Example").isEmpty());
    REQUIRE(launch.m_Settings.setUnitPrefix("UNIT").isEmpty());
    REQUIRE(wait_for([&] { return launch.m_Settings.configured(); }));
    CHECK(read_all(path_in(dir, "configuration.json.invalid")) == "{ this is damaged");
    CHECK(import_configuration(read_all(file)).has_value());
}

TEST_CASE("Import and export through real files", "[viewmodel][settings][file]")
{
    const QTemporaryDir dir;
    const auto picked = path_in(dir, "picked.json");
    const auto exported = path_in(dir, "exported.json");
    write_all(picked, valid_text());
    ThreadedSettings app(path_in(dir, "configuration.json"));
    app.m_Settings.load();
    REQUIRE(wait_for([&] { return !app.m_Settings.loading(); }));
    QSignalSpy prepared(&app.m_Settings, &SettingsModel::importPrepared);
    QSignalSpy finished(&app.m_Settings, &SettingsModel::exportFinished);

    app.m_Settings.loadImport(QString::fromStdU16String(picked.u16string()));
    REQUIRE(wait_for([&] { return prepared.count() == 1; }));
    app.m_Settings.applyImport();
    REQUIRE(wait_for([&] { return app.m_Settings.configured(); }));
    app.m_Settings.exportTo(QString::fromStdU16String(exported.u16string()));
    REQUIRE(wait_for([&] { return finished.count() == 1; }));

    CHECK(finished.at(0).at(0).toBool());
    CHECK(read_all(exported) == valid_text());
    CHECK(read_all(path_in(dir, "configuration.json")) == valid_text());
}

TEST_CASE("An export to a place that cannot be written reports and says why", "[viewmodel][settings][file]")
{
    const QTemporaryDir dir;
    ThreadedSettings app(path_in(dir, "configuration.json"));
    write_all(app.m_Store.file(), valid_text());
    app.m_Settings.load();
    REQUIRE(wait_for([&] { return app.m_Settings.configured(); }));
    QSignalSpy finished(&app.m_Settings, &SettingsModel::exportFinished);

    app.m_Settings.exportTo(QString::fromStdU16String((path_in(dir, "missing") / "out.json").u16string()));
    REQUIRE(wait_for([&] { return finished.count() == 1; }));

    CHECK_FALSE(finished.at(0).at(0).toBool());
    CHECK_FALSE(finished.at(0).at(1).toString().isEmpty());
}
