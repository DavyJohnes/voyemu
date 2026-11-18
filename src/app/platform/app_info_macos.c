// Embeds the Info.plist bytes as the __TEXT,__info_plist section; the Zig linker lacks -sectcreate.
__asm__(".section __TEXT,__info_plist,regular,no_dead_strip\n"
        ".incbin \"" VOYEMU_INFO_PLIST "\"\n"
        ".previous\n");
