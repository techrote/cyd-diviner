// Actual CYD controller regression tests. Rites record gestures; DSP/radio are
// intentionally outside this fixture. Build links the production theme codec.
#include "../src/cyd_controls.h"
#include "../src/cyd_settings_file.h"
#include "../src/rite.h"
#include "../src/theme.h"
#include "../src/native_script.h"
#include <cassert>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>
#include <unistd.h>

namespace {
std::vector<InstrumentPointer> gestures;
bool pointer(const InstrumentPointer &p) { gestures.push_back(p); return true; }
uint32_t now = 10;
uint8_t level = 140;
}
#define RITE_STUB(id) const Rite RITE_##id = {#id, #id, "", nullptr, nullptr, nullptr, nullptr}
#define INSTRUMENT_STUB(id) const Rite RITE_##id = {#id, #id, "", nullptr, nullptr, nullptr, nullptr, pointer}
INSTRUMENT_STUB(DRONE); INSTRUMENT_STUB(CANON); INSTRUMENT_STUB(GLYPHSONG);
INSTRUMENT_STUB(SPECTRA); INSTRUMENT_STUB(COVEN);
RITE_STUB(SEQ); RITE_STUB(BEAT); RITE_STUB(WIRED); RITE_STUB(VISIONS);
RITE_STUB(ORACLE); RITE_STUB(LINK); RITE_STUB(SCRY); RITE_STUB(LISTEN);
RITE_STUB(KIN); RITE_STUB(NET); RITE_STUB(GEMINI); RITE_STUB(IR);
RITE_STUB(SENTINEL); RITE_STUB(PULSE); RITE_STUB(PROBE); RITE_STUB(PALETTE);
namespace hal {
uint8_t volume() { return level; }
void setVolume(uint8_t value) { level = value; }
}
bool visionsUsesRgb444() { return false; }
bool visionsAreLayered() { return true; }
bool visionsPaused() { return false; }
bool visionsSoundEnabled() { return false; }
int visionsChoice() { return 0; }

hal::Key press(int x, int y, bool *produced = nullptr) {
    hal::Key key{};
    const bool got = cyd_controls::touch(true, x, y, now++, key);
    if (produced) *produced = got;
    return key;
}
void release() {
    hal::Key key{};
    cyd_controls::touch(false, 0, 0, now++, key);
    now += 31;
    cyd_controls::touch(false, 0, 0, now++, key);
}
hal::Key tap(int x, int y) { auto key = press(x, y); release(); return key; }
hal::Key logicalTap(int x, int y) {
    return tap(cyd_controls::viewportLeft() + x * cyd_controls::viewportWidth() / 240,
               cyd_controls::viewportTop() + y * cyd_controls::viewportHeight() / 135);
}

// Reproduce the missed Hide edge without sleeping, then use the actual native
// adapter with the same declared 1700/1900/2100/2350/2600 ms action sequence.
// The simulated render stall skips directly to 2300 ms, and no input poll is
// allowed between the next script contacts.
void delayedScriptRegression() {
    using namespace cyd_controls;
    release();
    setRite(&RITE_GLYPHSONG);
    logicalTap(100, 40);
    assert(viewportBottom() == 110);
    std::vector<hal::Key> keys;
    auto emit = [&](bool down, int x, int y) {
        hal::Key key{};
        if (touch(down, x, y, now, key)) keys.push_back(key);
    };

    // Old overdue-burst behavior: Sing is admitted, Hide at the same actual
    // instant is lost because its down edge cancels the pending release.
    now += 2300;
    emit(true, 295, 228); emit(false, 295, 228);
    emit(true, 248, 228); emit(false, 248, 228);
    assert(keys.size() == 1 && keys[0].nav == hal::N_ENTER);
    assert(viewportBottom() == 110);
    release();
    tap(248, 228);
    assert(viewportBottom() == 180);
    logicalTap(100, 40);
    assert(viewportBottom() == 110);
    keys.clear();

    native_script::Schedule clock;
    native_script::TouchEdges edges;
    const uint32_t start = now;
    clock.reset(start);
    now = start + 2300; // both first actions would be overdue in the old loop
    assert(clock.dispatch(1700, now));
    edges.tap(295, 228, emit);
    assert(!clock.dispatch(1900, now));
    now = start + 2499;
    assert(!clock.dispatch(1900, now));
    now = start + 2500;
    assert(clock.dispatch(1900, now));
    edges.tap(248, 228, emit);
    assert(viewportBottom() == 180); // known up state was settled before Hide
    now = start + 2700;
    assert(clock.dispatch(2100, now)); edges.tap(280, 195, emit);
    now = start + 2950;
    assert(clock.dispatch(2350, now)); edges.tap(280, 195, emit);
    now = start + 3200;
    assert(clock.dispatch(2600, now)); edges.tap(280, 195, emit);
    assert(keys.size() == 4 && keys[0].nav == hal::N_ENTER);
    for (size_t i = 1; i < keys.size(); ++i) assert(keys[i].nav == hal::N_TAB);
    assert(!edges.held());
    release();
    setRite(nullptr);

    // Counter rollover must not make a future action appear overdue.
    clock.reset(0xfffffff0u);
    assert(!clock.dispatch(32, 15));
    assert(clock.dispatch(32, 16));
    assert(!clock.dispatch(64, 47));
    assert(clock.dispatch(64, 48));
    std::puts("PASS scripted touch: forced 600 ms stall, preserved action intervals, settled release, Hide + three Mode edges, timer rollover");
}

int main() {
    using namespace cyd_controls;
    unsetenv("GRIM_SETTINGS");
    setRite(&RITE_CANON);
    assert(displayWidth() == 320 && displayHeight() == 240 && viewportBottom() == 180);
    press(240, 72);
    assert(gestures.size() == 1 && gestures.back().start && gestures.back().down);
    assert(gestures.back().x == 180 && gestures.back().y == 54);
    press(300, 150);
    assert(gestures.size() == 2 && !gestures.back().start && gestures.back().x == 225);
    release();
    assert(gestures.size() == 3 && !gestures.back().down);
    assert(!anyHeld());
    const size_t ended = gestures.size();
    release();
    assert(gestures.size() == ended); // exactly one release callback

    assert(tap(30, 195).nav == hal::N_ESC); // visible Menu, no accidental gesture
    assert(gestures.size() == ended);
    press(100, 90);
    setRite(&RITE_SPECTRA);
    const size_t switched = gestures.size();
    press(120, 90);
    assert(gestures.size() == switched); // held contact does not enter a new rite
    release();
    logicalTap(60, 60);
    assert(gestures.size() == switched + 2);

    setRite(&RITE_GLYPHSONG);
    const size_t beforeKeyboard = gestures.size();
    logicalTap(100, 40); // compose line opens the keyboard
    assert(viewportBottom() == 110 && gestures.size() == beforeKeyboard);
    assert(tap(16, 149).ch == 'q');
    const auto caret = tap(102, 228);
    assert(caret.nav == hal::N_LEFT && caret.ctrl && !caret.fn);
    tap(248, 228); // Hide keeps the instrument open
    assert(viewportBottom() == 180);
    logicalTap(210, 90);
    assert(gestures.size() == beforeKeyboard + 2);

    setRite(nullptr);
    openCarouselSettings();
    logicalTap(120, 85); // Display settings
    logicalTap(132, 50); // Portrait
    assert(orientation() == Orientation::Portrait);
    logicalTap(200, 50); // Flip 180
    assert(orientation() == Orientation::Portrait180);
    backCarouselSettings(); backCarouselSettings();
    setRite(&RITE_CANON);
    assert(displayWidth() == 240 && displayHeight() == 320 && viewportTop() == 47);
    const size_t beforeMargin = gestures.size();
    tap(120, 15); // letterbox is not the instrument
    assert(gestures.size() == beforeMargin);
    logicalTap(180, 60);
    assert(gestures[beforeMargin].x == 180 && gestures[beforeMargin].y == 60);
    setRite(nullptr); openCarouselSettings();
    logicalTap(120, 85); logicalTap(52, 50); // Landscape, retain 180 flag
    assert(orientation() == Orientation::Landscape180);
    logicalTap(200, 50);
    assert(orientation() == Orientation::Landscape);
    backCarouselSettings(); backCarouselSettings();

    const std::string file = "/tmp/diviner-ui-test-" + std::to_string(getpid()) + ".bin";
    setenv("GRIM_SETTINGS", file.c_str(), 1);
    auto colour = theme::presetColourway(theme::ColourScheme::Neon);
    colour.rgb[3] = 0x1248ce; colour.brightness = -17;
    assert(saveColourway(colour));
    theme::setColourScheme(theme::ColourScheme::Ember);
    loadSettings();
    assert(theme::colourScheme() == theme::ColourScheme::Custom);
    assert(theme::customColourway().rgb[3] == 0x1248ce && theme::customColourway().brightness == -17);
    cyd_settings_file::Record record{};
    assert(cyd_settings_file::read(record));
    record[4] = 250; // checksum-valid but invalid orientation is rejected
    assert(cyd_settings_file::write(record));
    theme::setColourScheme(theme::ColourScheme::Ember); loadSettings();
    assert(theme::colourScheme() == theme::ColourScheme::Ember);
    assert(orientation() == Orientation::Landscape);
    FILE *f = std::fopen(file.c_str(), "ab"); assert(f); std::fputc('x', f); std::fclose(f);
    assert(!cyd_settings_file::read(record)); // trailing bytes rejected
    setenv("GRIM_SETTINGS", "/missing-diviner-directory/settings", 1);
    assert(!saveColourway(colour));
    assert(theme::colourScheme() == theme::ColourScheme::Ember);
    std::remove(file.c_str());
    delayedScriptRegression();
    std::puts("PASS CYD controls: gesture capture/release, lifecycle isolation, docks, glyph keyboard/caret, four orientations, palette persistence/failure");
}
