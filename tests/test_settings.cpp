#include "core/config/config.hpp"
#include "core/config/ini_edit.hpp"
#include "core/config/settings.hpp"
#include "test.hpp"

using namespace uapad;

TEST(settings_from_config) {
    const Settings d = settingsFrom(Config{});
    CHECK(d == defaultSettings());
    CHECK(d.lang == Lang::En && !d.lock && !d.focusClick && d.mouseKeys == (std::array<int, 3>{0, 0, 0}));
    CHECK(!d.borderless && d.saveCpu && d.rumble && d.mark == MarkStyle::Gasp);
    Config c;
    c.lang = Lang::Ru;
    c.mouseKeys = {2, 0, 9};
    c.backgroundSleepMs = 0;
    c.rumble = false;
    const Settings s = settingsFrom(c);
    CHECK(s.lang == Lang::Ru && s.mouseKeys == (std::array<int, 3>{2, 0, 9}) && !s.saveCpu && !s.rumble);
}

TEST(settings_values_go_round) {
    Settings s = defaultSettings();
    s = changeSetting(s, SettingRow::Language, +1);
    CHECK(s.lang == Lang::Uk);
    s = changeSetting(s, SettingRow::Language, +1);
    CHECK(s.lang == Lang::Ru);
    s = changeSetting(s, SettingRow::Language, +1);
    CHECK(s.lang == Lang::En);
    CHECK(changeSetting(s, SettingRow::Language, -1).lang == Lang::Ru);
    s = changeSetting(s, SettingRow::Button4, -1);  // none -> F9
    CHECK(s.mouseKeys[1] == 9);
    s = changeSetting(s, SettingRow::Button4, +1);  // F9 -> none
    CHECK(s.mouseKeys[1] == 0);
    CHECK(changeSetting(s, SettingRow::Middle, +1).mouseKeys[0] == 1);
    CHECK(changeSetting(s, SettingRow::Lock, -1).lock);
    CHECK(!changeSetting(s, SettingRow::Rumble, +1).rumble);
    CHECK(changeSetting(s, SettingRow::FocusClick, +1).focusClick);
}

TEST(settings_reset_a_row) {
    Settings s = defaultSettings();
    s.lang = Lang::Ru;
    s.borderless = true;
    s = resetSetting(s, SettingRow::Language);
    CHECK(s.lang == Lang::En);
    CHECK(s.borderless);
}

TEST(settings_labels_and_values) {
    Settings s = defaultSettings();
    s.lang = Lang::Uk;
    s.mouseKeys = {3, 0, 0};
    CHECK(settingLabel(SettingRow::Language, Lang::En) == "Language");
    CHECK(settingLabel(SettingRow::Borderless, Lang::Ru) == "Окно без рамки");
    CHECK(settingValue(s, SettingRow::Language, Lang::En) == "Українська");  // always in its own language
    CHECK(settingValue(s, SettingRow::Lock, Lang::En) == "off");
    CHECK(settingValue(s, SettingRow::Rumble, Lang::Ru) == "вкл.");
    CHECK(settingValue(s, SettingRow::FocusClick, Lang::En) == "stays out of the game");
    CHECK(settingValue(s, SettingRow::Middle, Lang::En) == "F3");
    CHECK(settingValue(s, SettingRow::Button5, Lang::En) == "—");
}

TEST(settings_write_only_what_changed) {
    const Settings before = defaultSettings();
    Settings after = before;
    after.lang = Lang::Uk;
    after.mouseKeys[2] = 5;
    const std::vector<IniEdit> edits = settingsEdits(before, after, 10);
    CHECK_EQ(edits.size(), 2u);
    CHECK(edits.size() == 2 && edits[0].section == "general" && edits[0].key == "language" && edits[0].value == "uk");
    CHECK(edits.size() == 2 && edits[1].section == "mouse" && edits[1].key == "button5" && edits[1].value == "F5");
    CHECK(settingsEdits(before, before, 10).empty());
}

TEST(settings_cpu_saving_keeps_the_files_own_number) {
    Settings off = defaultSettings();
    off.saveCpu = false;
    const Settings on = defaultSettings();
    auto edits = settingsEdits(off, on, 25);
    CHECK(edits.size() == 1 && edits[0].key == "background_sleep_ms" && edits[0].value == "25");
    edits = settingsEdits(off, on, 0);
    CHECK(edits.size() == 1 && edits[0].value == "10");
    edits = settingsEdits(on, off, 25);
    CHECK(edits.size() == 1 && edits[0].value == "0");
}

TEST(settings_round_trip_through_the_file) {
    Settings s = defaultSettings();
    s.lang = Lang::Ru;
    s.lock = true;
    s.focusClick = true;
    s.mouseKeys = {1, 2, 3};
    s.borderless = true;
    s.saveCpu = false;
    s.rumble = false;
    s.mark = MarkStyle::Poison;
    const Config c = parseConfig(applyIniEdits("", settingsEdits(defaultSettings(), s, 10)));
    CHECK(settingsFrom(c) == s);
    Config applied;
    applySettings(s, 10, applied);
    CHECK(settingsFrom(applied) == s);
    CHECK_EQ(applied.backgroundSleepMs, 0);
    applySettings(defaultSettings(), 25, applied);
    CHECK_EQ(applied.backgroundSleepMs, 25);
}

TEST(settings_target_mark_goes_round) {
    Settings s = defaultSettings();  // "!" when chosen
    CHECK(settingLabel(SettingRow::Mark, Lang::Ru) == "Метка цели");
    CHECK(settingValue(s, SettingRow::Mark, Lang::En) == "\"!\" when chosen");
    s = changeSetting(s, SettingRow::Mark, +1);
    CHECK(s.mark == MarkStyle::Sight);
    CHECK(settingValue(s, SettingRow::Mark, Lang::Uk) == "вогник (Sight)");
    s = changeSetting(s, SettingRow::Mark, +1);
    CHECK(s.mark == MarkStyle::Ruwach);
    s = changeSetting(s, SettingRow::Mark, +1);
    CHECK(s.mark == MarkStyle::Poison);
    CHECK(settingValue(s, SettingRow::Mark, Lang::Ru) == "зелёный оттенок");
    s = changeSetting(s, SettingRow::Mark, +1);
    CHECK(s.mark == MarkStyle::Off);
    CHECK(settingValue(s, SettingRow::Mark, Lang::En) == "off");
    CHECK(changeSetting(s, SettingRow::Mark, -1).mark == MarkStyle::Poison);
    const auto edits = settingsEdits(defaultSettings(), changeSetting(defaultSettings(), SettingRow::Mark, +1), 10);
    CHECK(edits.size() == 1 && edits[0].section == "aim" && edits[0].key == "mark" && edits[0].value == "sight");
}
