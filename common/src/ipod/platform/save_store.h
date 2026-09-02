// Where the game's saved games go.
//
// The iPod kept them as files beside the game. Nothing else has to: a console has a save
// archive, Android has the app's private storage, a browser has local storage. So the game
// asks for a name and hands over some bytes, and the platform decides what that means.
//
// A platform provides its own by returning one from `Platform::create_save_store`; the runtime
// installs a directory-backed one when it does not. The store is asked for exactly three things
// (`src/libeapp/async_file.cpp`): the saved game, its backup, and the statistics.
#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace ipod::platform {

class SaveStore {
public:
    virtual ~SaveStore() = default;

    // Replace everything kept under `name` with `data`. False when it could not be stored — the
    // game is told the save failed, which it copes with.
    [[nodiscard]] virtual bool store(const std::string& name, const std::vector<uint8_t>& data) = 0;

    // Everything kept under `name`, or false when there is nothing.
    [[nodiscard]] virtual bool load(const std::string& name, std::vector<uint8_t>& data) const = 0;
};

// The store in use. Never null: until a platform installs one, saves are kept in memory, so a
// session works and nothing is written anywhere unexpected.
[[nodiscard]] SaveStore& save_store();
void set_save_store(std::unique_ptr<SaveStore> store);

// One file per save in `directory`, named as the game names it — what the iPod did, and what
// every desktop platform here uses.
[[nodiscard]] std::unique_ptr<SaveStore> make_directory_save_store(const std::string& directory);

}  // namespace ipod::platform
