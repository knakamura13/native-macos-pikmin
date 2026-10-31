#include "dolphin_stubs/pc_card_save_root.h"

#include <cstdlib>
#include <filesystem>
#include <fstream>

namespace fs = std::filesystem;

int main()
{
    int failures = 0;
    auto check = [&](bool ok, const char* message) {
        if (!ok) {
            ++failures;
            std::printf("FAIL: %s\n", message);
        }
    };

    const fs::path root = fs::temp_directory_path() / "pc_card_save_root_test";
    std::error_code ec;
    fs::remove_all(root, ec);
    const fs::path save = root / "save";
    fs::create_directories(save / "card0");
    std::ofstream(save / "card0" / "Pikmin dataFile") << "save";

    unsetenv("NECTAR_SAVE_DIR");
    unsetenv("NECTAR_SAVE_READ_ONLY");
    setenv("NECTAR_SAVE_DIR", save.string().c_str(), 1);
    check(pc_card_resolve_save_root({}) == save, "NECTAR_SAVE_DIR wins");

    setenv("NECTAR_SAVE_READ_ONLY", "1", 1);
    check(pc_card_save_writes_blocked(), "read-only env blocks writes");
    setenv("NECTAR_SAVE_READ_ONLY", "0", 1);
    check(!pc_card_save_writes_blocked(), "0 clears read-only");
    unsetenv("NECTAR_SAVE_READ_ONLY");
    check(!pc_card_save_writes_blocked(), "unset clears read-only");

    fs::remove_all(root, ec);
    if (failures == 0) {
        std::printf("pc_card_save_root_test: PASS\n");
        return 0;
    }
    std::printf("pc_card_save_root_test: %d FAILURES\n", failures);
    return 1;
}
