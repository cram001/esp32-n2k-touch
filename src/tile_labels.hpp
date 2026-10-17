#pragma once
#include "app_settings.hpp"
#include <array>

// UI-owned; allocate in PSRAM. Never included in transport settings snapshots.
struct TileLabels {
    static constexpr size_t Count=MAX_DATA_PAGES*MAX_DATA_FIELDS_PER_PAGE;
    static constexpr size_t Length=21; // 20 UTF-8 bytes plus terminator.
    uint32_t schema=1;
    std::array<std::array<char,Length>,Count> values{};
    bool load();
    bool save(size_t field,const char *text);
    const char *get(size_t field)const{return field<Count?values[field].data():"";}
};
static_assert(sizeof(TileLabels)==760,"Tile label v1 ABI changed");
