#include "standin_chappy_motion.h"

#include <iterator>

namespace nectar {
namespace {

struct ChappyMotion {
	int type;
	AnimPayload payload;
};

#include "standin_chappy_motion_data.inc"

} // namespace

bool makeChappyCollisionMotion(int info, AnimPayload& out, int& bundleType)
{
	if (info < 0 || info >= int(std::size(kChappyInfoMotion))) return false;
	const ChappyMotion& motion = kChappyMotions[kChappyInfoMotion[info]];
	out        = motion.payload;
	bundleType = motion.type;
	return true;
}

} // namespace nectar
