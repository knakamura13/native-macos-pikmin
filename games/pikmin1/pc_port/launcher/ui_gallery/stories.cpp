// The gallery's stories (#215): each one is a named fixture for the real
// launcher code. Add a story here when a screen, state or component appears;
// `ui_gallery --capture` then draws it at every viewport and the lint checks it.
#include "gallery_host.h"

#include "pikmin1_settings_fixture.h" // fixtures/pikmin1-settings.json (nectar --settings-dump)

#include <string>

namespace pikmin::launcher::gallery {
namespace {

const char* const kInstall = "/home/player/Games/Open Nectar";

std::string replaceAll(std::string text, const std::string& from, const std::string& to)
{
    for (std::size_t at = text.find(from); at != std::string::npos; at = text.find(from, at + to.size()))
        text.replace(at, from.size(), to);
    return text;
}

std::string pikmin1Settings()
{
    return std::string(reinterpret_cast<const char*>(kPikmin1Settings), kPikmin1SettingsSize);
}

// Pikmin 1 installed with its settings; Pikmin 2 not installed.
Fixture home()
{
    Fixture f;
    f.state.games[0].installed = true;
    f.state.games[0].directory = kInstall;
    f.state.games[0].executable = std::string(kInstall) + "/nectar";
    f.state.mapEditor = std::string(kInstall) + "/nectar-mapmaker";
    f.state.modelMaker = std::string(kInstall) + "/nectar-modelmaker";
    f.settingsDump[0] = pikmin1Settings();
    return f;
}

Fixture bothInstalled()
{
    Fixture f = home();
    f.state.games[1].installed = true;
    f.state.games[1].directory = std::string(kInstall) + "/pikmin2";
    f.state.games[1].executable = std::string(kInstall) + "/pikmin2/pikmin2_pc";
    return f;
}

Fixture update(bool notes)
{
    Fixture f = home();
    f.releaseNewer = true;
    f.releaseVersion = "1.4";
    if (notes) {
        f.releaseNotes = "# Open Nectar 1.4\n\n## Launcher\n- `--launch=pikmin1` and `--launch=pikmin2` start a game "
                         "without the home screen.\n- The version label sits on the logo baseline.\n\n## Pikmin 2\n"
                         "- Europe (PAL) discs install.\n- **Better Pathfinding** and **Thrown Pikmin Return** have "
                         "menu rows.\n  - Both are off by default.\n\nSee the [release page](https://example.com) for "
                         "everything else.";
    }
    return f;
}

Fixture settingsPage(int group)
{
    Fixture f = home();
    f.tab = 1;
    f.settingsGroup = group;
    return f;
}

Fixture withToast(Fixture f, std::string text, float seconds)
{
    f.toast = std::move(text);
    f.toastTime = seconds;
    return f;
}

Fixture paths(std::string rom, std::string dir)
{
    Fixture f;
    f.state.installerOnly = true;
    f.modal = Fixture::Modal::Paths;
    f.rom = std::move(rom);
    f.installDirectory = std::move(dir);
    return f;
}

Fixture message(Fixture f, std::string title, std::string text, std::string primary, std::string secondary = {},
                bool error = false, std::string cancel = {})
{
    f.modal = Fixture::Modal::Message;
    f.messageTitle = std::move(title);
    f.messageText = std::move(text);
    f.primary = std::move(primary);
    f.secondary = std::move(secondary);
    f.messageIsError = error;
    f.cancel = std::move(cancel);
    return f;
}

Fixture component(std::function<void(ComponentCanvas&)> draw)
{
    Fixture f;
    f.screen = Fixture::Screen::Component;
    f.component = std::move(draw);
    return f;
}

std::vector<Story> makeStories()
{
    std::vector<Story> s;
    const auto add = [&](std::string name, std::string group, std::string summary, std::function<Fixture()> make) {
        s.push_back({ std::move(name), std::move(group), std::move(summary), std::move(make), 0, 0, {}, {} });
    };
    const auto addComponent = [&](std::string name, std::string summary, int w, int h,
                                  std::function<void(ComponentCanvas&)> draw) {
        s.push_back({ std::move(name), "Components", std::move(summary), [draw] { return component(draw); }, w, h, {}, {} });
    };
    // A story with controls: `make` builds it from the args the window edits;
    // captures use `defaults`.
    const auto addArgs = [&](std::string name, std::string group, std::string summary, Args defaults,
                             std::function<Fixture(const Args&)> make, int w = 0, int h = 0) {
        Story story { std::move(name), std::move(group), std::move(summary), [make, defaults] { return make(defaults); },
                      w, h, defaults, make };
        s.push_back(std::move(story));
    };

    // ── Home screen ──
    addArgs("hub/home", "Screens", "Pikmin installed, Pikmin 2 not yet: tabs, both covers",
            { boolArg("Pikmin installed", true), boolArg("Pikmin 2 installed", false), boolArg("Update available", false),
              textArg("Update version", "1.4"), choiceArg("Tab", { "Games", "Settings", "Move Install" }, 0),
              textArg("Toast", "") },
            [](const Args& a) {
                Fixture f = argFlag(a, "Pikmin 2 installed") ? bothInstalled() : home();
                if (!argFlag(a, "Pikmin installed")) {
                    f.state.games[0] = GameInstall();
                    f.state.mapEditor.clear();
                    f.state.modelMaker.clear();
                    f.settingsDump[0].clear();
                }
                if (argFlag(a, "Update available")) {
                    f.releaseNewer = true;
                    f.releaseVersion = argText(a, "Update version");
                }
                f.tab = argChoice(a, "Tab");
                if (!argText(a, "Toast").empty()) f = withToast(f, argText(a, "Toast"), 8.0f);
                return f;
            });
    add("hub/both-installed", "Screens", "Both games installed", bothInstalled);
    add("hub/first-run", "Screens", "Launcher from the download, nothing installed: Install only", [] {
        Fixture f;
        f.state.installerOnly = true;
        return f;
    });
    addArgs("hub/update-notice", "Screens", "Newer release found: notice pill and What's new",
            { textArg("Version", "1.4"), boolArg("Release notes", true) }, [](const Args& a) {
                Fixture f = update(argFlag(a, "Release notes"));
                f.releaseVersion = argText(a, "Version");
                return f;
            });
    add("hub/update-notice-no-notes", "Screens", "Newer release without notes: the pill alone", [] { return update(false); });
    add("hub/whats-new", "Screens", "What's new panel open over the home screen", [] {
        Fixture f = update(true);
        f.whatsNewOpen = true;
        return f;
    });
    addArgs("hub/launch-fallback-notice", "Screens", "--launch=pikmin2 without Pikmin 2: home screen says why (#336)",
            { textArg("Notice", "Pikmin 2 is not installed yet. Install it here to play."),
              numberArg("Seconds left", 8.0f, 0.5f, 10.0f, 0.5f) },
            [](const Args& a) { return withToast(home(), argText(a, "Notice"), argNumber(a, "Seconds left")); });
    add("hub/launch-fallback-missing-exe", "Screens", "--launch=pikmin1 with the game file gone (#336)", [] {
        return withToast(home(), "Pikmin could not start: its game file is missing. Update to restore it.", 8.0f);
    });
    addArgs("hub/toast-long", "Screens", "Long toast (texture pack installed), two lines",
            { textArg("Toast", "Installed HD 4K (1.2 GB) to /home/player/Games/Open Nectar/Load/Textures/hd-4k. It "
                               "applies the next time the game starts.") },
            [](const Args& a) { return withToast(settingsPage(6), argText(a, "Toast"), 8.0f); });
    add("hub/update-and-fallback", "Screens", "Update notice and --launch notice at once", [] {
        return withToast(update(true), "Pikmin 2 is not installed yet. Install it here to play.", 8.0f);
    });
    add("hub/data-folder", "Screens", "Data folder footer with Change folder (Android-style state)", [] {
        Fixture f = bothInstalled();
        f.state.dataFolder = "/storage/emulated/0/Android/data/io.github.opennectar/files";
        return f;
    });
    add("move/default", "Screens", "Move Install tab, nothing chosen", [] {
        Fixture f = home();
        f.tab = 2;
        return f;
    });
    add("move/target-chosen", "Screens", "Move Install with a target and a note", [] {
        Fixture f = home();
        f.tab = 2;
        f.moveTarget = "/media/player/Games SSD/Open Nectar";
        f.moveNote = "The install moves to another disk: it is copied, then the old folder is removed.";
        return f;
    });

    // ── Settings ──
    const char* const groups[] = { "display", "graphics", "controls", "camera", "gameplay", "cheats" };
    for (int g = 0; g < 6; ++g)
        add(std::string("settings/") + groups[g], "Settings", std::string("Pikmin settings page: ") + groups[g],
            [g] { return settingsPage(g); });
    add("settings/texture-packs", "Settings", "Texture Packs page, nothing installed", [] { return settingsPage(6); });
    add("settings/hd-models", "Settings", "HD Models page, none installed", [] { return settingsPage(7); });
    add("settings/hd-models-installed", "Settings", "HD Models page, Olimar and Bulborb installed (one turned off)", [] {
        Fixture f = settingsPage(7);
        std::string& dump = f.settingsDump[0];
        dump = replaceAll(dump, "\"name\":\"Olimar HD\",\"installed\":false,\"enabled\":true",
                          "\"name\":\"Olimar HD\",\"installed\":true,\"enabled\":true");
        dump = replaceAll(dump, "\"name\":\"Bulborb HD\",\"installed\":false,\"enabled\":true",
                          "\"name\":\"Bulborb HD\",\"installed\":true,\"enabled\":false");
        dump = replaceAll(dump, "\"bytes\":0,\"size\":\"0 B\"", "\"bytes\":48234496,\"size\":\"46.0 MB\"");
        return f;
    });
    add("settings/saves", "Settings", "Saves page, no memory card yet", [] { return settingsPage(8); });
    add("settings/launcher", "Settings", "Settings > Launcher: where the launcher keeps its settings", [] {
        Fixture f = home();
        f.tab = 1;
        f.settingsGame = kHubGameCount;
        f.launcherPage = true;
        f.launcherFolder.keepInGameFolder = false;
        f.launcherFolder.canChange = true;
        f.launcherFolder.folder = "/home/player/.config/open-nectar";
        f.launcherFolder.note = "Settings are in your user folder, so every install shares them.";
        return f;
    });
    add("settings/loading", "Settings", "Still asking the game for its settings", [] {
        Fixture f = home();
        f.tab = 1;
        f.settingsDump[0].clear();
        f.settingsLoading[0] = true;
        return f;
    });
    add("settings/error", "Settings", "The game did not answer: error and Retry", [] {
        Fixture f = home();
        f.tab = 1;
        f.settingsDump[0].clear();
        f.settingsError[0] = "Could not read the game's settings (the game did not answer with its settings).";
        return f;
    });
    add("settings/pikmin2-not-installed", "Settings", "Pikmin 2 settings before it is installed", [] {
        Fixture f = home();
        f.tab = 1;
        f.settingsGame = 1;
        return f;
    });

    // ── Install and dialogs ──
    add("install/paths-empty", "Dialogs", "Install modal, no disc image chosen yet (missing ISO)",
        [] { return paths("", ""); });
    add("install/paths-ready", "Dialogs", "Install modal with disc image and folder", [] {
        return paths("/home/player/Downloads/Pikmin (USA) (Rev 1).iso", "/home/player/Games/Open Nectar");
    });
    addArgs("install/paths-long", "Dialogs", "Install modal with long paths (shortened from the front)",
            { textArg("Disc image", "/home/player/Documents/Backups/GameCube discs/Nintendo/2001/Pikmin (USA) (Rev 1) "
                                    "[GPIE01].iso"),
              textArg("Install folder", "/media/player/External drive with a long name/Games/Open Nectar/Pikmin") },
            [](const Args& a) { return paths(argText(a, "Disc image"), argText(a, "Install folder")); });
    add("install/paths-save-migration", "Dialogs", "Install modal with the save-migration note", [] {
        Fixture f = paths("/home/player/Downloads/Pikmin (USA) (Rev 1).iso", "/home/player/Games/Open Nectar");
        f.legacySave = "/home/player/.local/share/pikmin-native/card";
        return f;
    });
    add("install/paths-waiting", "Dialogs", "Install modal while the disc image picker is open", [] {
        Fixture f = paths("", "");
        f.dialogWaiting = "disc image picker";
        return f;
    });
    addArgs("install/progress", "Dialogs", "Extracting the disc",
            { numberArg("Percent", 42.0f, 0.0f, 100.0f), textArg("Phase", "Extracting"),
              textArg("Current file", "assets/dataDir/archives/tekis.arc") },
            [](const Args& a) {
                Fixture f = paths("/home/player/Downloads/Pikmin (USA) (Rev 1).iso", kInstall);
                f.modal = Fixture::Modal::Progress;
                f.percent = unsigned(argNumber(a, "Percent") + 0.5f);
                f.phase = argText(a, "Phase");
                f.currentFile = argText(a, "Current file");
                return f;
            });
    add("install/error", "Dialogs", "Installation did not finish (one button)", [] {
        Fixture f = paths("", "");
        return message(f, "Installation did not finish",
                       "The disc image is not Pikmin (USA, Rev 1) or Pikmin (Europe): its checksum does not match. "
                       "Dump your own disc again or choose another image.",
                       "OK", "", true);
    });
    add("install/retry", "Dialogs", "Installation did not finish (Back to setup / Close)", [] {
        Fixture f = paths("", "");
        return message(f, "Installation did not finish", "Not enough free space in /home/player/Games: 1.3 GB needed.",
                       "Back to setup", "Close", true);
    });
    add("install/complete", "Dialogs", "Installation complete (Play now / Back)", [] {
        Fixture f = home();
        return message(f, "Installation complete",
                       std::string("Pikmin is installed in:\n") + kInstall
                           + "\n\nIn game, F1 or backtick opens graphics, controls and gameplay settings."
                             "\nOpen nectar-launcher from that folder to play again.",
                       "Play now", "Back");
    });
    add("dialog/update-available", "Dialogs", "Update available (Update now / Not now)", [] {
        return message(update(true), "Update available",
                       "Open Nectar 1.4 - Launcher polish\nYou have 1.3. The update downloads and installs by itself;"
                       " your saves, settings and packs stay as they are.",
                       "Update now", "Not now");
    });
    addArgs("dialog/replace-save", "Dialogs", "Replace your save? (Replace / Cancel)",
            { textArg("Title", "Replace your save?"),
              textArg("Text", "Importing replaces all save slots and options on this install's memory card with the "
                              "ones in the .gci file.\n\nYour current save is backed up first, and you can bring it "
                              "back from this page with \"Restore previous save\"."),
              textArg("Primary", "Replace"), textArg("Cancel", "Cancel"), boolArg("Error", false) },
            [](const Args& a) {
                return message(settingsPage(8), argText(a, "Title"), argText(a, "Text"), argText(a, "Primary"), "",
                               argFlag(a, "Error"), argText(a, "Cancel"));
            });

    // ── Components ──
    addComponent("components/button-sizes", "The four standard widths, normal and selected", 760, 220,
                 [](ComponentCanvas& c) {
                     const char* names[] = { "Small", "Medium", "Large", "Extra large" };
                     const float widths[] = { 120, 144, 190, 300 };
                     float x = 24;
                     for (int i = 0; i < 4; ++i) {
                         c.caption(x, 20, names[i]);
                         c.at(x, 48);
                         c.standardButton((std::string(names[i]) + "##n").c_str(), i, 44);
                         c.at(x, 112);
                         c.standardButton((std::string(names[i]) + "##s").c_str(), i, 44, true);
                         x += widths[i] + 12;
                     }
                 });
    addArgs("components/button", "Components", "One glass button: edit its label, size and state",
            { textArg("Label", "Install"), choiceArg("Width", { "Small", "Medium", "Large", "Extra large" }, 1),
              numberArg("Height", 44.0f, 30.0f, 70.0f), boolArg("Selected", false), boolArg("Disabled", false) },
            [](const Args& a) {
                const std::string label = argText(a, "Label") + "##button";
                const int width = argChoice(a, "Width");
                const float height = argNumber(a, "Height");
                const bool selected = argFlag(a, "Selected"), disabled = argFlag(a, "Disabled");
                return component([=](ComponentCanvas& c) {
                    c.at(40, 40);
                    c.standardButton(label.c_str(), width, height, selected, disabled);
                });
            },
            400, 140);
    addArgs("components/button-states", "Components", "Normal, selected, disabled, pressed-looking heights",
            { textArg("Normal label", "Play"), textArg("Selected label", "Install"), textArg("Disabled label", "Install"),
              numberArg("Height", 50.0f, 30.0f, 70.0f) },
            [](const Args& a) {
                const std::string normal = argText(a, "Normal label") + "##a", selected = argText(a, "Selected label") + "##b",
                                  disabled = argText(a, "Disabled label") + "##c";
                const float height = argNumber(a, "Height");
                return component([=](ComponentCanvas& c) {
                     c.caption(24, 20, "Normal");
                     c.at(24, 48);
                     c.standardButton(normal.c_str(), 1, height);
                     c.caption(184, 20, "Selected");
                     c.at(184, 48);
                     c.standardButton(selected.c_str(), 1, height, true);
                     c.caption(344, 20, "Disabled");
                     c.at(344, 48);
                     c.standardButton(disabled.c_str(), 1, height, true, true);
                     c.caption(24, 120, "Heights 38 / 44 / 50");
                     c.at(24, 148);
                     c.standardButton("Pikmin##h1", 1, 38);
                     c.at(184, 148);
                     c.standardButton("Browse##h2", 0, 44);
                     c.at(320, 148);
                     c.standardButton("Cancel##h3", 1, 50);
                });
            },
            640, 220);
    addComponent("components/button-fit", "Longest real labels in their real sizes (shrink, never cut)", 720, 260,
                 [](ComponentCanvas& c) {
                     struct Row { const char* label; int size; float h; };
                     const Row rows[] = { { "Move Install", 1, 42 }, { "Install a pack from a .zip", 3, 44 },
                                          { "Get HD models", 3, 44 }, { "Restore previous save", 3, 44 },
                                          { "Change folder", 1, 40 }, { "Back to setup", 1, 50 } };
                     float x = 24, y = 24;
                     for (const Row& r : rows) {
                         c.at(x, y);
                         c.standardButton(r.label, r.size, r.h);
                         y += r.h + 14;
                         if (y > 200) { y = 24; x += 360; }
                     }
                 });
    addComponent("components/tabs", "Games / Settings / Move Install", 640, 120, [](ComponentCanvas& c) { c.tabs(36); });
    addArgs("components/toast", "Components", "Toast alone",
            { textArg("Text", "Pikmin 2 is not installed yet. Install it here to play.") },
            [](const Args& a) {
                Fixture f = component([](ComponentCanvas&) {});
                f.toast = argText(a, "Text");
                f.toastTime = 8.0f;
                return f;
            },
            720, 200);
    addArgs("components/release-notice", "Components", "Release notice pill with What's new",
            { textArg("Version", "1.4"), boolArg("Release notes", true) },
            [](const Args& a) {
                Fixture f = component([](ComponentCanvas&) {});
                f.releaseNewer = true;
                f.releaseVersion = argText(a, "Version");
                if (argFlag(a, "Release notes")) f.releaseNotes = "- Fixes";
                return f;
            },
            560, 120);
    return s;
}

} // namespace

const std::vector<Story>& stories()
{
    static const std::vector<Story> all = makeStories();
    return all;
}

Arg boolArg(std::string name, bool value)
{
    Arg a;
    a.name = std::move(name);
    a.kind = Arg::Kind::Bool;
    a.flag = value;
    return a;
}

Arg textArg(std::string name, std::string value)
{
    Arg a;
    a.name = std::move(name);
    a.kind = Arg::Kind::Text;
    a.text = std::move(value);
    return a;
}

Arg numberArg(std::string name, float value, float min, float max, float step)
{
    Arg a;
    a.name = std::move(name);
    a.kind = Arg::Kind::Number;
    a.number = value;
    a.min = min;
    a.max = max;
    a.step = step;
    return a;
}

Arg choiceArg(std::string name, std::vector<std::string> choices, int value)
{
    Arg a;
    a.name = std::move(name);
    a.kind = Arg::Kind::Choice;
    a.choices = std::move(choices);
    a.choice = value;
    return a;
}

namespace {
const Arg* findArg(const Args& args, const std::string& name)
{
    for (const Arg& a : args)
        if (a.name == name) return &a;
    return nullptr;
}
} // namespace

bool argFlag(const Args& args, const std::string& name)
{
    const Arg* a = findArg(args, name);
    return a && a->flag;
}

const std::string& argText(const Args& args, const std::string& name)
{
    static const std::string empty;
    const Arg* a = findArg(args, name);
    return a ? a->text : empty;
}

float argNumber(const Args& args, const std::string& name)
{
    const Arg* a = findArg(args, name);
    return a ? a->number : 0.0f;
}

int argChoice(const Args& args, const std::string& name)
{
    const Arg* a = findArg(args, name);
    return a ? a->choice : 0;
}

bool sameArgs(const Args& a, const Args& b)
{
    if (a.size() != b.size()) return false;
    for (std::size_t i = 0; i < a.size(); ++i)
        if (a[i].name != b[i].name || a[i].flag != b[i].flag || a[i].text != b[i].text || a[i].number != b[i].number
            || a[i].choice != b[i].choice)
            return false;
    return true;
}

Fixture makeFixture(const Story& story, const Args* args)
{
    if (args && story.makeWith) return story.makeWith(*args);
    return story.make ? story.make() : Fixture();
}

const Story* findStory(const std::string& name)
{
    for (const Story& s : stories())
        if (s.name == name) return &s;
    return nullptr;
}

} // namespace pikmin::launcher::gallery
