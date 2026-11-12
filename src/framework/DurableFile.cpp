// Copyright (C) 2026 DarkMatter Productions. GPL-3.0-or-later.
#include "DurableFile.h"

#include <algorithm>
#include <atomic>
#include <cerrno>
#include <cstdint>
#include <cstdio>
#include <limits>
#include <new>
#include <utility>

#include <fcntl.h>
#include <sys/file.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#if defined(__SWITCH__)
// Horizon has no openable directories; shims emulate directory descriptors.
extern "C" int switch_dirfd_open(const char *path);
extern "C" int switch_close(int fd);
extern "C" int switch_fsync(int fd);
#define close(fd) switch_close(fd)
#define fsync(fd) switch_fsync(fd)
#endif
#if defined(__linux__)
#include <sys/syscall.h>
#endif

namespace openq4 {
namespace durable_detail {

constexpr std::size_t MaxPathBytes = 32700;
constexpr std::size_t ChunkBytes = 64 * 1024;
constexpr unsigned TempAttempts = 32;

// Compile-time-only fault injection. The standalone test includes this source;
// engine builds contain no mutable hook or alternate operating-system backend.
enum class Point { OpenRead, Read, OpenTemp, Write, FileSync, CloseRead,
    CloseWrite, Rename, MetadataSync, Remove, Cleanup, CreatePublication };
#ifdef OPENQ4_DURABLE_FILE_TESTING
struct Faults {
    Point point = Point::OpenRead;
    int failOn = 0, calls = 0;
    std::size_t readChunk = ChunkBytes, writeChunk = ChunkBytes;
    bool zeroRead = false, zeroWrite = false;
    void (*beforeRead)() = nullptr;
    void (*beforeCreatePublication)() = nullptr;
};
thread_local Faults faults;
bool Fail(Point point) {
    return faults.failOn > 0 && point == faults.point && ++faults.calls == faults.failOn;
}
#else
bool Fail(Point) { return false; }
#endif

bool Error(std::string& error, const char* operation, unsigned long code = 0) {
    error = std::string(operation) + (code ? " (OS error " + std::to_string(code) + ")" : "");
    return false;
}

bool Utf8(const std::string& text) {
    for (std::size_t i = 0; i < text.size();) {
        const auto first = static_cast<unsigned char>(text[i++]);
        if (first == 0) return false;
        if (first < 0x80) continue;
        unsigned left;
        std::uint32_t value, minimum;
        if (first >= 0xc2 && first <= 0xdf) { left = 1; value = first & 0x1f; minimum = 0x80; }
        else if (first >= 0xe0 && first <= 0xef) { left = 2; value = first & 0xf; minimum = 0x800; }
        else if (first >= 0xf0 && first <= 0xf4) { left = 3; value = first & 7; minimum = 0x10000; }
        else return false;
        if (text.size() - i < left) return false;
        while (left--) {
            const auto next = static_cast<unsigned char>(text[i++]);
            if ((next & 0xc0) != 0x80) return false;
            value = (value << 6) | (next & 0x3f);
        }
        if (value < minimum || value > 0x10ffff || (value >= 0xd800 && value <= 0xdfff)) return false;
    }
    return true;
}

struct Path {
    std::string full, parent, leaf;
};

bool ParsePath(const std::string& input, Path& path, std::string& error) {
    if (input.empty() || input.size() > MaxPathBytes || !Utf8(input))
        return Error(error,"Invalid or overlong UTF-8 path");
    std::string normalized = input;
    std::size_t root = 1;
#if defined(__SWITCH__)
    // Horizon absolute paths carry a device prefix ("sdmc:/...").
    if (normalized.compare(0,6,"sdmc:/") == 0) root = 6;
    else if (normalized[0] != '/') return Error(error,"Path must be absolute");
#else
    if (normalized[0] != '/') return Error(error,"Path must be absolute");
#endif
    for (std::size_t begin = root; begin <= normalized.size();) {
        const auto end = normalized.find('/',begin);
        const auto count = (end == std::string::npos ? normalized.size() : end) - begin;
        const auto part = normalized.substr(begin,count);
        if (part.empty() || part == "." || part == "..")
            return Error(error,"Path must contain a file and no empty or dot components");
        if (end == std::string::npos) break;
        begin = end + 1;
    }
    path.full = normalized;
    const auto split = normalized.rfind('/');
    path.parent = split == 0 ? "/" : normalized.substr(0,split);
#if defined(__SWITCH__)
    if (split == 5 && normalized.compare(0,6,"sdmc:/") == 0) path.parent = "sdmc:/";
#endif
    path.leaf = normalized.substr(split + 1);
    return true;
}

using Native = int;
constexpr Native Invalid = -1;
unsigned long LastError() { return static_cast<unsigned long>(errno); }
bool MissingError(unsigned long code) { return code == ENOENT; }
void CloseRaw(Native handle) { close(handle); }

struct Handle {
    Native value = Invalid;
    Handle() = default;
    explicit Handle(Native v) : value(v) {}
    Handle(const Handle&) = delete;
    Handle& operator=(const Handle&) = delete;
    ~Handle() { if (value != Invalid) CloseRaw(value); }
    bool Close(std::string& error, Point point) {
        if (value == Invalid) return true;
        const auto v = value; value = Invalid;
        // Do not retry close(EINTR): on Linux the descriptor has been released
        // and could already belong to another thread. Treat its state as failed.
        const bool okay = close(v) == 0;
        const auto code = okay ? 0 : LastError();
        if (!okay || Fail(point)) return Error(error,"File close failed",code);
        return true;
    }
};

bool Regular(Native file, std::uint64_t& size, std::string& error) {
    struct stat info{};
    if (fstat(file,&info) != 0) return Error(error,"Cannot inspect regular file",errno);
    if (!S_ISREG(info.st_mode) || info.st_size < 0) return Error(error,"Target is not a regular file");
    size = static_cast<std::uint64_t>(info.st_size);
    return true;
}

bool ReadChunk(Native file, char* bytes, std::size_t request, std::size_t& count, std::string& error) {
    if (Fail(Point::Read)) return Error(error,"File read failed (injected)");
#ifdef OPENQ4_DURABLE_FILE_TESTING
    if (faults.zeroRead) { count = 0; return true; }
    request = (std::min)(request,faults.readChunk);
    if (faults.beforeRead) { auto callback = faults.beforeRead; faults.beforeRead = nullptr; callback(); }
#endif
    ssize_t got;
    do { got = read(file,bytes,request); } while (got < 0 && errno == EINTR);
    if (got < 0) return Error(error,"File read failed",errno);
    count = static_cast<std::size_t>(got);
    return true;
}

bool WriteAll(Native file, const std::string& bytes, std::string& error) {
    std::size_t offset = 0;
    while (offset < bytes.size()) {
        auto request = (std::min)(ChunkBytes,bytes.size()-offset);
        if (Fail(Point::Write)) return Error(error,"File write failed (injected)");
#ifdef OPENQ4_DURABLE_FILE_TESTING
        if (faults.zeroWrite) return Error(error,"File write made no progress");
        request = (std::min)(request,faults.writeChunk);
#endif
        ssize_t wrote;
        do { wrote = write(file,bytes.data()+offset,request); } while (wrote < 0 && errno == EINTR);
        if (wrote < 0) return Error(error,"File write failed",errno);
        if (wrote == 0) return Error(error,"File write made no progress");
        offset += static_cast<std::size_t>(wrote);
    }
    return true;
}

bool Sync(Native file, Point point, std::string& error) {
    if (Fail(point)) return Error(error,point == Point::MetadataSync ?
        "Namespace durability could not be confirmed (injected)" : "File sync failed (injected)");
    int result;
    do { result = fsync(file); } while (result != 0 && errno == EINTR);
    if (result != 0) return Error(error,point == Point::MetadataSync ?
        "Namespace durability could not be confirmed" : "File sync failed",errno);
    return true;
}

std::atomic<std::uint64_t> tempSequence{0};
std::string TempLeaf() {
    const auto pid = static_cast<unsigned long>(getpid());
    // CREATE_NEW/O_EXCL is the ownership proof, not predictability of the name.
    // Bounded collision retries never delete a file left by a different call.
    return ".openq4-durable-" + std::to_string(pid) + "-" +
        std::to_string(tempSequence.fetch_add(1,std::memory_order_relaxed)) + ".tmp";
}

bool OpenParent(const Path& path, Handle& parent, std::string& error, bool* missing = nullptr) {
#if defined(__SWITCH__)
    parent.value = switch_dirfd_open(path.parent.c_str());
#else
    parent.value = open(path.parent.c_str(),O_RDONLY | O_DIRECTORY | O_CLOEXEC);
#endif
    if (parent.value == Invalid) {
        const auto code = errno;
        if (missing) *missing = code == ENOENT;
        return Error(error,"Cannot open parent directory",code);
    }
    return true;
}

struct Temp {
    Handle file;
    std::string name;
    Native parent = Invalid;
    bool owned = false;
    ~Temp() {
        if (!owned) return;
        if (file.value != Invalid) { CloseRaw(file.value); file.value = Invalid; }
        unlinkat(parent,name.c_str(),0);
        // Cleanup is best effort and never changes the authoritative target.
        // A crash/failure can leave this one inert, uniquely owned temp behind.
    }
    bool Create(const Path& path, Native parentHandle, std::string& error) {
        if (Fail(Point::OpenTemp)) return Error(error,"Temporary file creation failed (injected)");
        for (unsigned attempt = 0; attempt < TempAttempts; ++attempt) {
            const auto leaf = TempLeaf();
            (void)path;
            parent = parentHandle;
            name = leaf;
            file.value = openat(parent,name.c_str(),O_RDWR | O_CREAT | O_EXCL | O_CLOEXEC | O_NOFOLLOW,0600);
            const auto code = errno;
            const bool collision = code == EEXIST;
            if (file.value != Invalid) { owned = true; return true; }
            if (!collision) return Error(error,"Temporary file creation failed",code);
        }
        return Error(error,"Temporary file collision budget exhausted");
    }
};

bool InspectTarget(const Path& path, Native parent, bool& missing, std::string& error) {
    missing = false;
    struct stat info{};
    if (fstatat(parent,path.leaf.c_str(),&info,AT_SYMLINK_NOFOLLOW) != 0) {
        if (errno == ENOENT) { missing = true; return true; }
        return Error(error,"Cannot inspect target",errno);
    }
    if (!S_ISREG(info.st_mode)) return Error(error,"Target is not a regular file");
    return true;
}

DurableReadResult Read(const Path& path, std::size_t budget, std::string& out, std::string& error) {
    if (Fail(Point::OpenRead)) { Error(error,"File open failed (injected)"); return DurableReadResult::Failed; }
    // O_NONBLOCK avoids hanging before the regular-file check on a FIFO.
    Handle file(open(path.full.c_str(),O_RDONLY | O_CLOEXEC | O_NOFOLLOW | O_NONBLOCK));
    if (file.value == Invalid) {
        const auto code = LastError();
        if (MissingError(code)) { error.clear(); return DurableReadResult::Missing; }
        Error(error,"File open failed",code); return DurableReadResult::Failed;
    }
    std::uint64_t size;
    if (!Regular(file.value,size,error)) return DurableReadResult::Failed;
    if (size > budget) { Error(error,"File exceeds read budget"); return DurableReadResult::Failed; }
    std::string candidate(static_cast<std::size_t>(size),'\0');
    std::size_t offset = 0;
    while (offset < candidate.size()) {
        std::size_t got = 0;
        if (!ReadChunk(file.value,candidate.data()+offset,(std::min)(ChunkBytes,candidate.size()-offset),got,error))
            return DurableReadResult::Failed;
        if (got == 0) { Error(error,"File shortened during read"); return DurableReadResult::Failed; }
        offset += got;
    }
    char extra;
    std::size_t got = 0;
    if (!ReadChunk(file.value,&extra,1,got,error)) return DurableReadResult::Failed;
    std::uint64_t finalSize;
    if (!Regular(file.value,finalSize,error)) return DurableReadResult::Failed;
    if (got != 0 || finalSize != size) { Error(error,"File changed size during read"); return DurableReadResult::Failed; }
    if (!file.Close(error,Point::CloseRead)) return DurableReadResult::Failed;
    out.swap(candidate); error.clear(); return DurableReadResult::Present;
}

bool Replace(const Path& path, const std::string& bytes, std::string& error) {
    Handle parent;
    if (!OpenParent(path,parent,error)) return false;
    bool missing;
    if (!InspectTarget(path,parent.value,missing,error)) return false;
    Temp temp;
    if (!temp.Create(path,parent.value,error) || !WriteAll(temp.file.value,bytes,error) ||
        !Sync(temp.file.value,Point::FileSync,error) || !temp.file.Close(error,Point::CloseWrite)) return false;
    if (Fail(Point::Rename)) return Error(error,"Replacement rename failed (injected)");
    if (renameat(parent.value,temp.name.c_str(),parent.value,path.leaf.c_str()) != 0)
        return Error(error,"Replacement rename failed",errno);
    temp.owned = false;
    if (!Sync(parent.value,Point::MetadataSync,error)) return false;
    error.clear(); return true;
}

DurableCreateResult Create(const Path& path, const std::string& bytes, std::string& error) {
    Handle parent;
    if (!OpenParent(path,parent,error)) return DurableCreateResult::Failed;
    bool missing;
    if (!InspectTarget(path,parent.value,missing,error)) return DurableCreateResult::Failed;
    if (!missing) { error.clear(); return DurableCreateResult::Exists; }
    Temp temp;
    if (!temp.Create(path,parent.value,error) || !WriteAll(temp.file.value,bytes,error) ||
        !Sync(temp.file.value,Point::FileSync,error) || !temp.file.Close(error,Point::CloseWrite))
        return DurableCreateResult::Failed;
    if (Fail(Point::CreatePublication)) {
        Error(error,"New-file publication failed (injected)"); return DurableCreateResult::Failed;
    }
#ifdef OPENQ4_DURABLE_FILE_TESTING
    if (faults.beforeCreatePublication) {
        auto callback=faults.beforeCreatePublication;faults.beforeCreatePublication=nullptr;callback();
    }
#endif
    // The earlier existence check is only an optimization. The native operation
    // itself refuses an occupied name, including one created after that check.
    if (linkat(parent.value,temp.name.c_str(),parent.value,path.leaf.c_str(),0)!=0) {
        const auto code=errno;
        if (code==EEXIST) { error.clear(); return DurableCreateResult::Exists; }
        Error(error,"New-file publication failed; publication/durability may be uncertain",code);
        return DurableCreateResult::Failed;
    }
    // Only our newly created temp has a second name. Never remove or replace
    // the target on failure; it may already be visible to another process.
    if (Fail(Point::Cleanup) || unlinkat(parent.value,temp.name.c_str(),0)!=0) {
        Error(error,"New file published; temporary cleanup/durability unconfirmed");
        return DurableCreateResult::Failed;
    }
    temp.owned=false;
    if (!Sync(parent.value,Point::MetadataSync,error)) return DurableCreateResult::Failed;
    error.clear(); return DurableCreateResult::Created;
}

bool Remove(const Path& path, std::string& error) {
    Handle parent;
    bool missingParent = false;
    if (!OpenParent(path,parent,error,&missingParent)) {
        if (missingParent) { error.clear(); return true; }
        return false;
    }
    bool missing;
    if (!InspectTarget(path,parent.value,missing,error)) return false;
    if (missing) {
        // Also makes a retried unlink's previously uncertain absence durable.
        if (!Sync(parent.value,Point::MetadataSync,error)) return false;
        error.clear(); return true;
    }
    if (Fail(Point::Remove)) return Error(error,"Removal failed (injected)");
    if (unlinkat(parent.value,path.leaf.c_str(),0) != 0) return Error(error,"Removal failed",errno);
    if (!Sync(parent.value,Point::MetadataSync,error)) return false;
    error.clear(); return true;
}

// Refuses an occupied destination atomically where the kernel and filesystem
// can. Android's app seccomp policy predates renameat2, so it is never called
// there; filesystems without an exclusive rename fall back to renameat after
// the caller's absence check.
bool RenameNoReplace(Native parent, const std::string& from, const std::string& to, std::string& error) {
#if defined(__linux__) && defined(SYS_renameat2)
    constexpr unsigned renameNoReplace = 1;
    if (syscall(SYS_renameat2,parent,from.c_str(),parent,to.c_str(),renameNoReplace) == 0) return true;
    if (errno == EEXIST) return Error(error,"Move destination is occupied");
    if (errno != EINVAL && errno != ENOSYS && errno != ENOTSUP && errno != EOPNOTSUPP)
        return Error(error,"Move rename failed",errno);
#endif
    if (renameat(parent,from.c_str(),parent,to.c_str()) != 0) return Error(error,"Move rename failed",errno);
    return true;
}

bool Move(const Path& from, const Path& to, bool& moved, std::string& error) {
    moved = false;
    // Callers derive both names from one directory; a differently spelled
    // parent refuses rather than guessing at case or link equivalence.
    if (from.parent != to.parent) return Error(error,"Durable move must stay within one directory");
    Handle parent;
    if (!OpenParent(from,parent,error)) return false;
    bool missing;
    if (!InspectTarget(from,parent.value,missing,error)) return false;
    if (missing) return Error(error,"Move source is missing");
    if (!InspectTarget(to,parent.value,missing,error)) return false;
    if (!missing) return Error(error,"Move destination is occupied");
    if (Fail(Point::Rename)) return Error(error,"Move rename failed (injected)");
    if (!RenameNoReplace(parent.value,from.leaf,to.leaf,error)) return false;
    moved = true;
    if (!Sync(parent.value,Point::MetadataSync,error)) return false;
    error.clear(); return true;
}

} // namespace durable_detail

DurableReadResult DurableReadExact(const std::string& path, std::size_t maxBytes,
    std::string& outBytes, std::string& error) {
    using namespace durable_detail;
    try {
        Path parsed;
        if (maxBytes > DurableFileMaxBytes || !ParsePath(path,parsed,error)) {
            if (maxBytes > DurableFileMaxBytes) Error(error,"Read budget exceeds durable-file limit");
            return DurableReadResult::Failed;
        }
        return Read(parsed,maxBytes,outBytes,error);
    } catch (const std::bad_alloc&) {
        Error(error,"Durable read allocation failed"); return DurableReadResult::Failed;
    }
}

bool DurableReplaceExact(const std::string& path, const std::string& bytes, std::string& error) {
    using namespace durable_detail;
    try {
        Path parsed;
        if (bytes.size() > DurableFileMaxBytes) return Error(error,"Replacement exceeds durable-file limit");
        return ParsePath(path,parsed,error) && Replace(parsed,bytes,error);
    } catch (const std::bad_alloc&) { return Error(error,"Durable replacement allocation failed"); }
}

DurableCreateResult DurableCreateExact(const std::string& path, const std::string& bytes, std::string& error) {
    using namespace durable_detail;
    try {
        Path parsed;
        if (bytes.size()>DurableFileMaxBytes) {
            Error(error,"New file exceeds durable-file limit"); return DurableCreateResult::Failed;
        }
        if (!ParsePath(path,parsed,error)) return DurableCreateResult::Failed;
        return Create(parsed,bytes,error);
    } catch (const std::bad_alloc&) {
        Error(error,"Durable new-file allocation failed"); return DurableCreateResult::Failed;
    }
}

bool DurableMoveExact(const std::string& from, const std::string& to, bool& moved, std::string& error) {
    using namespace durable_detail;
    moved = false;
    try {
        Path source, destination;
        return ParsePath(from,source,error) && ParsePath(to,destination,error) &&
            Move(source,destination,moved,error);
    } catch (const std::bad_alloc&) { return Error(error,"Durable move allocation failed"); }
}

bool DurableRemoveExact(const std::string& path, std::string& error) {
    using namespace durable_detail;
    try {
        Path parsed;
        return ParsePath(path,parsed,error) && Remove(parsed,error);
    } catch (const std::bad_alloc&) { return Error(error,"Durable removal allocation failed"); }
}

struct DurableFileLease::Impl {
    durable_detail::Handle file;
};

DurableFileLease::DurableFileLease() = default;
DurableFileLease::~DurableFileLease() = default;

bool DurableFileLease::TryAcquire(const std::string& path, std::string& error) {
    using namespace durable_detail;
    if (IsHeld()) return Error(error,"Durable file lease is already held by this instance");
    try {
        Path parsed;
        if (!ParsePath(path,parsed,error)) return false;
        auto candidate = std::make_unique<Impl>();
        candidate->file.value = open(parsed.full.c_str(),O_RDWR | O_CREAT | O_CLOEXEC | O_NOFOLLOW | O_NONBLOCK,0600);
        if (candidate->file.value == Invalid) return Error(error,"Cannot acquire durable file lease",LastError());
        std::uint64_t size;
        if (!Regular(candidate->file.value,size,error)) return false;
        int result;
        do { result = flock(candidate->file.value,LOCK_EX | LOCK_NB); } while (result != 0 && errno == EINTR);
        if (result != 0) return Error(error,"Durable file lease is busy or unsupported",errno);
        impl = std::move(candidate);
        error.clear(); return true;
    } catch (const std::bad_alloc&) { return Error(error,"Durable file lease allocation failed"); }
}

void DurableFileLease::Release() { impl.reset(); }
bool DurableFileLease::IsHeld() const { return impl && impl->file.value != durable_detail::Invalid; }

} // namespace openq4
