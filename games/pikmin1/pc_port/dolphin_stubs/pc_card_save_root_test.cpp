#include "dolphin_stubs/pc_card_save_root.h"

#include <cstdlib>
#include <filesystem>
#include <fstream>

namespace fs = std::filesystem;

void setEnv(const char* key, const char* value)
{
#ifdef _WIN32
    _putenv_s(key, value ? value : "");
#else
    if (value)
        setenv(key, value, 1);
    else
        unsetenv(key);
#endif
}

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

    setEnv("NECTAR_SAVE_DIR", nullptr);
    setEnv("NECTAR_SAVE_READ_ONLY", nullptr);
    setEnv("NECTAR_SAVE_DIR", save.string().c_str());
    check(pc_card_resolve_save_root({}) == save, "NECTAR_SAVE_DIR wins");

    setEnv("NECTAR_SAVE_READ_ONLY", "1");
    check(pc_card_save_writes_blocked(), "read-only env blocks writes");
    setEnv("NECTAR_SAVE_READ_ONLY", "0");
    check(!pc_card_save_writes_blocked(), "0 clears read-only");
    setEnv("NECTAR_SAVE_READ_ONLY", nullptr);
    check(!pc_card_save_writes_blocked(), "unset clears read-only");

    fs::remove_all(root, ec);
    if (failures == 0) {
        std::printf("pc_card_save_root_test: PASS\n");
        return 0;
    }
    std::printf("pc_card_save_root_test: %d FAILURES\n", failures);
    return 1;
}
