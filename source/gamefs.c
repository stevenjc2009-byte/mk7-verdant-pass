#include "gamefs.h"

#include <stdio.h>

static const char *s_error = "";

const char *vp_gamefs_error(void) { return s_error; }

#ifdef __3DS__

#include <3ds.h>

#define MOUNT_NAME "mk7game"

// Mario Kart 7's title IDs. Same list Hotswap carries, and for the same reason:
// there is no way to ask the console "which region of this game do you have",
// so each is tried in turn.
static const u64 TITLE_IDS[] = {
    0x0004000000030700ULL,   // Europe
    0x0004000000030800ULL,   // Americas
    0x0004000000030600ULL,   // Japan
    0x0004000000030A00ULL,   // Korea
};
#define TITLE_COUNT (int)(sizeof(TITLE_IDS) / sizeof(TITLE_IDS[0]))

static bool s_mounted = false;

const char *vp_gamefs_open(void)
{
    if (s_mounted) return MOUNT_NAME ":/";
    s_error = "";

    // SD first: a digital copy is the common case, and checking the card slot
    // first would spin up a cartridge read for nothing on most consoles.
    static const FS_MediaType MEDIA[] = { MEDIATYPE_SD, MEDIATYPE_GAME_CARD };

    for (int m = 0; m < 2; m++)
    {
        for (int i = 0; i < TITLE_COUNT; i++)
        {
            if (R_SUCCEEDED(romfsMountFromTitle(TITLE_IDS[i], MEDIA[m], MOUNT_NAME)))
            {
                s_mounted = true;
                return MOUNT_NAME ":/";
            }
        }
    }

    s_error = "Mario Kart 7 was not found on this console. "
              "The game has to be installed, or the cartridge in the slot.";
    return NULL;
}

void vp_gamefs_close(void)
{
    if (!s_mounted) return;
    romfsUnmount(MOUNT_NAME);
    s_mounted = false;
}

#else

// Host build. The test harness points this at the reference ROM extract so the
// install runs against real game files rather than a stand-in - the assembly is
// the whole product, and a fake input would prove nothing about it.
#ifndef GAME_ROMFS
#error "host build of gamefs.c needs GAME_ROMFS"
#endif

const char *vp_gamefs_open(void)
{
    s_error = "";
    return GAME_ROMFS "/";
}

void vp_gamefs_close(void) { }

#endif
