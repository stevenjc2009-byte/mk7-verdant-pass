// Where the updater has got to.
//
// Split out of updater.h so the front end's logic can be reasoned about - and
// tested on the host - without dragging in <3ds.h>. updater.h includes this;
// nothing else needs to know they were ever separate.

#pragma once

typedef enum
{
    UPDATE_IDLE,         ///< Nothing has been asked for yet.
    UPDATE_CHECKING,     ///< Talking to GitHub.
    UPDATE_UP_TO_DATE,   ///< Asked, and this build is already the newest.
    UPDATE_AVAILABLE,    ///< A newer release exists; waiting on the player.
    UPDATE_DOWNLOADING,  ///< Streaming the .cia into the install handle.
    UPDATE_INSTALLING,   ///< Download finished, AM is committing the title.
    UPDATE_DONE,         ///< Installed. The app should relaunch itself now.
    UPDATE_FAILED,       ///< Gave up. updaterMessage() says why.
} updateState;
