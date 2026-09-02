// The AsyncFileIO framework: the game's files, served from the title's resource directory.
//
// On the iPod these calls are asynchronous: the game fills in a request object, the firmware
// accepts it, and a completion callback runs later. Here the operation is performed at once and
// the completion is queued for the frame pump to run between frames (`queue_completion`), which
// is how the emulator models it with `--async-files`. The request object layout and every
// behaviour below come from reference/eapp-loader/lib.rs (Stub::AsyncOpen / AsyncOp /
// AsyncRead) and reversing/asyncfileio-abi.md.
//
// Files are found by the path the game gives, case-insensitively, and failing that by bare name
// anywhere under the game directory (`find_resource` says why both rules exist).
// Handles are 1-based and never reused; the host keeps each file's bytes and a read position.
#include "framework/storage.h"
#include "gamedata/asset_fixes.h"
#include "host_state.h"
#include "ipod_eapp.h"
#include "platform/save_store.h"
#include "runtime/memory.h"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <map>
#include <string>
#include <vector>

namespace vortex::eapp {

namespace {

namespace fs = std::filesystem;

// VORTEX_TRACE_FILES=1 prints one line per file operation. The emulator has the same in
// `--file-ops`, and a save that never finishes is exactly the kind of fault that is one line of
// this and an afternoon without it.
void trace(const char* what, const std::string& name, uint32_t a, uint32_t b, uint32_t result) {
    static const bool on = std::getenv("VORTEX_TRACE_FILES") != nullptr;
    if (on) {
        std::fprintf(stderr, "file: %-8s %-16s %u %u -> %u\n", what, name.c_str(), a, b, result);
    }
}

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
// Where the firmware leaves the handle for the game to cache. RetailOS resolves later operations
// through *this* field rather than through the file object, so an open that does not write it
// leaves the 0xffffffff the game initialised it to and every later request names a stream that
// does not exist (reference/eapp-loader/lib.rs, Stub::AsyncOpen).
constexpr uint32_t REQUEST_HANDLE = 0x2c;
// The file object's result word. What belongs here is the *result of the operation*, which is
// not always the handle: for an open that loaded, it is how many bytes arrived. Lost's completion
// handler at 0x1803b068 stores it as the slot's descriptor and hands it to `OpenGLES #164` as the
// render server's image size, so a handle here tells the driver its firmware is one byte long.
constexpr uint32_t FILE_OBJECT_RESULT = 0x08;

constexpr uint32_t OPERATION_READ = 4;
constexpr uint32_t OPERATION_SEEK = 5;
constexpr uint32_t OPEN_MODE_WRITE = 1;
constexpr uint32_t MAX_WRITE_BYTES = 1u << 24;
constexpr uint32_t STATUS_OK = 0;
constexpr uint32_t STATUS_FAILED = ~0u;

struct OpenFile {
    std::vector<uint8_t> bytes;
    size_t position = 0;
    std::string path;      // where it lives on the host; empty for a save, which the store owns
    std::string name;      // what the game called it
    bool writing = false;  // opened to be written: which way a transfer goes
    bool saved = false;    // the platform's save store owns this one, not the game directory
    bool written = false;  // a transfer has arrived, so the old contents are gone
};

struct FileState {
    std::string game_dir;
    std::vector<OpenFile> files;  // handle - 1 indexes this
    std::map<uint32_t, uint32_t> handle_by_object;
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
// One path component matched against a directory's entries, ignoring case.
fs::path child_named(const fs::path& directory, const std::string& wanted) {
    std::error_code error;
    for (const auto& entry : fs::directory_iterator(directory, error)) {
        if (lowercase(entry.path().filename().string()) == wanted) {
            return entry.path();
        }
    }
    return {};
}

// The file a name refers to. The path is honoured first: Texas Hold'em asks for its files by
// path, and two of them (`Data/en.lproj/fonts.txt`, `Data/ja.lproj/fonts.txt`) share a name and
// a size and differ only in which folder they sit in — which is how a search by bare name once
// handed the English game its Japanese font. Only when nothing sits at the path given is the
// name looked for anywhere under the game directory, which is the rule the other titles were
// written against: they refer to resources by bare name, and their folder layout is not part of
// the contract.
fs::path find_resource(const std::string& name) {
    std::string relative = name;
    std::replace(relative.begin(), relative.end(), '\\', '/');

    fs::path at_path = files().game_dir;
    size_t start = 0;
    while (!at_path.empty() && start <= relative.size()) {
        const size_t slash = relative.find('/', start);
        const std::string component = lowercase(
            relative.substr(start, slash == std::string::npos ? std::string::npos : slash - start));
        at_path = component.empty() ? at_path : child_named(at_path, component);
        if (slash == std::string::npos) {
            break;
        }
        start = slash + 1;
    }
    std::error_code error;
    if (!at_path.empty() && fs::is_regular_file(at_path, error)) {
        return at_path;
    }

    const size_t slash = relative.rfind('/');
    const std::string wanted =
        lowercase(slash == std::string::npos ? relative : relative.substr(slash + 1));
    std::vector<fs::path> pending = {files().game_dir};
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

uint32_t register_open(OpenFile file) {
    files().files.push_back(std::move(file));
    return static_cast<uint32_t>(files().files.size());
}

uint32_t open_for_read(const std::string& name) {
    const fs::path path = find_resource(name);
    if (path.empty()) {
        return 0;
    }
    // One of this game's shipped files mislabels three of its textures, and the correction has to
    // happen here — before the game sees a byte of it — because the game reads the label and asks
    // the driver for the wrong thing. `gamedata/asset_fixes.h` says which and why; it corrects
    // nothing in any other file, and nothing at all under `--emulator-firmware`.
    std::vector<uint8_t> bytes = read_host_file(path);
    static_cast<void>(gamedata::correct_asset(name, bytes));
    return register_open(OpenFile{std::move(bytes), 0, path.string(), name, false, false});
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
    // Whatever is already there is kept: an open is not a truncation, and this is the same open
    // the game uses when it means to read (see `async_open`).
    std::vector<uint8_t> existing;
    if (fs::exists(path, error)) {
        existing = read_host_file(path);
    } else if (!std::ofstream(path, std::ios::binary)) {
        return 0;
    }
    return register_open(OpenFile{std::move(existing), 0, path.string(), name, true, false});
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
    return static_cast<uint32_t>(count);
}

// Put the read position back to the start. An open-as-load has consumed the whole file without
// the game asking for a read, and a game that opens that way and *then* reads (Mini Golf does)
// must not find itself at the end.
void rewind(uint32_t handle) {
    if (OpenFile* file = file_for(handle); file != nullptr) {
        file->position = 0;
    }
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

// --- the store calls (#12/#14/#16) --------------------------------------------------------------
//
// A second way to keep a file, which the Mini Golf recomp was built around and **this game has
// never been seen to use**: every save it writes goes through an ordinary open and transfer (see
// `async_open`). The three entry points are kept because the image imports them and a framework
// answers what it is asked, not what it expects to be asked; if a later part of the game does
// call them, they work.
//
// --- the save store ---------------------------------------------------------------------------
//
// What the platform's store keeps a file under, given the name the game used. Defined below,
// beside the file-layer path that shares it.
std::string store_key(const std::string& name);

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
        entry->bytes.empty() || platform::save_store().store(store_key(entry->name), entry->bytes);
    entry->open = false;
    entry->bytes.clear();
    entry->bytes.shrink_to_fit();
    return stored;
}

// The files the platform's store owns, by the names the game opens them with. This game keeps
// three of its own in its folder (PLAN.md difference 3): `options` (12 bytes) and `stats` (228)
// at the root, both created during the boot by a write-mode open and one transfer, and
// `<lang>/stats` (`en/stats`, 2 128 bytes) once a name has been entered. None of the three ships,
// and nothing shipped is called either name, so the names alone identify them. They are read
// from and written to the platform's store rather than the game directory, so a platform that
// keeps saves somewhere else (a console's archive, an app's private storage) reads them back from
// there too.
bool is_save_name(const std::string& name) {
    const std::string lower = lowercase(name);
    if (lower == "options" || lower == "stats") {
        return true;
    }
    // `<lang>/stats`: a two-letter language folder and nothing else in front of the name.
    return lower.size() == 8 && lower.compare(2, 6, "/stats") == 0 && lower[0] >= 'a' &&
           lower[0] <= 'z' && lower[1] >= 'a' && lower[1] <= 'z';
}

// What the store keeps a file under, for every path this game saves through — and there are two
// of those, which is the whole reason this function exists rather than being inlined at one of
// them. The game writes `options` and `stats` through the ordinary file calls and `stats`,
// `<lang>/stats` and its saved game `quick<name>` through the store ordinals (#12/#14/#16), and
// both end at the same `platform::save_store()`.
//
// A store's names are flat: it keeps only the last path component
// (ipod/platform/save_store.cpp, `base_name`, which exists because another title hands it a whole
// device path). So `en/stats` arrived as `stats` and *overwrote the root `stats`* — 2 128 bytes
// of the player's per-language statistics on top of the 228-byte file, every time the game was
// left. Joining the last two components with a dot keeps them apart, and leaves a plain name
// (`options`, `quicka`) exactly as it is.
std::string store_key(const std::string& name) {
    const std::string lower = lowercase(name);
    const size_t last = lower.find_last_of("/\\:");
    if (last == std::string::npos) {
        return lower;
    }
    const std::string leaf = lower.substr(last + 1);
    const std::string parent = lower.substr(0, last);
    const size_t before = parent.find_last_of("/\\:");
    const std::string directory = before == std::string::npos ? parent : parent.substr(before + 1);
    return directory.empty() ? leaf : directory + "." + leaf;
}

// Put a file's bytes where they belong: the platform's store for a save, the disk for anything
// else. False when they could not be kept, which the caller reports as a failed transfer.
bool commit(OpenFile& file) {
    if (file.saved) {
        return platform::save_store().store(store_key(file.name), file.bytes);
    }
    std::ofstream out(file.path, std::ios::binary | std::ios::trunc);
    return static_cast<bool>(out.write(reinterpret_cast<const char*>(file.bytes.data()),
                                       static_cast<std::streamsize>(file.bytes.size())));
}

// Take `length` bytes from guest memory into the file, at its current position, and put the
// result where the file belongs. Returns how many were written; 0 says the transfer failed,
// which is what the game is told.
uint32_t write_from_guest(uint32_t handle, uint32_t buffer, uint32_t length) {
    OpenFile* file = file_for(handle);
    if (file == nullptr || buffer == 0 || length == 0 || length >= MAX_WRITE_BYTES) {
        return 0;
    }
    // The first transfer into a file is what its contents become; the ones after it extend that.
    // An *open* must not do this — the game opens a save with write mode when it means to read
    // one — so the length is settled here, by bytes actually arriving, and not there.
    if (!file->written) {
        file->written = true;
        file->bytes.resize(file->position + length);
    } else if (file->position + length > file->bytes.size()) {
        file->bytes.resize(file->position + length);
    }
    for (uint32_t i = 0; i < length; ++i) {
        file->bytes[file->position + i] = static_cast<uint8_t>(ld8(buffer + i));
    }
    file->position += length;
    return commit(*file) ? length : 0;
}

bool& writes_refused();

// Shared body of #0 and #3, which differ only in how many arguments carry the request.
uint32_t async_open(uint32_t open_mode, uint32_t path, uint32_t request) {
    const uint32_t mode = open_mode & 0xff;
    const std::string name = read_guest_string(path, 256);

    // One of the game's own files (is_save_name) belongs to the platform's store rather than to
    // the game directory. This game handles them the ordinary way — an open, a transfer, a
    // close — and the mode means what it says: watched at boot with VORTEX_TRACE_FILES=1, it
    // opens `options` with write mode and no buffer, transfers 12 bytes into it, and closes it;
    // `stats` the same with 228. (The Sims Bowling's file layer, this one's ancestor, read a save
    // whatever mode named it, because that game wrote its save through the store ordinals; that
    // rule would throw every one of this game's writes away.)
    //
    // A read-mode open of one that does not exist yet *fails*, as it did on the device — the
    // game creates its files after that failure, and the recordings are of first boots that
    // took exactly that path (play.rs forces `allow_creates` for this title because without the
    // create "its loader retries the missing `options` forever"). A write-mode open starts
    // empty; the bytes the transfer brings are what the store keeps at close.
    // Anything the store already holds is a save, whatever it is called. The three fixed names
    // below are only the ones that must open *before* they exist; the saved game itself is
    // `quick<name>` — the player chose the name, so nothing can know it in advance — and it too
    // is opened with mode 1 and read at start-up. Asking the store first is what makes that work
    // without a list: the store holds exactly what this game has written, and nothing the game
    // ships is called any of those names.
    std::vector<uint8_t> saved;
    const bool stored = platform::save_store().load(store_key(name), saved);
    const bool save = stored || is_save_name(name);
    // A save is opened to be *read*, whatever mode it names — and the mode it names is 1, the
    // same one another title's file layer reads as "write". This is not a guess: watched with
    // VORTEX_TRACE_FILES=1, every file this game keeps is written through the *store* ordinals
    // (#12 open, #16 write, #14 close) when it is left — `stats` 228 bytes, `en/stats` 2 128,
    // `options` 12, and the saved game `quick<name>` 18 988 — and the only thing the file calls
    // ever do with those names is one mode-1 open and one transfer at start-up, which is the
    // load. Believing the mode instead pointed that transfer at the file and wrote the game's
    // *uninitialised* buffer over the save on every launch: `stats` came back 228 bytes of zeros
    // and the player's statistics and saved game were gone before the title screen.
    const bool writing = save ? false : mode == OPEN_MODE_WRITE;
    uint32_t handle = 0;
    if (save) {
        // A save that is not there yet still opens, **empty**. The game expects to be able to
        // open one before it has ever written it and decides there is none from what it does not
        // find inside — refusing the open instead left it on `LOADING…` for ever, waiting for a
        // file it had no way to create. What it does next is create the defaults through the
        // store ordinals, which is where every one of this game's writes goes.
        handle = register_open(OpenFile{std::move(saved), 0, {}, name, false, true});
    } else if (writing) {
        handle = open_for_write(name);
    } else {
        handle = open_for_read(name);
    }
    const uint32_t object = ld32(request + REQUEST_FILE_OBJECT);

    if (handle == 0) {
        // A refused open is the most interesting line this trace can carry — it is what the game
        // does *next* that tells you what it wanted — so it is printed like any other.
        trace(writing ? "open-w" : "open-r", name, mode, 0, 0);
        st32(request + REQUEST_STATUS, STATUS_FAILED);
        queue_completion(request);
        return 0;
    }
    if (object != 0) {
        files().handle_by_object[object] = handle;
    }
    st32(request + REQUEST_HANDLE, handle);
    const uint32_t buffer = ld32(request + REQUEST_BUFFER);
    const uint32_t length = ld32(request + REQUEST_LENGTH);
    // A write-mode open that carries a buffer puts it on disk — unless writes are refused, which
    // is the emulator's own state (its write-on-open path is off) and what the recordings hold.
    if (writing && buffer != 0 && length != 0 && !writes_refused()) {
        write_from_guest(handle, buffer, length);
    }

    // An open that carries a destination buffer *is* the load. Lost hands `#3` a 512 000-byte
    // buffer and never issues a read, because on the device there was nothing left to read; the
    // completion goes straight to collecting the data. The position is put back to zero
    // afterwards so a game that opens this way and then reads through `#2` — Mini Golf does —
    // still sees the whole file. (reference/eapp-loader/lib.rs, Stub::AsyncOpen.)
    const bool has_destination = buffer != 0 && length != 0;
    uint32_t result = 0;
    if (!writing && has_destination) {
        result = read_into_guest(handle, buffer, length);
        rewind(handle);
        st32(request + REQUEST_SIZE_RESULT, result);
    } else if (has_destination) {
        // A write-mode open moved nothing inward, and leaving whatever the game had here reads
        // back as a transfer that never happened. Zero is the truth.
        st32(request + REQUEST_SIZE_RESULT, 0);
    } else {
        // A bufferless open is asking one thing: how big is it. The size goes on the file object
        // rather than in the request, which is where the games that ask this way read it.
        const OpenFile* file = file_for(handle);
        result = file != nullptr ? static_cast<uint32_t>(file->bytes.size()) : 0;
    }
    if (object != 0) {
        st32(object + FILE_OBJECT_RESULT, result);
    }
    st32(request + REQUEST_STATUS, STATUS_OK);
    trace(writing ? "open-w" : "open-r", name, buffer, length, result);
    queue_completion(request);
    return handle;
}

// An operation with nothing to do but complete (close and friends).
uint32_t async_operation(uint32_t request) {
    st32(request + REQUEST_STATUS, STATUS_OK);
    trace("close", "", request, 0, 1);
    queue_completion(request);
    return 1;
}

// The body of #2: the request's own fields say whether this is a read, a seek or a skip.
// See storage.h `set_writes_refused`.
bool& writes_refused() {
    static bool refused = false;
    return refused;
}

uint32_t async_perform(uint32_t request) {
    const uint32_t buffer = ld32(request + REQUEST_BUFFER);
    const uint32_t length = ld32(request + REQUEST_LENGTH);
    const uint32_t object = ld32(request + REQUEST_FILE_OBJECT);
    const uint32_t operation = ld8(request + REQUEST_OPERATION);
    const auto found = files().handle_by_object.find(object);
    const uint32_t handle = found == files().handle_by_object.end() ? 0 : found->second;

    // Which way a transfer goes is the *file's* business, not the operation's. The game asks for
    // a transfer with the same operation whichever it means — this title uses 3 for every one of
    // them — and what decides the direction is the mode the file was opened with. Reading a file
    // that was opened to be written moves the bytes exactly the wrong way and leaves the save
    // empty, which is a fault with no symptom until a restart.
    const OpenFile* opened = file_for(handle);
    const bool to_the_file = opened != nullptr && opened->writing;

    uint32_t got = 0;
    if (handle == 0) {
        got = 0;
    } else if (operation == OPERATION_SEEK) {
        const int32_t offset = static_cast<int32_t>(ld32(request + REQUEST_SEEK_OFFSET));
        const uint32_t whence = ld8(request + REQUEST_SEEK_WHENCE);
        st32(request + REQUEST_POSITION_RESULT, seek(handle, offset, whence));
    } else if (to_the_file) {
        // Refused, the transfer is what the emulator's catch-all read of a freshly opened,
        // empty write file amounts to: nothing moves, the position stays where it is.
        got = writes_refused() ? 0 : write_from_guest(handle, buffer, length);
        const OpenFile* file = file_for(handle);
        st32(request + REQUEST_POSITION_RESULT,
             file != nullptr ? static_cast<uint32_t>(file->position) : 0);
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
    // `+0x24` is the operation's byte count, and every transfer publishes it, not only an open
    // that loaded. The Sims Bowling's read completion at 0x180345c4 copies it to its stream
    // (`ldr r0,[r2,#0x14c] / str r0,[r2,#0x120]` — `stream+0x128` is the request) and its
    // resource library advances by it; left unwritten, a chunk of `gameLib.rlb` that arrived in
    // full advanced the resource by nothing and was fetched again forever
    // (reference/eapp-loader/lib.rs, the `+0x24` note in Stub::AsyncRead).
    st32(request + REQUEST_SIZE_RESULT, got);
    const bool ok = operation == OPERATION_SEEK || length == 0 || (got == length && got > 0);
    trace(to_the_file ? "write" : "read", opened == nullptr ? "" : opened->name, operation, length,
          ok ? got : ~0u);
    st32(request + REQUEST_STATUS, ok ? STATUS_OK : STATUS_FAILED);
    queue_completion(request);
    // A seek moved no bytes, and answering its byte count says "failed" to a caller that only
    // checks for non-zero: The Sims Bowling's library asks for the first 4 KB of `gameLib.rlb`
    // through 0x1800432c, reads 0 back, and re-asks on the next frame forever. The operation was
    // queued; say so (lib.rs, the same stub's last lines).
    if (got == 0 && operation == OPERATION_SEEK) {
        return 1;
    }
    return got;
}

}  // namespace

const std::string& game_directory() {
    return files().game_dir;
}
void set_game_directory(const std::string& path) {
    files().game_dir = path;
}

}  // namespace vortex::eapp

namespace vortex::storage {

using namespace vortex::eapp;  // NOLINT(google-build-using-namespace): one file, by design

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

void set_writes_refused(bool refused) {
    writes_refused() = refused;
}

void set_store_stubbed(bool stubbed) {
    store_stubbed = stubbed;
}

uint32_t store_open(uint32_t mode, GuestAddress path, GuestAddress record) {
    log_call("AsyncFileIO", 12, {mode, path, record});
    if (store_stubbed) {
        return STORE_STUB_ANSWER;
    }
    const std::string name = read_guest_string(path, 256);
    const uint32_t handle = open_store_entry(name);
    trace("store-open", name, mode, 0, handle);
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
    const StoreEntry* entry = store_entry_for(handle);
    const std::string name = entry != nullptr ? entry->name : std::string();
    const uint32_t size = entry != nullptr ? static_cast<uint32_t>(entry->bytes.size()) : 0;
    const bool kept = commit_store_entry(handle);
    trace("store-close", name, handle, size, kept ? 1 : 0);
    return kept ? STORE_OK : STORE_REFUSED;
}

// `data` is a guest address and `size` a byte count, whatever the ordinal's arguments were
// called: the game hands over the whole save in one call.
uint32_t store_write(uint32_t handle, uint32_t data, uint32_t size) {
    log_call("AsyncFileIO", 16, {handle, data, size});
    if (store_stubbed) {
        return STORE_STUB_ANSWER;
    }
    const StoreEntry* entry = store_entry_for(handle);
    const bool wrote = append_to_store_entry(handle, data, size);
    trace("store-write", entry != nullptr ? entry->name : std::string(), handle, size,
          wrote ? size : 0);
    return wrote ? STORE_OK : STORE_REFUSED;
}

}  // namespace vortex::storage
