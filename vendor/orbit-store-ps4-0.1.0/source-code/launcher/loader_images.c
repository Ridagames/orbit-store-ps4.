/* The starter payload managers run (launcher/starter.c) and the pinned elfldr (a481740) that it,
 * restarts and the TV app send to GoldHEN when no ELF loader answers (backend/loader_start.c).
 * Saved on the console by backend/autoboot.c. */
extern const unsigned char orbit_starter_image[], orbit_starter_image_end[];
extern const unsigned char orbit_elfldr_image[], orbit_elfldr_image_end[];
__asm__(".section .rodata\n"
        ".global orbit_starter_image\n"
        ".global orbit_starter_image_end\n"
        ".global orbit_elfldr_image\n"
        ".global orbit_elfldr_image_end\n"
        ".balign 16\n"
        "orbit_starter_image:\n"
        ".incbin \"build/orbit_starter.elf\"\n"
        "orbit_starter_image_end:\n"
        ".balign 16\n"
        "orbit_elfldr_image:\n"
        ".incbin \".deps/elfldr/elfldr.elf\"\n"
        "orbit_elfldr_image_end:\n"
        ".previous\n");
