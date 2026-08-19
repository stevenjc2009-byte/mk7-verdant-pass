// Getting at the player's own copy of Mario Kart 7.
//
// This app ships none of Nintendo's data. It carries the three files this
// project generated and reads everything else out of the game already on the
// console, so the finished track is assembled from the player's own cartridge
// or download. That means mounting MK7's RomFS from its title, which libctru
// can do directly with romfsMountFromTitle - no dumping, no decryption, and
// nothing left on the card afterwards.
//
// The region is not known in advance, so all four are tried, on the SD card
// first and then the cartridge slot. A player with the game as a card and a
// different region's demo installed should still get the right one.

#pragma once

#include <stdbool.h>

// Mounts the game and returns the path prefix its files live under, e.g.
// "mk7game:/". NULL if the game could not be found or opened.
//
// Safe to call more than once; the second call returns the same prefix without
// remounting.
const char *vp_gamefs_open(void);

// Unmounts. Harmless if the game was never opened.
void vp_gamefs_close(void);

// Why the last vp_gamefs_open() returned NULL, as a sentence for the screen.
// Never NULL.
const char *vp_gamefs_error(void);
