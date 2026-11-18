#include "boot_id.h"
#include <assert.h>
#include <string.h>
int main(void)
{
    const char valid[]="01234567-89ab-cdef-0123-456789abcdef\n";
    char bad[sizeof(valid)]; uint8_t id[16];
    assert(parse_boot_id(valid,sizeof(valid)-1,id));
    assert(id[0]==0x01 && id[15]==0xef);
    assert(!parse_boot_id(valid,sizeof(valid)-2,id));
    memcpy(bad,valid,sizeof(valid)); bad[8]='x'; assert(!parse_boot_id(bad,sizeof(bad)-1,id));
    memcpy(bad,valid,sizeof(valid)); bad[0]='Z'; assert(!parse_boot_id(bad,sizeof(bad)-1,id));
    return 0;
}
