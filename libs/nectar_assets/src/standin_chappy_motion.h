#pragma once

#include "nectar_assets/anim.h"

namespace nectar {

// Measured collision-driving Chappy joint channels, indexed by the existing
// AnimMgr info table. bundleType is BUNDLE_DCA or BUNDLE_DCK. Returns false for
// an info outside the measured table; out and bundleType remain unchanged.
// No file access: the generated numeric tables carry source hashes and decode
// provenance. Non-collision joints retain the measured MOD rest SRT.
bool makeChappyCollisionMotion(int info, AnimPayload& out, int& bundleType);

} // namespace nectar
