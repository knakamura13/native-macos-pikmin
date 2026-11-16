// SCRATCH ONLY (fork #207 wall-bomb sweep). Never commit.
#pragma once

struct PcWbsTrace {
	const void* target = nullptr; // wall being probed
	// meltingPot loop (the path walls actually live in)
	int meltVisited      = 0;
	int meltChildCount   = 0;
	int meltBestChild    = -1;
	float meltMinDist    = 1e9f; // the value compared against range
	float meltBestLen3   = 0.0f; // |childCentre - bombPos| (3D)
	float meltBestLenXZ  = 0.0f;
	float meltBestDy     = 0.0f;
	float meltBestChildR = 0.0f;
	float meltBestCx = 0.0f, meltBestCy = 0.0f, meltBestCz = 0.0f;
	float meltBombCentreSize = 0.0f;
	float meltDamageParam    = 0.0f;
	int meltAlive = 0, meltVisible = 0, meltCheck = 0;
	float range = 0.0f;
	// item loop (the path #207 suspected)
	int itemVisited       = 0;
	float itemCentreDist  = 0.0f;
	float itemBombR       = 0.0f;
	float itemWallR       = 0.0f;
	float itemCircleDist  = 0.0f;
	float itemDist        = 0.0f;
	int itemCheck         = 0;
	// InteractBomb::actItem on the target
	int actCalls     = 0;
	int shakeMsgs    = 0;
	int stageMsgs    = 0;
	float damage     = 0.0f;
};

inline PcWbsTrace gPcWbs;
inline bool gPcWbsActive = false;
