// The save store: the default implementations, and the one in use. See save_store.h.
#include "ipod/platform/save_store.h"

#include <filesystem>
#include <fstream>
#include <map>

namespace ipod::platform {

namespace {

namespace fs = std::filesystem;

// A save name is a plain file name, whatever the game called it. The statistics path arrives as
// a whole device path ("/Volumes/.../stats"), and no store should be asked to reproduce that
// layout — only the last component identifies the save.
std::string base_name(const std::string& name) {
    const size_t slash = name.find_last_of("/\\:");
    return slash == std::string::npos ? name : name.substr(slash + 1);
}

// The fallback: saves last as long as the process does. It exists so that a platform which has
// not chosen yet still behaves — the game saves, reads it back, and nothing touches the disk.
class MemorySaveStore final : public SaveStore {
public:
    bool store(const std::string& name, const std::vector<uint8_t>& data) override {
        saves_[base_name(name)] = data;
        return true;
    }
    bool load(const std::string& name, std::vector<uint8_t>& data) const override {
        const auto found = saves_.find(base_name(name));
        if (found == saves_.end()) {
            return false;
        }
        data = found->second;
        return true;
    }

private:
    std::map<std::string, std::vector<uint8_t>> saves_;
};

class DirectorySaveStore final : public SaveStore {
public:
    explicit DirectorySaveStore(std::string directory) : directory_(std::move(directory)) {}

    bool store(const std::string& name, const std::vector<uint8_t>& data) override {
        std::error_code error;
        fs::create_directories(directory_, error);
        std::ofstream file(path_for(name), std::ios::binary | std::ios::trunc);
        if (!file) {
            return false;
        }
        file.write(reinterpret_cast<const char*>(data.data()),
                   static_cast<std::streamsize>(data.size()));
        return file.good();
    }

    bool load(const std::string& name, std::vector<uint8_t>& data) const override {
        std::ifstream file(path_for(name), std::ios::binary | std::ios::ate);
        if (!file) {
            return false;
        }
        const std::streamsize size = file.tellg();
        if (size < 0) {
            return false;
        }
        data.resize(static_cast<size_t>(size));
        file.seekg(0);
        return static_cast<bool>(
            file.read(reinterpret_cast<char*>(data.data()), static_cast<std::streamsize>(size)));
    }

private:
    [[nodiscard]] fs::path path_for(const std::string& name) const {
        return fs::path(directory_) / base_name(name);
    }

    std::string directory_;
};

std::unique_ptr<SaveStore>& installed() {
    static std::unique_ptr<SaveStore> store = std::make_unique<MemorySaveStore>();
    return store;
}

}  // namespace

SaveStore& save_store() {
    return *installed();
}

void set_save_store(std::unique_ptr<SaveStore> store) {
    if (store != nullptr) {
        installed() = std::move(store);
    }
}

std::unique_ptr<SaveStore> make_directory_save_store(const std::string& directory) {
    return std::make_unique<DirectorySaveStore>(directory);
}

}  // namespace ipod::platform
