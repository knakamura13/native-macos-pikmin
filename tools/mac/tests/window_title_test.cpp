#include "../../../games/pikmin1/pc_port/pc_build_window_title.h"
#include <cassert>
#include <cstring>

int main()
{
    assert(std::strcmp(pc_build_window_title("default", nullptr), "default") == 0);
    assert(std::strcmp(pc_build_window_title("default", ""), "default") == 0);
    assert(std::strcmp(pc_build_window_title("default", "Pikmin 1 — Develop"), "Pikmin 1 — Develop") == 0);
}
