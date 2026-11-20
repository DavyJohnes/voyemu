#include "boot_id.h"
static int hex(char c)
{
    if (c >= '0' && c <= '9') return c-'0';
    if (c >= 'a' && c <= 'f') return c-'a'+10;
    return -1;
}
bool parse_boot_id(const char *text, size_t length, uint8_t output[16])
{
    uint8_t id[16]; unsigned pos=0, byte=0;
    if (length != 37 || text[36] != '\n') return false;
    while (pos < 36) {
        int hi, lo;
        if (pos==8 || pos==13 || pos==18 || pos==23) {
            if (text[pos++] != '-') return false;
            continue;
        }
        hi=hex(text[pos++]); lo=hex(text[pos++]);
        if (hi < 0 || lo < 0 || byte >= sizeof(id)) return false;
        id[byte++] = (uint8_t)((hi<<4)|lo);
    }
    if (byte != sizeof(id)) return false;
    for (byte=0; byte<sizeof(id); ++byte) output[byte]=id[byte];
    return true;
}
