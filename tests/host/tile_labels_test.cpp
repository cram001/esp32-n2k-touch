#include "tile_labels.hpp"
#include "nvs_fake.hpp"
#include <cassert>
#include <cstring>
#include <cstdio>
int main(){
 fake_nvs::clear();TileLabels labels;assert(labels.load());assert(!labels.get(0)[0]);
 fake_nvs::durable["display_v1"]={1,2,3};const auto original=fake_nvs::durable;
 assert(labels.save(0,"House battery"));assert(labels.save(35,"Port solar"));
 for(const auto &item:original)assert(fake_nvs::durable.at(item.first)==item.second);
 TileLabels reboot;assert(reboot.load());assert(!std::strcmp(reboot.get(0),"House battery"));assert(!std::strcmp(reboot.get(35),"Port solar"));
 const auto saved=fake_nvs::durable;fake_nvs::fail_commit=true;assert(!reboot.save(0,"Changed"));
 assert(fake_nvs::durable==saved && !std::strcmp(reboot.get(0),"House battery"));
 fake_nvs::fail_commit=false;assert(!reboot.save(36,"Bad"));assert(!reboot.save(0,"123456789012345678901"));assert(reboot.save(0,""));
 assert(!reboot.get(0)[0]);fake_nvs::durable["tile_labels_v1"][0]=255;const auto future=fake_nvs::durable;
 TileLabels unknown;assert(!unknown.load());assert(fake_nvs::durable==future && !unknown.get(35)[0]);
 puts("PASS: tile label persistence, clear/default, bounds, failed commit, unknown schema and unrelated NVS preservation");
}
