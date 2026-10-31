#include "CustomMaps/MapCatalog.h"

#include <algorithm>
#include <cerrno>
#include <cstring>
#include <cwchar>
#include <filesystem>
#include <new>
#if defined(_WIN32)
#include <io.h>
#include <windows.h>
#endif

#undef CompareString

#include "BML/ILogger.h"
#include "PathUtils.h"
#include "StringUtils.h"

namespace {
std::filesystem::path NativePath(const std::wstring &path) {
#if defined(_WIN32)
    return std::filesystem::path(path);
#else
    return std::filesystem::path(utils::Utf16ToUtf8(path));
#endif
}

std::wstring WidePath(const std::filesystem::path &path) {
#if defined(_WIN32)
    return path.wstring();
#else
    return utils::Utf8ToUtf16(path.string());
#endif
}
}

MapEntry::~MapEntry() {
    if (m_BeingDeleted)
        return;
    m_BeingDeleted = true;

    for (auto *child : children) {
        if (child && !child->m_BeingDeleted) {
            child->parent = nullptr;
            delete child;
        }
    }
    children.clear();

    if (parent && !parent->m_BeingDeleted) {
        const auto it = std::find(parent->children.begin(), parent->children.end(), this);
        if (it != parent->children.end())
            parent->children.erase(it);
    }
}

bool MapEntry::operator<(const MapEntry &rhs) const {
    if (type != rhs.type)
        return type < rhs.type;
    return utils::CompareStrings(name, rhs.name) < 0;
}

MapCatalog::MapCatalog() : m_Root(std::make_unique<MapEntry>(nullptr, MAP_ENTRY_DIR)) {}

bool MapCatalog::ResolveFile(const std::wstring &mapsDirectory, std::string_view relativePath,
                             std::wstring &path, std::string &error) {
    path.clear();
    error.clear();
    if (mapsDirectory.empty() || relativePath.empty()) {
        error = "provide a map path relative to ModLoader/Maps";
        return false;
    }

    std::wstring widePath;
    if (!utils::TryUtf8ToUtf16(relativePath, widePath)) {
        error = "the map path is not valid UTF-8";
        return false;
    }

    const std::filesystem::path requested(utils::Utf16ToUtf8(widePath));
    if (requested.has_root_path()) {
        error = "use a path relative to ModLoader/Maps";
        return false;
    }
    for (const auto &part : requested) {
        if (part == "..") {
            error = "parent-directory paths are not allowed";
            return false;
        }
    }
    if (!IsSupportedFileType(widePath)) {
        error = "only .nmo and .cmo maps can be loaded";
        return false;
    }

    const std::wstring candidate = WidePath(NativePath(mapsDirectory) / requested);
    return ValidateFile(mapsDirectory, candidate, path, error);
}

bool MapCatalog::ValidateFile(const std::wstring &mapsDirectory, const std::wstring &candidate,
                              std::wstring &path, std::string &error) {
    path.clear();
    error.clear();
    if (mapsDirectory.empty() || candidate.empty() || !IsSupportedFileType(candidate)) {
        error = "only .nmo and .cmo maps can be loaded";
        return false;
    }
    if (!utils::FileExistsW(candidate)) {
        error = "map file was not found";
        return false;
    }

    std::wstring filePath;
    if (!utils::TryGetFinalPathInsideRootW(candidate, mapsDirectory, filePath)) {
        error = "the map could not be resolved inside ModLoader/Maps";
        return false;
    }
    if (!IsSupportedFileType(filePath)) {
        error = "the resolved map path is not a supported file";
        return false;
    }

    path = std::move(filePath);
    return true;
}

bool MapCatalog::Refresh(const std::wstring &path, int maxDepth, ILogger *logger) {
    auto newRoot = std::unique_ptr<MapEntry>(new(std::nothrow) MapEntry(nullptr, MAP_ENTRY_DIR));
    if (!newRoot) {
        if (logger)
            logger->Error("Failed to allocate memory for maps root");
        return false;
    }

    newRoot->name = "Maps";
    newRoot->path = path;

    if (utils::PathExistsW(path) && !utils::DirectoryExistsW(path)) {
        if (logger)
            logger->Error("Maps path is not a directory: %s", utils::Utf16ToUtf8(path).c_str());
        return false;
    } else if (utils::DirectoryExistsW(path)) {
        std::wstring rootPath;
        if (!utils::TryGetFinalPathW(path, rootPath) ||
            ExploreMaps(newRoot.get(), maxDepth, logger, rootPath) == ScanResult::Failure)
            return false;
    }

    m_Root = std::move(newRoot);
    return true;
}

std::vector<std::string> MapCatalog::ListFiles(std::string_view fragment, std::size_t limit) const {
    std::vector<std::string> paths;
    if (!m_Root || m_Root->path.empty() || limit == 0)
        return paths;

    const std::string search(fragment);
    std::vector<const MapEntry *> pending{m_Root.get()};
    while (!pending.empty() && paths.size() < limit) {
        const MapEntry *entry = pending.back();
        pending.pop_back();
        if (entry->type == MAP_ENTRY_DIR) {
            for (auto it = entry->children.rbegin(); it != entry->children.rend(); ++it)
                pending.push_back(*it);
            continue;
        }

        if (entry->path.size() <= m_Root->path.size())
            continue;
        std::wstring relative = entry->path.substr(m_Root->path.size());
        if (!relative.empty() && (relative.front() == L'\\' || relative.front() == L'/'))
            relative.erase(relative.begin());

        const std::string name = utils::Utf16ToUtf8(relative);
        if (!utils::ContainsUtf8CaseInsensitive(name, search))
            continue;
        paths.push_back(name);
    }
    return paths;
}

MapCatalog::ScanResult MapCatalog::ExploreMaps(MapEntry *maps, int depth, ILogger *logger,
                                               const std::wstring &rootPath) {
    if (!maps || maps->type != MAP_ENTRY_DIR || maps->path.empty())
        return ScanResult::Failure;
    if (depth <= 0)
        return ScanResult::Success;

    try {
        std::error_code ec;
        for (std::filesystem::directory_iterator it(NativePath(maps->path), ec), end;
             it != end && !ec; it.increment(ec)) {
            const std::wstring fileName = WidePath(it->path().filename());
            if (fileName == L"." || fileName == L"..")
                continue;
            const std::wstring fullPath = WidePath(it->path());

            std::wstring finalPath;
            if (!utils::TryGetFinalPathW(fullPath, finalPath) ||
                !utils::IsPathInsideRootW(finalPath, rootPath))
                continue;

            if (fileName.find_first_of(L"\\/") != std::wstring::npos ||
                fileName.find(L"..") != std::wstring::npos) {
                continue;
            }

            if (it->is_directory(ec)) {
                auto entry = std::unique_ptr<MapEntry>(new(std::nothrow) MapEntry(maps, MAP_ENTRY_DIR));
                if (!entry) {
                    if (logger)
                        logger->Error("Memory allocation failed for directory entry");
                    return ScanResult::Failure;
                }
                entry->name = utils::Utf16ToUtf8(fileName);
                entry->path = fullPath;
                maps->children.push_back(entry.get());
                MapEntry *child = entry.release();
                if (ExploreMaps(child, depth - 1, logger, rootPath) == ScanResult::Failure)
                    return ScanResult::Failure;
            } else if (it->is_regular_file(ec) && IsSupportedFileType(fileName) &&
                       IsSupportedFileType(finalPath)) {
                std::wstring filename = fileName;
                const size_t dotPos = filename.find_last_of(L'.');
                if (dotPos != std::wstring::npos)
                    filename = filename.substr(0, dotPos);

                auto entry = std::unique_ptr<MapEntry>(new(std::nothrow) MapEntry(maps, MAP_ENTRY_FILE));
                if (!entry) {
                    if (logger)
                        logger->Error("Memory allocation failed for file entry");
                    return ScanResult::Failure;
                }
                entry->name = utils::Utf16ToUtf8(filename);
                entry->path = fullPath;
                maps->children.push_back(entry.get());
                entry.release();
            }
        }
        if (ec) {
            if (logger) {
                logger->Error("Failed while enumerating maps directory %s: %s",
                              utils::Utf16ToUtf8(maps->path).c_str(),
                              ec.message().c_str());
            }
            return ScanResult::Failure;
        }
    } catch (const std::exception &e) {
        if (logger)
            logger->Error("Exception while exploring maps: %s", e.what());
        return ScanResult::Failure;
    } catch (...) {
        if (logger)
            logger->Error("Unknown exception while exploring maps");
        return ScanResult::Failure;
    }

    if (!maps->children.empty()) {
        try {
            std::sort(maps->children.begin(), maps->children.end(),
                      [](const MapEntry *lhs, const MapEntry *rhs) {
                          return lhs && rhs && *lhs < *rhs;
                      });
        } catch (...) {
            if (logger)
                logger->Error("Failed to sort map entries");
            return ScanResult::Failure;
        }
    }
    return ScanResult::Success;
}

bool MapCatalog::IsSupportedFileType(const std::wstring &path) {
    const size_t dotPos = path.find_last_of(L'.');
    if (path.empty() || dotPos == std::wstring::npos || dotPos >= path.length() - 1)
        return false;

    std::wstring extension = path.substr(dotPos);
    std::transform(extension.begin(), extension.end(), extension.begin(), ::towlower);
    return extension == L".nmo" || extension == L".cmo";
}
