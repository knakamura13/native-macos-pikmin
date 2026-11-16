#pragma once

#include "GlobalGameOptions.h"

// Disc formations stop at 300 points (FormationMgr::mMax). The port's Pikmin
// Limit goes up to PIKI_FIELD_LIMIT_MAX, and a squad that large is one
// formation. The table has to be that big or getFormPoint panics and slide
// stores at mFormPoints[-1].
inline constexpr int pc_formation_slot_count = PIKI_FIELD_LIMIT_MAX;

// True while another arranged point still fits in the table.
inline bool pc_formation_can_add(int pointCount, int capacity)
{
	return pointCount >= 0 && pointCount < capacity;
}

// Slot to give a joining Pikmin, or -1 when the index or the member list is
// out of the table. Callers must not store through a -1 result.
inline int pc_formation_claim(int freeIdx, int memberCount, int capacity)
{
	if (freeIdx < 0 || freeIdx >= capacity || memberCount < 0 || memberCount >= capacity) {
		return -1;
	}
	return freeIdx;
}
