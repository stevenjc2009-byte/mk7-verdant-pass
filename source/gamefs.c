#include "gamefs.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const char *s_error = "";

const char *vp_gamefs_error(void) { return s_error; }

#ifdef __3DS__

#include <3ds.h>

#define MOUNT_NAME "mk7game"

// Mario Kart 7's title IDs, the same list Hotswap carries. These are no longer
// the primary way the game is found - the title database is asked first - but
// they are still the fallback for a console where that lookup does not answer.
static const u64 TITLE_IDS[] = {
    0x0004000000030700ULL,   // Europe
    0x0004000000030800ULL,   // Americas
    0x0004000000030600ULL,   // Japan
    0x0004000000030A00ULL,   // Korea
};
#define TITLE_COUNT (int)(sizeof(TITLE_IDS) / sizeof(TITLE_IDS[0]))

// Every copy of Mario Kart 7 carries this in its NCCH header - CTR-P-AMKP for
// Europe, ...E for the Americas, ...J for Japan, ...K for Korea. Measured out of
// a real dump rather than taken on trust.
//
// The list above is four guesses; this is a question the console can answer. A
// variant with a title ID nobody wrote down still says CTR-P-AMK, so asking the
// title database what it has beats hoping the right number was hardcoded.
#define MK7_PRODUCT_PREFIX "CTR-P-AMK"

// The high half of every 3DS game's title ID. Filtering on it keeps the product
// code lookup off the two hundred-odd system titles a console carries.
#define GAME_TITLE_HIGH 0x00040000ULL

static bool s_mounted = false;
static char s_detail[256];

static bool isKnownMk7Id(u64 id)
{
    for (int i = 0; i < TITLE_COUNT; i++)
        if (id == TITLE_IDS[i]) return true;
    return false;
}

// Asks the title database which title on this media is Mario Kart 7, and hands
// back its ID.
//
// Two reasons this beats trying the four hardcoded IDs blind. It finds a copy
// whose title ID is not in that list - the product code is the game's own name
// for itself and does not vary the way region IDs do. And when it finds nothing,
// that is real evidence the game is absent, so the app can stop telling a player
// who owns the game that they do not.
static bool findGameOn(FS_MediaType media, u64 *found)
{
    if (R_FAILED(amInit())) return false;

    u32 count = 0;
    bool present = false;

    if (R_SUCCEEDED(AM_GetTitleCount(media, &count)) && count > 0)
    {
        u64 *ids = (u64 *)malloc(count * sizeof(u64));
        if (ids)
        {
            u32 read = 0;
            if (R_SUCCEEDED(AM_GetTitleList(&read, media, count, ids)))
            {
                for (u32 t = 0; t < read && !present; t++)
                {
                    if ((ids[t] >> 32) != GAME_TITLE_HIGH) continue;

                    // A known ID is enough on its own; otherwise ask the title
                    // what it calls itself.
                    if (isKnownMk7Id(ids[t]))
                    {
                        present = true;
                    }
                    else
                    {
                        char code[16] = { 0 };
                        if (R_SUCCEEDED(AM_GetTitleProductCode(media, ids[t], code)) &&
                            strncmp(code, MK7_PRODUCT_PREFIX, strlen(MK7_PRODUCT_PREFIX)) == 0)
                            present = true;
                    }

                    if (present && found) *found = ids[t];
                }
            }
            free(ids);
        }
    }

    amExit();
    return present;
}

const char *vp_gamefs_open(void)
{
    if (s_mounted) return MOUNT_NAME ":/";
    s_error = "";

    // SD first: a digital copy is the common case, and checking the card slot
    // first would spin up a cartridge read for nothing on most consoles.
    static const FS_MediaType MEDIA[] = { MEDIATYPE_SD, MEDIATYPE_GAME_CARD };

    // The most informative thing the console said while trying. A title ID for a
    // region the player does not own answers "not found", which is expected and
    // says nothing; anything else is the real complaint and outranks it.
    Result told = 0;

    // Ask first, guess second. Whatever the title database names as Mario Kart 7
    // is tried before the hardcoded list, so a copy with an ID nobody wrote down
    // still opens.
    u64  found[2] = { 0, 0 };
    bool have[2]  = { false, false };

    for (int m = 0; m < 2; m++)
    {
        have[m] = findGameOn(MEDIA[m], &found[m]);
        if (!have[m]) continue;

        Result rc = romfsMountFromTitle(found[m], MEDIA[m], MOUNT_NAME);
        if (R_SUCCEEDED(rc))
        {
            s_mounted = true;
            return MOUNT_NAME ":/";
        }
        told = rc;
    }

    // The title database did not answer, or what it named would not open. Try
    // the four known IDs blind - this is what the app did before and it costs
    // nothing, so it stays as the floor rather than the plan.
    for (int m = 0; m < 2; m++)
    {
        for (int i = 0; i < TITLE_COUNT; i++)
        {
            Result rc = romfsMountFromTitle(TITLE_IDS[i], MEDIA[m], MOUNT_NAME);
            if (R_SUCCEEDED(rc))
            {
                s_mounted = true;
                return MOUNT_NAME ":/";
            }

            if (told == 0 || (R_DESCRIPTION(told) == RD_NOT_FOUND &&
                              R_DESCRIPTION(rc) != RD_NOT_FOUND))
                told = rc;
        }
    }

    // Nothing mounted. Say which of the two stories is the true one rather than
    // the one that is usually true - "you do not have the game" sends a player
    // who does have it off fixing the wrong thing.
    if (have[0] || have[1])
        snprintf(s_detail, sizeof(s_detail),
                 "Mario Kart 7 is here (%016llX, %s) but this app could not open "
                 "it. The console said 0x%08lX.",
                 (unsigned long long)(have[0] ? found[0] : found[1]),
                 have[0] ? "SD" : "cartridge", (unsigned long)told);
    else
        snprintf(s_detail, sizeof(s_detail),
                 "Mario Kart 7 was not found on this console. The game has to be "
                 "installed, or the cartridge in the slot. (0x%08lX)",
                 (unsigned long)told);

    s_error = s_detail;
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
