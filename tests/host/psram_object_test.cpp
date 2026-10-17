#include "psram_object.hpp"
#include <array>
#include <cassert>
#include <cstdio>
#include <cstring>
static bool fail=false,external=true;
static unsigned allocations=0,releases=0,constructors=0,destructors=0;
void *heap_caps_malloc(size_t bytes,uint32_t caps){
    assert(caps==(MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT));++allocations;
    if(fail)return nullptr;
    void *memory=std::malloc(bytes);assert(memory);std::memset(memory,0xa5,bytes);return memory;
}
void heap_caps_free(void *memory){assert(memory);++releases;std::free(memory);}
bool esp_ptr_external_ram(const void *){return external;}
struct Table {
    std::array<uint64_t,254> names{};
    std::array<uint32_t,254> targets{};
    unsigned source=255;
    Table(){++constructors;targets.fill(UINT32_MAX);}
    ~Table(){++destructors;}
};
int main(){
    {
        PsramObject<Table> table;
        fail=true;assert(!table.initialize()&&!table.get());
        assert(allocations==1&&constructors==0&&releases==0);
        fail=false;external=false;assert(!table.initialize()&&!table.get());
        assert(allocations==2&&constructors==0&&releases==1);
        external=true;assert(table.initialize());auto *original=table.get();
        assert(constructors==1&&original->source==255);
        for(auto value:original->names)assert(value==0);
        for(auto value:original->targets)assert(value==UINT32_MAX);
        original->names[253]=42;
        for(unsigned i=0;i<1000;++i){assert(table.initialize()&&table.get()==original);}
        assert(table.get()->names[253]==42&&allocations==3&&constructors==1);
    }
    assert(releases==2&&destructors==1);
    std::puts("PASS: strict PSRAM/8-bit capabilities, allocation failure without fallback, residency rejection, constructor defaults, retry, one allocation and cleanup");
}
