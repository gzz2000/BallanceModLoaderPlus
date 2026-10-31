#include "Loader/StaticModRegistry.h"
#include <cstring>
#include <mutex>
#include <string>
#include <deque>

namespace {
struct Entry { std::string name; BMLStaticModFactory factory; };
std::deque<Entry> entries;
std::mutex mutex;
}

bool BML_RegisterStaticModFactory(const BMLStaticModFactory &factory) {
    if (!factory.name || !*factory.name || !factory.create || !factory.destroy)
        return false;
    std::lock_guard<std::mutex> lock(mutex);
    for (const auto &entry : entries)
        if (entry.name == factory.name)
            return entry.factory.create == factory.create && entry.factory.destroy == factory.destroy;
    entries.push_back({factory.name, factory});
    entries.back().factory.name = entries.back().name.c_str();
    return true;
}

std::vector<BMLStaticModFactory> BML_StaticModFactories() {
    std::lock_guard<std::mutex> lock(mutex);
    std::vector<BMLStaticModFactory> result;
    for (const auto &entry : entries) result.push_back(entry.factory);
    return result;
}
