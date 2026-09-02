// The AsyncFileIO framework: the game's files, served from the title's resource directory.
//
// On the iPod these calls are asynchronous: the game fills in a request object, the firmware
// accepts it, and a completion callback runs later. Here the operation is performed at once and
// the completion is queued for the frame pump to run between frames (`queue_completion`), which
// is how the emulator models it with `--async-files`. The request object layout and every
// behaviour below come from reference/eapp-loader/lib.rs (Stub::AsyncOpen / AsyncOp /
// AsyncRead) and reversing/asyncfileio-abi.md.
//
// Files are found by name, case-insensitively, anywhere under the game directory — the titles
// refer to resources by bare name and the directory layout is not part of the contract.
// Handles are 1-based and never reused; the host keeps each file's bytes and a read position.
#include "framework/storage.h"
#include "host_state.h"
#include "ipod_eapp.h"
#include "platform/save_store.h"
#include "runtime/memory.h"

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <map>
#include <string>
#include <vector>

namespace minigolf::eapp {

namespace {

namespace fs = std::filesystem;

// Request object fields (offsets in bytes).
constexpr uint32_t REQUEST_OPERATION = 0x04;    // byte: 4 = read at position, 5 = seek
constexpr uint32_t REQUEST_FILE_OBJECT = 0x08;  // the game's file object; +8 in it receives results
constexpr uint32_t REQUEST_SEEK_OFFSET = 0x0c;
constexpr uint32_t REQUEST_SEEK_WHENCE = 0x10;  // byte: 0 set, 1 current, 2 end
constexpr uint32_t REQUEST_BUFFER = 0x14;
constexpr uint32_t REQUEST_LENGTH = 0x18;
constexpr uint32_t REQUEST_STATUS = 0x20;       // 0 on success, all ones on failure
constexpr uint32_t REQUEST_SIZE_RESULT = 0x24;  // file size, for an open without a buffer
constexpr uint32_t REQUEST_POSITION_RESULT = 0x28;
constexpr uint32_t FILE_OBJECT_RESULT = 0x08;

constexpr uint32_t OPERATION_READ = 4;
constexpr uint32_t OPERATION_SEEK = 5;
constexpr uint32_t OPEN_MODE_WRITE = 1;
constexpr uint32_t MAX_WRITE_BYTES = 1u << 24;
constexpr size_t EXTENTS_KEPT = 512;
constexpr uint32_t STATUS_OK = 0;
constexpr uint32_t STATUS_FAILED = ~0u;

struct OpenFile {
    std::vector<uint8_t> bytes;
    size_t position = 0;
    std::string path;
};

struct FileState {
    std::string game_dir;
    std::vector<OpenFile> files;  // handle - 1 indexes this
    std::map<uint32_t, uint32_t> handle_by_object;
    std::vector<FileExtent> extents;
    std::vector<std::string> wav_files;
    std::string course = "c00";
};

FileState& files() {
    static FileState instance;
    return instance;
}

std::string lowercase(std::string text) {
    std::transform(text.begin(), text.end(), text.begin(),
                   [](unsigned char c) { return std::tolower(c); });
    return text;
}

// Search the game directory tree for a file whose name matches `name`'s final component,
// ignoring case. Directories are visited in sorted order so the result does not depend on the
// filesystem's listing order.
fs::path find_resource(const std::string& name) {
    std::string relative = name;
    std::replace(relative.begin(), relative.end(), '\\', '/');
    const size_t slash = relative.rfind('/');
    const std::string wanted =
        lowercase(slash == std::string::npos ? relative : relative.substr(slash + 1));

    std::vector<fs::path> pending = {files().game_dir};
    std::error_code error;
    while (!pending.empty()) {
        const fs::path directory = pending.back();
        pending.pop_back();
        std::vector<fs::directory_entry> entries;
        for (const auto& entry : fs::directory_iterator(directory, error)) {
            entries.push_back(entry);
        }
        std::sort(entries.begin(), entries.end());
        for (const auto& entry : entries) {
            if (entry.is_directory(error)) {
                pending.push_back(entry.path());
            } else if (lowercase(entry.path().filename().string()) == wanted) {
                return entry.path();
            }
        }
    }
    return {};
}

std::vector<uint8_t> read_host_file(const fs::path& path) {
    std::ifstream in(path, std::ios::binary);
    return {std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
}

// Note which course's assets are loaded: course files are named c00, c000, c00.en and so on.
void note_course(const std::string& filename) {
    if (filename.size() >= 3 && filename[0] == 'c' &&
        std::isdigit(static_cast<unsigned char>(filename[1])) &&
        std::isdigit(static_cast<unsigned char>(filename[2]))) {
        files().course = filename.substr(0, 3);
    }
}

uint32_t register_open(OpenFile file) {
    const std::string filename = fs::path(file.path).filename().string();
    if (lowercase(filename).size() >= 4 &&
        lowercase(filename).compare(lowercase(filename).size() - 4, 4, ".wav") == 0 &&
        std::find(files().wav_files.begin(), files().wav_files.end(), file.path) ==
            files().wav_files.end()) {
        files().wav_files.push_back(file.path);
    }
    note_course(filename);
    files().files.push_back(std::move(file));
    return static_cast<uint32_t>(files().files.size());
}

uint32_t open_for_read(const std::string& name) {
    const fs::path path = find_resource(name);
    if (path.empty()) {
        return 0;
    }
    return register_open(OpenFile{read_host_file(path), 0, path.string()});
}

// A write-mode open creates the file under the game directory if it is missing. Names that
// could escape the directory are refused.
uint32_t open_for_write(const std::string& name) {
    if (name.empty() || name.find("..") != std::string::npos || name.front() == '/') {
        return 0;
    }
    const fs::path path = fs::path(files().game_dir) / name;
    std::error_code error;
    fs::create_directories(path.parent_path(), error);
    std::vector<uint8_t> existing;
    if (fs::exists(path, error)) {
        existing = read_host_file(path);
    } else if (!std::ofstream(path, std::ios::binary)) {
        return 0;
    }
    return register_open(OpenFile{std::move(existing), 0, path.string()});
}

OpenFile* file_for(uint32_t handle) {
    if (handle == 0 || handle > files().files.size()) {
        return nullptr;
    }
    return &files().files[handle - 1];
}

// Copy up to `length` bytes from the file's position into guest memory; returns the count.
uint32_t read_into_guest(uint32_t handle, uint32_t buffer, uint32_t length) {
    OpenFile* file = file_for(handle);
    if (file == nullptr) {
        return 0;
    }
    const size_t start = std::min(file->position, file->bytes.size());
    const size_t count = std::min<size_t>(length, file->bytes.size() - start);
    for (size_t i = 0; i < count; ++i) {
        st8(buffer + static_cast<uint32_t>(i), file->bytes[start + i]);
    }
    file->position = start + count;
    if (count > 0) {
        files().extents.insert(
            files().extents.begin(),
            FileExtent{buffer, buffer + static_cast<uint32_t>(count), file->path});
        if (files().extents.size() > EXTENTS_KEPT) {
            files().extents.resize(EXTENTS_KEPT);
        }
    }
    return static_cast<uint32_t>(count);
}

uint32_t skip_forward(uint32_t handle, uint32_t by) {
    OpenFile* file = file_for(handle);
    if (file == nullptr) {
        return 0;
    }
    const size_t moved =
        std::min<size_t>(by, file->bytes.size() - std::min(file->position, file->bytes.size()));
    file->position += moved;
    return static_cast<uint32_t>(moved);
}

uint32_t seek(uint32_t handle, int32_t offset, uint32_t whence) {
    OpenFile* file = file_for(handle);
    if (file == nullptr) {
        return 0;
    }
    const int64_t size = static_cast<int64_t>(file->bytes.size());
    const int64_t base = whence == 1   ? static_cast<int64_t>(file->position)
                         : whence == 2 ? size
                                       : 0;
    file->position = static_cast<size_t>(std::clamp<int64_t>(base + offset, 0, size));
    return static_cast<uint32_t>(file->position);
}

// The write-mode open carries the whole file in the request's buffer: write it out now.
void write_buffer_to_file(uint32_t handle, uint32_t buffer, uint32_t length) {
    OpenFile* file = file_for(handle);
    if (file == nullptr || buffer == 0 || length == 0 || length >= MAX_WRITE_BYTES) {
        return;
    }
    std::vector<uint8_t> bytes(length);
    for (uint32_t i = 0; i < length; ++i) {
        bytes[i] = static_cast<uint8_t>(ld8(buffer + i));
    }
    std::ofstream out(file->path, std::ios::binary);
    if (out.write(reinterpret_cast<const char*>(bytes.data()),
                  static_cast<std::streamsize>(bytes.size()))) {
        file->bytes = std::move(bytes);
        file->position = 0;
    }
}

// --- the save store ---------------------------------------------------------------------------
//
// An entry being written: the name it will be stored under and the bytes gathered so far. The
// game writes one save at a time, but a table costs nothing and means a stray close cannot
// commit the wrong bytes.

constexpr uint32_t STORE_OK = 0;           // the open/write/close answers the game reads as success
constexpr uint32_t STORE_REFUSED = 8;      // anything else stops it writing; 8 is its own "no"
constexpr uint32_t STORE_STUB_ANSWER = 1;  // what the emulator's stub answered: "not ready"
constexpr uint32_t STORE_RECORD_HANDLE = 0;  // SimpleFile: handle at +0, status at +4

struct StoreEntry {
    std::string name;
    std::vector<uint8_t> bytes;
    bool open = false;
};

std::vector<StoreEntry>& store_entries() {
    static std::vector<StoreEntry> entries;
    return entries;
}

// Handles are one-based so that 0 stays "refused", as everywhere else in this file.
uint32_t open_store_entry(const std::string& name) {
    if (name.empty()) {
        return 0;
    }
    std::vector<StoreEntry>& entries = store_entries();
    for (size_t i = 0; i < entries.size(); ++i) {
        if (!entries[i].open) {
            entries[i] = StoreEntry{name, {}, true};
            return static_cast<uint32_t>(i + 1);
        }
    }
    entries.push_back(StoreEntry{name, {}, true});
    return static_cast<uint32_t>(entries.size());
}

StoreEntry* store_entry_for(uint32_t handle) {
    std::vector<StoreEntry>& entries = store_entries();
    if (handle == 0 || handle > entries.size() || !entries[handle - 1].open) {
        return nullptr;
    }
    return &entries[handle - 1];
}

bool append_to_store_entry(uint32_t handle, uint32_t data, uint32_t size) {
    StoreEntry* entry = store_entry_for(handle);
    if (entry == nullptr || data == 0 || size == 0 || size > MAX_WRITE_BYTES) {
        return false;
    }
    const uint8_t* from = guest_pointer(data, size);
    entry->bytes.insert(entry->bytes.end(), from, from + size);
    return true;
}

bool commit_store_entry(uint32_t handle) {
    StoreEntry* entry = store_entry_for(handle);
    if (entry == nullptr) {
        return false;
    }
    const bool stored =
        entry->bytes.empty() || platform::save_store().store(entry->name, entry->bytes);
    entry->open = false;
    entry->bytes.clear();
    entry->bytes.shrink_to_fit();
    return stored;
}

// The saves the store owns, by the names the game opens them with. A read of one of these comes
// from the store rather than the game directory, so a platform that keeps saves somewhere else
// is read back from there too.
bool is_save_name(const std::string& name) {
    return name.size() >= 4 && name.compare(name.size() - 4, 4, ".sav") == 0;
}

// Shared body of #0 and #3, which differ only in how many arguments carry the request.
uint32_t async_open(uint32_t open_mode, uint32_t path, uint32_t request) {
    const uint32_t mode = open_mode & 0xff;
    const std::string name = read_guest_string(path, 256);

    // A saved game belongs to the platform's store, not to the game directory. It is also the
    // one thing the game opens in write mode *to read*: `flow.cpp` asks for the save with mode 1
    // whether it is loading or saving, so opening it for writing here truncated the save on the
    // way to reading it — the reason a save never survived a restart. Saving happens through the
    // store calls (#12/#14/#16) on the way out, never here.
    const bool save = is_save_name(name);
    const bool writing = !save && mode == OPEN_MODE_WRITE;
    bool loaded = false;
    uint32_t handle = 0;
    if (save) {
        // A save that is not there yet still opens, empty. The game expects to be able to open
        // one before it has ever written it — it decides there is no saved game from the magic
        // words it does not find, not from the open failing.
        std::vector<uint8_t> saved;
        loaded = platform::save_store().load(name, saved);
        handle = register_open(OpenFile{std::move(saved), 0, name});
    } else {
        handle = writing ? open_for_write(name) : open_for_read(name);
    }
    const uint32_t object = ld32(request + REQUEST_FILE_OBJECT);

    if (handle == 0) {
        st32(request + REQUEST_STATUS, STATUS_FAILED);
        queue_completion(request);
        return 0;
    }
    if (object != 0) {
        files().handle_by_object[object] = handle;
        st32(object + FILE_OBJECT_RESULT, handle);
    }
    const uint32_t buffer = ld32(request + REQUEST_BUFFER);
    const uint32_t length = ld32(request + REQUEST_LENGTH);
    if (writing) {
        write_buffer_to_file(handle, buffer, length);
    }
    if (loaded && buffer != 0 && length != 0) {
        // Opening a save *is* loading it: the request already says where it goes and how much
        // fits, and the game reads the buffer when the completion arrives without asking again.
        st32(request + REQUEST_SIZE_RESULT, read_into_guest(handle, buffer, length));
    } else if (buffer == 0 || length == 0) {
        const OpenFile* file = file_for(handle);
        st32(request + REQUEST_SIZE_RESULT,
             file != nullptr ? static_cast<uint32_t>(file->bytes.size()) : 0);
    }
    st32(request + REQUEST_STATUS, STATUS_OK);
    queue_completion(request);
    return handle;
}

// An operation with nothing to do but complete (close and friends).
uint32_t async_operation(uint32_t request) {
    st32(request + REQUEST_STATUS, STATUS_OK);
    queue_completion(request);
    return 1;
}

// The body of #2: the request's own fields say whether this is a read, a seek or a skip.
uint32_t async_perform(uint32_t request) {
    const uint32_t buffer = ld32(request + REQUEST_BUFFER);
    const uint32_t length = ld32(request + REQUEST_LENGTH);
    const uint32_t object = ld32(request + REQUEST_FILE_OBJECT);
    const uint32_t operation = ld8(request + REQUEST_OPERATION);
    const auto found = files().handle_by_object.find(object);
    const uint32_t handle = found == files().handle_by_object.end() ? 0 : found->second;

    uint32_t got = 0;
    if (handle == 0) {
        got = 0;
    } else if (operation == OPERATION_SEEK) {
        const int32_t offset = static_cast<int32_t>(ld32(request + REQUEST_SEEK_OFFSET));
        const uint32_t whence = ld8(request + REQUEST_SEEK_WHENCE);
        st32(request + REQUEST_POSITION_RESULT, seek(handle, offset, whence));
    } else if (operation == OPERATION_READ) {
        got = read_into_guest(handle, buffer, length);
        const OpenFile* file = file_for(handle);
        st32(request + REQUEST_POSITION_RESULT,
             file != nullptr ? static_cast<uint32_t>(file->position) : 0);
    } else if (buffer == 0 && length > 0) {
        got = skip_forward(handle, length);
    } else {
        got = read_into_guest(handle, buffer, length);
    }

    if (object != 0) {
        st32(object + FILE_OBJECT_RESULT, got);
    }
    const bool ok = operation == OPERATION_SEEK || length == 0 || (got == length && got > 0);
    st32(request + REQUEST_STATUS, ok ? STATUS_OK : STATUS_FAILED);
    queue_completion(request);
    return got;
}

}  // namespace

const std::vector<FileExtent>& file_extents() {
    return files().extents;
}
const std::vector<std::string>& opened_wav_files() {
    return files().wav_files;
}
const std::string& current_course() {
    return files().course;
}
const std::string& game_directory() {
    return files().game_dir;
}
void set_game_directory(const std::string& path) {
    files().game_dir = path;
}

}  // namespace minigolf::eapp

namespace minigolf::storage {

using namespace minigolf::eapp;  // NOLINT(google-build-using-namespace): one file, by design

// #0 open(mode, path, reading, request) and #3 open(mode, path, request): the handle, or 0.
uint32_t open(uint32_t mode, GuestAddress path, uint32_t reading, GuestAddress request) {
    log_call("AsyncFileIO", 0, {mode, path, reading, request});
    return async_open(mode, path, request);
}

uint32_t open_for_read(uint32_t mode, GuestAddress path, GuestAddress request) {
    log_call("AsyncFileIO", 3, {mode, path, request});
    return async_open(mode, path, request);
}

// #1 close(request): the completion is queued as for any other operation.
uint32_t close(GuestAddress request) {
    log_call("AsyncFileIO", 1, {request});
    return async_operation(request);
}

// #2 perform(request): a read, a seek, or a skip depending on the request's operation byte and
// buffer. Answers the byte count; the file object's +8 receives it too.
uint32_t perform(GuestAddress request) {
    log_call("AsyncFileIO", 2, {request});
    return async_perform(request);
}

// #12, #14, #16 — the save store, which is how the game writes a saved game.
//
// The game opens a save by name into a two-word record (handle, status), writes the whole thing
// in one call, and closes it. It only writes when the open answered 0 or 5, so the previous
// implementation — which answered 1 and did nothing — meant the write was never even attempted:
// no `#16` appears in any recorded log. Answering 0 and filling in the handle is what makes a
// save happen at all.
//
// Where the bytes go is `platform::save_store()`'s business, not this file's.
namespace {
// See set_store_stubbed: the recordings predate the store working at all.
bool store_stubbed = false;
}  // namespace

void set_store_stubbed(bool stubbed) {
    store_stubbed = stubbed;
}

uint32_t store_open(uint32_t mode, GuestAddress path, GuestAddress record) {
    log_call("AsyncFileIO", 12, {mode, path, record});
    if (store_stubbed) {
        return STORE_STUB_ANSWER;
    }
    const uint32_t handle = open_store_entry(read_guest_string(path, 256));
    if (handle == 0) {
        return STORE_REFUSED;
    }
    if (record != 0) {
        st32(record + STORE_RECORD_HANDLE, handle);
    }
    return STORE_OK;
}

// Closing is what commits: the bytes are handed to the platform's store here, so a save that is
// never closed never lands, and a half-written one cannot replace a good one.
uint32_t store_close(uint32_t handle) {
    log_call("AsyncFileIO", 14, {handle});
    if (store_stubbed) {
        return STORE_STUB_ANSWER;
    }
    return commit_store_entry(handle) ? STORE_OK : STORE_REFUSED;
}

// `data` is a guest address and `size` a byte count, whatever the ordinal's arguments were
// called: the game hands over the whole save in one call.
uint32_t store_write(uint32_t handle, uint32_t data, uint32_t size) {
    log_call("AsyncFileIO", 16, {handle, data, size});
    if (store_stubbed) {
        return STORE_STUB_ANSWER;
    }
    return append_to_store_entry(handle, data, size) ? STORE_OK : STORE_REFUSED;
}

}  // namespace minigolf::storage
