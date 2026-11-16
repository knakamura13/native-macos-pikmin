// The formation table used to be a fixed 300. Gameplay > Pikmin Limit goes to
// 999, and whistling that many Pikmin into one squad walks off the table.
// This locks the capacity and the guard the game calls before every store.
#include "pc_formation_slots.h"

#include <cstdio>

namespace {

int checks   = 0;
int failures = 0;

#define CHECK(condition)                                                                                               \
	do {                                                                                                               \
		++checks;                                                                                                      \
		if (!(condition)) {                                                                                            \
			std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #condition);                                           \
			++failures;                                                                                                \
		}                                                                                                              \
	} while (0)

} // namespace

int main()
{
	CHECK(pc_formation_slot_count == PIKI_FIELD_LIMIT_MAX);
	CHECK(pc_formation_slot_count == 999);
	// The disc table. A port squad is allowed to be larger than this.
	CHECK(pc_formation_slot_count > 300);

	CHECK(pc_formation_can_add(0, pc_formation_slot_count));
	CHECK(pc_formation_can_add(298, pc_formation_slot_count));
	CHECK(pc_formation_can_add(998, pc_formation_slot_count));
	CHECK(!pc_formation_can_add(999, pc_formation_slot_count));
	CHECK(!pc_formation_can_add(-1, pc_formation_slot_count));

	// Circle arrange adds four points per ring. Past the last full ring the
	// extra calls have to be refused so the 1000th point is not stored.
	int points = 0;
	for (int i = 0; i < pc_formation_slot_count + 4; i++) {
		if (pc_formation_can_add(points, pc_formation_slot_count)) {
			points++;
		}
	}
	CHECK(points == pc_formation_slot_count);

	// slide() used to write mFormPoints[-1] when every arranged point was taken.
	CHECK(pc_formation_claim(-1, 0, pc_formation_slot_count) == -1);
	CHECK(pc_formation_claim(0, pc_formation_slot_count, pc_formation_slot_count) == -1);
	CHECK(pc_formation_claim(999, 998, pc_formation_slot_count) == -1);
	CHECK(pc_formation_claim(10, 10, pc_formation_slot_count) == 10);
	CHECK(pc_formation_claim(998, 998, pc_formation_slot_count) == 998);
	// A 300-slot table refuses the 301st member the menu can put on the field.
	CHECK(pc_formation_claim(300, 300, 300) == -1);

	if (failures != 0) {
		std::printf("%d/%d checks failed\n", failures, checks);
		return 1;
	}
	std::printf("%d checks passed\n", checks);
	return 0;
}
