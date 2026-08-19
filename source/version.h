// The version this build calls itself.
//
// It is compared numerically against the newest release tag on GitHub, with a
// leading "v" tolerated on either side, so a tag of "v1.0.0" and a value here
// of "1.0.0" are the same version. Bump this in the same commit that cuts the
// release, or the shipped build will offer itself an update forever.
//
// Left blank between releases on purpose. While it is blank the updater refuses
// to compare - it reports that the build has no version rather than treating an
// unset value as 0.0.0, which would make every release on GitHub look newer and
// offer a pointless update on every check.

#pragma once

#define VP_VERSION "1.1.0"

// Whether the line above has been filled in. Everything that prints or compares
// the version goes through this so there is one answer to "do we know".
#define VP_VERSION_SET (VP_VERSION[0] != '\0')
