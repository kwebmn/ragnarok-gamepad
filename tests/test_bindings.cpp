#include "core/config/bindings.hpp"
#include "test.hpp"

using namespace uapad;

namespace {

size_t indexOf(uint16_t button) {
    for (size_t i = 0; i < kBindableButtons.size(); ++i)
        if (kBindableButtons[i] == button) return i;
    return kBindableButtons.size();
}

Action slot(int s) { return Action{Action::Kind::Slot, uint8_t(s)}; }
Action kind(Action::Kind k) { return Action{k, 0}; }

}  // namespace

TEST(bindings_default_is_the_hunter_layout) {
    const Bindings b = defaultBindings();
    CHECK(b[indexOf(kPadA)] == kind(Action::Kind::Attack));
    CHECK(b[indexOf(kPadX)] == slot(0));
    CHECK(b[indexOf(kPadY)] == slot(1));
    CHECK(b[indexOf(kPadB)] == slot(2));
    CHECK(b[indexOf(kPadRB)] == slot(3));
    CHECK(b[indexOf(kPadRT)] == slot(4));
    CHECK(b[indexOf(kPadLB)] == slot(5));
    CHECK(b[indexOf(kPadUp)] == slot(6));
    CHECK(b[indexOf(kPadRight)] == slot(7));
    CHECK(b[indexOf(kPadDown)] == slot(8));
    CHECK(b[indexOf(kPadLT)] == kind(Action::Kind::Pickup));
    CHECK(b[indexOf(kPadLeft)] == kind(Action::Kind::SitStand));
    CHECK(b[indexOf(kPadR3)] == kind(Action::Kind::ClearTarget));
    CHECK(b[indexOf(kPadL3)] == kind(Action::Kind::None));
}

TEST(bindings_action_ring_wraps) {
    CHECK(nextAction(kind(Action::Kind::None), -1) == kind(Action::Kind::ClearTarget));
    CHECK(nextAction(kind(Action::Kind::Attack), +1) == slot(0));
    CHECK(nextAction(slot(0), -1) == kind(Action::Kind::Attack));
    CHECK(nextAction(slot(26), +1) == kind(Action::Kind::Pickup));
    CHECK(nextAction(kind(Action::Kind::ClearTarget), +1) == kind(Action::Kind::None));
    Action a = kind(Action::Kind::None);
    for (int i = 0; i < 32; ++i) a = nextAction(a, +1);
    CHECK(a == kind(Action::Kind::None));  // 32 actions in the ring
}

TEST(bindings_tokens_round_trip) {
    Action a = kind(Action::Kind::None);
    for (int i = 0; i < 32; ++i) {
        const auto back = parseAction(actionToken(a));
        CHECK(back && *back == a);
        a = nextAction(a, +1);
    }
    CHECK(actionToken(slot(11)) == "slot 12");
    CHECK(!parseAction("slot 0"));
    CHECK(!parseAction("slot 28"));
    CHECK(!parseAction("jump"));
    CHECK(parseAction("  Slot 3 ") && *parseAction("  Slot 3 ") == slot(2));
}

TEST(bindings_profiles_round_trip) {
    Profiles p;
    p.fallback[indexOf(kPadX)] = kind(Action::Kind::Attack);
    Profiles::Character hunter;
    hunter.name = "Hunter";
    hunter.bindings = defaultBindings();
    hunter.bindings[indexOf(kPadL3)] = slot(20);
    p.characters[150001] = hunter;
    const Profiles back = parseProfiles(formatProfiles(p));
    CHECK(back.fallback == p.fallback);
    CHECK_EQ(back.characters.size(), 1u);
    CHECK(back.characters.count(150001) && back.characters.at(150001).name == "Hunter");
    CHECK(back.characters.count(150001) && back.characters.at(150001).bindings == hunter.bindings);
}

TEST(bindings_missing_keys_fall_back) {
    const Profiles p = parseProfiles("[char 7]\nname = Mage\nB = attack\n[default]\nA = slot 3\n");
    CHECK(p.fallback[indexOf(kPadA)] == slot(2));
    CHECK(p.fallback[indexOf(kPadX)] == slot(0));  // not in [default]: built in
    const Bindings mage = p.forCharacter(7);
    CHECK(mage[indexOf(kPadB)] == kind(Action::Kind::Attack));
    CHECK(mage[indexOf(kPadA)] == slot(2));  // from [default], even though it comes later in the file
    CHECK(mage[indexOf(kPadY)] == slot(1));
    CHECK(p.forCharacter(8) == p.fallback);  // a character with no section of its own
}

TEST(bindings_bad_lines_are_skipped) {
    const Profiles p = parseProfiles(
        "\xEF\xBB\xBF; saved with a BOM\n"
        "[default]\nA = jump\nZZ = attack\nnot a line\nX = slot 99\n"
        "[char abc]\nA = attack\n"
        "[weird]\nA = attack\n"
        "[char 5]\nY = pickup ; comment\n");
    CHECK(p.fallback == defaultBindings());
    CHECK_EQ(p.characters.size(), 1u);
    CHECK(p.forCharacter(5)[indexOf(kPadY)] == kind(Action::Kind::Pickup));
}

TEST(bindings_labels) {
    CHECK(std::string(actionLabel(slot(0))) == "Слот F1");
    CHECK(std::string(actionLabel(slot(11))) == "Слот F3, ряд 2");
    CHECK(std::string(actionLabel(kind(Action::Kind::None))) == "—");
    CHECK(std::string(buttonLabel(indexOf(kPadUp))) == "D-pad ↑");
    CHECK(std::string(buttonToken(indexOf(kPadRight))) == "RIGHT");
}

TEST(bindings_saving_one_layout_keeps_the_others) {
    Profiles disk;  // what another client already wrote
    disk.fallback[indexOf(kPadL3)] = slot(9);
    disk.characters[1].name = "Priest";
    disk.characters[1].bindings = defaultBindings();
    disk.characters[1].bindings[indexOf(kPadA)] = slot(1);
    Bindings hunter = defaultBindings();
    hunter[indexOf(kPadX)] = kind(Action::Kind::Attack);
    const Profiles merged = withLayout(disk, 2, "Hunter", hunter);
    CHECK(merged.characters.count(1) && merged.characters.at(1).bindings == disk.characters.at(1).bindings);
    CHECK(merged.characters.count(2) && merged.characters.at(2).bindings == hunter);
    CHECK(merged.characters.count(2) && merged.characters.at(2).name == "Hunter");
    CHECK(merged.fallback == disk.fallback);
    const Profiles def = withLayout(disk, 0, "", hunter);  // before login: the default layout
    CHECK(def.fallback == hunter);
    CHECK_EQ(def.characters.size(), 1u);
}
