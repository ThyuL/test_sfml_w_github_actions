// ---------------------------------------------------------------------
// SFML 2.6.2 port of the "Cursor Effect" overlay with advanced physics.
//
// Build (MinGW example):
//   g++ CursorEffectSFML.cpp -o CursorEffect.exe -lsfml-graphics -lsfml-window
//       -lsfml-system -ldwmapi -lgdi32 -static -mwindows
// ---------------------------------------------------------------------

#include <SFML/Graphics.hpp>
#include <windows.h>
#include <dwmapi.h>
#include <iostream>
#include <fstream>
#include <sstream>
#include <vector>
#include <string>
#include <cmath>
#include <cstring>
#include <random>
#include <unordered_map>
#include <algorithm>
#include <atomic>

#pragma comment(lib, "dwmapi.lib")

// ---------------------------------------------------------------------
// Global Mouse Hook (For capturing scroll everywhere)
// ---------------------------------------------------------------------
static HHOOK g_mouseHook = nullptr;
static std::atomic<float> g_scrollAccum{0.0f};

static LRESULT CALLBACK MouseHookCallback(int nCode, WPARAM wParam, LPARAM lParam) {
    if (nCode == HC_ACTION) {
        if (wParam == WM_MOUSEWHEEL) {
            MSLLHOOKSTRUCT* pMouse = reinterpret_cast<MSLLHOOKSTRUCT*>(lParam);
            short wheelDelta = HIWORD(pMouse->mouseData);
            float delta = static_cast<float>(wheelDelta) / 120.0f;
            g_scrollAccum = g_scrollAccum + delta;
        }
    }
    return CallNextHookEx(g_mouseHook, nCode, wParam, lParam);
}

// ---------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------
static std::mt19937 g_rng{ std::random_device{}() };

// NOTE: std::uniform_int_distribution requires lo <= hi. Callers must not
// pass a reversed range (that was the cause of the left-click crash below).
static int RandInt(int lo, int hi) {
    std::uniform_int_distribution<int> d(lo, hi);
    return d(g_rng);
}

static float ClampF(float v, float lo, float hi) {
    if (v < lo) return lo;
    if (v > hi) return hi;
    return v;
}

static sf::Color Fade(sf::Color c, float alpha01) {
    c.a = static_cast<sf::Uint8>(ClampF(alpha01 * 255.0f, 0.0f, 255.0f));
    return c;
}

static sf::Color ColorFromHSV(float h, float s, float v) {
    float c = v * s;
    float x = c * (1.0f - std::fabs(std::fmod(h / 60.0f, 2.0f) - 1.0f));
    float m = v - c;
    float r, g, b;
    if (h < 60)       { r = c; g = x; b = 0; }
    else if (h < 120) { r = x; g = c; b = 0; }
    else if (h < 180) { r = 0; g = c; b = x; }
    else if (h < 240) { r = 0; g = x; b = c; }
    else if (h < 300) { r = x; g = 0; b = c; }
    else              { r = c; g = 0; b = x; }
    return sf::Color(
        static_cast<sf::Uint8>((r + m) * 255),
        static_cast<sf::Uint8>((g + m) * 255),
        static_cast<sf::Uint8>((b + m) * 255));
}

// Raylib's non-standard named colors that SFML doesn't ship.
static const sf::Color SKYBLUE_C(102, 191, 255);
static const sf::Color GOLD_C(255, 203, 0);
static const sf::Color ORANGE_C(255, 161, 0);
static const sf::Color PINK_C(255, 109, 194);
static const sf::Color VIOLET_C(135, 60, 190);
static const sf::Color BROWN_C(127, 106, 79);
static const sf::Color LIME_C(0, 158, 47);
static const sf::Color BEIGE_C(211, 176, 131);
static const sf::Color MAROON_C(190, 33, 55);
static const sf::Color DARKBLUE_C(0, 82, 172);
static const sf::Color DARKGREEN_C(0, 117, 44);
static const sf::Color DARKBROWN_C(76, 63, 47);
static const sf::Color DARKPURPLE_C(112, 31, 126);
static const sf::Color RAYWHITE_C(245, 245, 245);

static bool ParseColorName(const std::string& nameIn, sf::Color& outColor, bool& outIsRainbow) {
    std::string name = nameIn;
    std::transform(name.begin(), name.end(), name.begin(), ::tolower);
    name.erase(std::remove_if(name.begin(), name.end(), ::isspace), name.end());
    outIsRainbow = false;

    if (name.empty()) return false;
    if (name == "rainbow") { outIsRainbow = true; return true; }

    static const std::unordered_map<std::string, sf::Color> table = {
        {"white", sf::Color::White}, {"black", sf::Color::Black},
        {"red", sf::Color::Red}, {"green", sf::Color::Green},
        {"blue", sf::Color::Blue}, {"yellow", sf::Color::Yellow},
        {"gold", GOLD_C}, {"orange", ORANGE_C}, {"pink", PINK_C},
        {"purple", sf::Color(200, 122, 255)}, {"violet", VIOLET_C},
        {"brown", BROWN_C}, {"lime", LIME_C}, {"skyblue", SKYBLUE_C},
        {"beige", BEIGE_C}, {"magenta", sf::Color::Magenta},
        {"maroon", MAROON_C}, {"darkgray", sf::Color(80, 80, 80)},
        {"lightgray", sf::Color(200, 200, 200)}, {"gray", sf::Color(130, 130, 130)},
        {"grey", sf::Color(130, 130, 130)}, {"darkblue", DARKBLUE_C},
        {"darkgreen", DARKGREEN_C}, {"darkbrown", DARKBROWN_C},
        {"darkpurple", DARKPURPLE_C}, {"raywhite", RAYWHITE_C},
    };

    auto it = table.find(name);
    if (it == table.end()) return false;
    outColor = it->second;
    return true;
}

// ---------------------------------------------------------------------
// Particle System
// ---------------------------------------------------------------------
struct Particle {
    sf::Vector2f pos;
    sf::Vector2f vel;
    float initialRadius;
    float radius;
    float life;
    float maxLife;
    sf::Color color;

    // Physics & Shape Properties
    float friction;
    float gravity;
    bool isSquare;
};

static void SpawnTrail(std::vector<Particle>& particles, sf::Vector2f pos, float baseRad, float alpha) {
    Particle p;
    p.pos = pos;
    p.vel = { RandInt(-20, 20) / 10.0f, RandInt(-20, 20) / 10.0f };
    p.initialRadius = baseRad * (RandInt(50, 150) / 100.0f);
    p.radius = p.initialRadius;
    p.life = 0.0f;
    p.maxLife = RandInt(40, 80) / 100.0f;
    p.color = Fade(sf::Color::White, alpha);
    p.friction = 0.96f;
    p.gravity = 0.0f;
    p.isSquare = false;
    particles.push_back(p);
}

// LEFT CLICK: Sparkle Shower (Explodes up, falls down)
// `spread` scales how wide the burst is (both the sideways cone and the
// upward launch speed), default 1.0.
static void SpawnLeftClick(std::vector<Particle>& particles, sf::Vector2f pos, float baseRad, float alpha, sf::Color color, float spread, int count) {
    int sideRange = static_cast<int>(250.0f * spread);
    if (sideRange < 1) sideRange = 1;
    int upMin = static_cast<int>(100.0f * spread);
    int upMax = static_cast<int>(600.0f * spread);
    if (upMin < 1) upMin = 1;
    if (upMax <= upMin) upMax = upMin + 1;

    for (int i = 0; i < count; i++) {
        Particle p;
        p.pos = pos;
        p.vel.x = static_cast<float>(RandInt(-sideRange, sideRange));
        // Shoot upwards. NOTE: uniform_int_distribution requires lo <= hi,
        // so the more-negative value (upMax) must come first.
        p.vel.y = static_cast<float>(RandInt(-upMax, -upMin));
        p.initialRadius = baseRad * (RandInt(40, 100) / 100.0f);
        p.radius = p.initialRadius;
        p.life = 0.0f;
        p.maxLife = RandInt(40, 80) / 100.0f;
        p.color = Fade(color, alpha);
        p.friction = 0.98f;
        p.gravity = 1400.0f; // High gravity
        p.isSquare = false;
        particles.push_back(p);
    }
}

// RIGHT CLICK: Double Ring (Concentric spinning diamonds)
// `spread` scales the ring speeds, which widens how far the rings travel.
static void SpawnRightClick(std::vector<Particle>& particles, sf::Vector2f pos, float baseRad, float alpha, sf::Color color, float spread, int count) {
    int counts[2] = { count / 2, count };
    float speeds[2] = { 150.0f * spread, 300.0f * spread };
    for (int r = 0; r < 2; r++) {
        for (int i = 0; i < counts[r]; i++) {
            float angle = (static_cast<float>(i) / counts[r]) * 2.0f * 3.14159265f;
            Particle p;
            p.pos = pos;
            p.vel = { std::cos(angle) * speeds[r], std::sin(angle) * speeds[r] };
            p.initialRadius = baseRad * 0.5f;
            p.radius = p.initialRadius;
            p.life = 0.0f;
            p.maxLife = 0.5f;
            p.color = Fade(color, alpha);
            p.friction = 0.94f;
            p.gravity = 0.0f;
            p.isSquare = true;
            particles.push_back(p);
        }
    }
}

// MIDDLE CLICK: Nova (Dense, fast rays that brake hard)
// `spread` scales both ray speed and the angular jitter, making the nova
// reach further and fan out wider.
static void SpawnMiddleClick(std::vector<Particle>& particles, sf::Vector2f pos, float baseRad, float alpha, sf::Color color, bool isRainbow, float spread, int count) {
    int rays = 8;
    int jitterDeg = static_cast<int>(5.0f * spread);
    if (jitterDeg < 1) jitterDeg = 1;

    for (int i = 0; i < count; i++) {
        int rayIndex = i % rays;
        float angle = (static_cast<float>(rayIndex) / rays) * 2.0f * 3.14159265f;
        angle += static_cast<float>(RandInt(-jitterDeg, jitterDeg)) * (3.14159265f / 180.0f); // jitter
        float speed = static_cast<float>(RandInt(150, 700)) * spread;

        Particle p;
        p.pos = pos;
        p.vel = { std::cos(angle) * speed, std::sin(angle) * speed };
        p.initialRadius = baseRad * (RandInt(60, 150) / 100.0f);
        p.radius = p.initialRadius;
        p.life = 0.0f;
        p.maxLife = RandInt(50, 90) / 100.0f;
        p.color = Fade(isRainbow ? ColorFromHSV(static_cast<float>(RandInt(0, 360)), 1.0f, 1.0f) : color, alpha);
        p.friction = 0.88f;
        p.gravity = 0.0f;
        p.isSquare = false;
        particles.push_back(p);
    }
}

// SCROLL: Directional Jet (Shoots opposite to scroll)
// `spread` widens the sideways range of the jet, turning a narrow beam into
// a wider fan/sector.
static void SpawnScroll(std::vector<Particle>& particles, sf::Vector2f pos, float baseRad, float scrollDelta, float alpha, sf::Color color, float spread) {
    int count = static_cast<int>(std::abs(scrollDelta) * 20.0f);
    int sideRange = static_cast<int>(150.0f * spread);
    if (sideRange < 1) sideRange = 1;

    for (int i = 0; i < count; i++) {
        Particle p;
        p.pos = pos;
        p.vel.x = static_cast<float>(RandInt(-sideRange, sideRange));

        // Scroll > 0 (Up) -> Shoot Down (+Y) | Scroll < 0 (Down) -> Shoot Up (-Y)
        if (scrollDelta > 0) p.vel.y = static_cast<float>(RandInt(300, 900));
        else                 p.vel.y = static_cast<float>(RandInt(-900, -300));

        p.initialRadius = baseRad * (RandInt(50, 130) / 100.0f);
        p.radius = p.initialRadius;
        p.life = 0.0f;
        p.maxLife = RandInt(40, 70) / 100.0f;
        p.color = Fade(color, alpha);
        p.friction = 0.95f;
        p.gravity = 0.0f;
        p.isSquare = false;
        particles.push_back(p);
    }
}

// ---------------------------------------------------------------------
// UI widgets
// ---------------------------------------------------------------------
struct UiSlider {
    sf::FloatRect bounds;
    float min, max;
    std::string label;
};

static void UpdateAndDrawSlider(sf::RenderWindow& window, const sf::Font& font,
                                 const UiSlider& s, float* value, bool mouseOverPanel,
                                 sf::Vector2f mouse, bool mouseDown) {
    float t = ClampF((*value - s.min) / (s.max - s.min), 0.0f, 1.0f);
    float handleX = s.bounds.left + t * s.bounds.width;
    sf::FloatRect handle(handleX - 6, s.bounds.top - 4, 12, s.bounds.height + 8);

    bool hovering = mouseOverPanel && s.bounds.contains(mouse);
    bool dragging = mouseOverPanel && mouseDown && (handle.contains(mouse) || hovering);

    if (dragging) {
        float nt = ClampF((mouse.x - s.bounds.left) / s.bounds.width, 0.0f, 1.0f);
        *value = s.min + nt * (s.max - s.min);
        t = nt;
        handleX = s.bounds.left + t * s.bounds.width;
    }

    sf::RectangleShape track({ s.bounds.width, s.bounds.height });
    track.setPosition(s.bounds.left, s.bounds.top);
    track.setFillColor(Fade(sf::Color(80, 80, 80), 0.6f));
    window.draw(track);

    sf::RectangleShape fill({ t * s.bounds.width, s.bounds.height });
    fill.setPosition(s.bounds.left, s.bounds.top);
    fill.setFillColor(Fade(SKYBLUE_C, 0.8f));
    window.draw(fill);

    sf::RectangleShape handleShape({ 12, s.bounds.height + 8 });
    handleShape.setPosition(handleX - 6, s.bounds.top - 4);
    handleShape.setFillColor(RAYWHITE_C);
    window.draw(handleShape);

    // Trim trailing zeros from string for cleanliness
    std::string valStr = std::to_string(*value);
    valStr.erase(valStr.find_last_not_of('0') + 1, std::string::npos);
    if (!valStr.empty() && valStr.back() == '.') valStr.pop_back();

    sf::Text text(s.label + ": " + valStr, font, 14);
    text.setPosition(s.bounds.left, s.bounds.top - 18);
    text.setFillColor(RAYWHITE_C);
    window.draw(text);
}

struct UiTextBox {
    sf::FloatRect bounds;
    std::string buffer;
    bool active;
};

static void UpdateAndDrawTextBox(sf::RenderWindow& window, const sf::Font& font,
                                 UiTextBox& box, const std::string& label,
                                 bool mouseOverPanel) {
    sf::RectangleShape rect({ box.bounds.width, box.bounds.height });
    rect.setPosition(box.bounds.left, box.bounds.top);
    rect.setFillColor(box.active ? Fade(SKYBLUE_C, 0.35f) : Fade(sf::Color(80, 80, 80), 0.6f));
    rect.setOutlineThickness(1);
    rect.setOutlineColor(box.active ? RAYWHITE_C : sf::Color(150, 150, 150));
    window.draw(rect);

    sf::Text bufText(box.buffer, font, 16);
    bufText.setPosition(box.bounds.left + 6, box.bounds.top + 6);
    bufText.setFillColor(RAYWHITE_C);
    window.draw(bufText);

    sf::Text labelText(label, font, 14);
    labelText.setPosition(box.bounds.left, box.bounds.top - 18);
    labelText.setFillColor(RAYWHITE_C);
    window.draw(labelText);
}

// ---------------------------------------------------------------------
// Per-effect tunable settings (Size / Alpha / Spread) for Left, Right,
// Middle and Scroll. Colors are kept directly in the UiTextBox buffers
// (boxL/boxR/boxM/boxS) so that whatever text the user typed round-trips
// through the config file exactly as-is.
// ---------------------------------------------------------------------
struct EffectConfig {
    float size;
    float alpha;
    float spread;
};

enum EffectIndex { EFFECT_LEFT = 0, EFFECT_RIGHT = 1, EFFECT_MIDDLE = 2, EFFECT_SCROLL = 3, EFFECT_COUNT = 4 };
static const char* kEffectNames[EFFECT_COUNT] = { "Left", "Right", "Middle", "Scroll" };

// ---------------------------------------------------------------------
// Config persistence — minimal hand-written XML reader/writer.
// This avoids pulling in an external XML/YAML library (which would need
// extra -l flags in _compile.bat); it just reads/writes our own fixed
// schema as plain, valid XML text that can also be opened in any editor.
// ---------------------------------------------------------------------
static const char* CONFIG_PATH_PRIMARY  = "..\\resources\\cursor_effect_config.xml";
static const char* CONFIG_PATH_FALLBACK = "..\\cursor_effect_config.xml";

static std::string XmlEscape(const std::string& s) {
    std::string out;
    out.reserve(s.size());
    for (char c : s) {
        switch (c) {
            case '&':  out += "&amp;";  break;
            case '<':  out += "&lt;";   break;
            case '>':  out += "&gt;";   break;
            case '"':  out += "&quot;"; break;
            default:   out += c;        break;
        }
    }
    return out;
}

static std::string XmlUnescape(const std::string& s) {
    std::string out;
    out.reserve(s.size());
    for (size_t i = 0; i < s.size(); ) {
        if (s.compare(i, 5, "&amp;") == 0)       { out += '&'; i += 5; }
        else if (s.compare(i, 4, "&lt;") == 0)   { out += '<'; i += 4; }
        else if (s.compare(i, 4, "&gt;") == 0)   { out += '>'; i += 4; }
        else if (s.compare(i, 6, "&quot;") == 0) { out += '"'; i += 6; }
        else { out += s[i]; i += 1; }
    }
    return out;
}

// Finds `attr="value"` within a single tag's text and returns the value (unescaped).
static bool XmlGetAttr(const std::string& tagText, const std::string& attr, std::string& out) {
    std::string needle = attr + "=\"";
    size_t p = tagText.find(needle);
    if (p == std::string::npos) return false;
    p += needle.size();
    size_t end = tagText.find('"', p);
    if (end == std::string::npos) return false;
    out = XmlUnescape(tagText.substr(p, end - p));
    return true;
}

static bool XmlFindTag(const std::string& xml, const std::string& tagName, std::string& outTagText) {
    std::string needle = "<" + tagName;
    size_t p = xml.find(needle);
    if (p == std::string::npos) return false;
    size_t end = xml.find('>', p);
    if (end == std::string::npos) return false;
    outTagText = xml.substr(p, end - p + 1);
    return true;
}

static float XmlGetFloatAttr(const std::string& tagText, const std::string& attr, float def) {
    std::string v;
    if (!XmlGetAttr(tagText, attr, v)) return def;
    try { return std::stof(v); } catch (...) { return def; }
}

static std::string XmlGetStrAttr(const std::string& tagText, const std::string& attr, const std::string& def) {
    std::string v;
    if (!XmlGetAttr(tagText, attr, v)) return def;
    return v;
}

static bool SaveConfig(float trailSize, float trailAlpha,
                        UiTextBox* boxes[EFFECT_COUNT],
                        EffectConfig configs[EFFECT_COUNT]) {
    std::ostringstream ss;
    ss << "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n";
    ss << "<CursorEffectConfig version=\"1\">\n";
    ss << "    <Trail size=\"" << trailSize << "\" alpha=\"" << trailAlpha << "\" />\n";
    for (int i = 0; i < EFFECT_COUNT; i++) {
        ss << "    <" << kEffectNames[i]
           << " color=\"" << XmlEscape(boxes[i]->buffer) << "\""
           << " size=\""   << configs[i].size   << "\""
           << " alpha=\""  << configs[i].alpha  << "\""
           << " spread=\"" << configs[i].spread << "\" />\n";
    }
    ss << "</CursorEffectConfig>\n";

    std::ofstream out(CONFIG_PATH_PRIMARY, std::ios::trunc);
    if (!out.is_open()) {
        out.open(CONFIG_PATH_FALLBACK, std::ios::trunc);
        if (!out.is_open()) return false;
    }
    out << ss.str();
    return true;
}

static bool LoadConfig(float& trailSize, float& trailAlpha,
                        UiTextBox* boxes[EFFECT_COUNT],
                        EffectConfig configs[EFFECT_COUNT]) {
    std::ifstream in(CONFIG_PATH_PRIMARY);
    if (!in.is_open()) {
        in.open(CONFIG_PATH_FALLBACK);
        if (!in.is_open()) return false;
    }
    std::ostringstream buf;
    buf << in.rdbuf();
    std::string xml = buf.str();

    std::string tag;
    if (XmlFindTag(xml, "Trail", tag)) {
        trailSize  = XmlGetFloatAttr(tag, "size",  trailSize);
        trailAlpha = XmlGetFloatAttr(tag, "alpha", trailAlpha);
    }
    for (int i = 0; i < EFFECT_COUNT; i++) {
        if (XmlFindTag(xml, kEffectNames[i], tag)) {
            boxes[i]->buffer   = XmlGetStrAttr(tag, "color", boxes[i]->buffer);
            configs[i].size    = XmlGetFloatAttr(tag, "size",   configs[i].size);
            configs[i].alpha   = XmlGetFloatAttr(tag, "alpha",  configs[i].alpha);
            configs[i].spread  = XmlGetFloatAttr(tag, "spread", configs[i].spread);
        }
    }
    return true;
}

// ---------------------------------------------------------------------
// Main
// ---------------------------------------------------------------------
int main() {
    sf::VideoMode desktop = sf::VideoMode::getDesktopMode();
    unsigned int W = desktop.width;
    unsigned int H = desktop.height;

    sf::RenderWindow window(sf::VideoMode(W - 1, H - 1), "Cursor Effect", sf::Style::None);
    window.setFramerateLimit(120);
    window.setPosition({ 0, 0 });

    HWND hwnd = window.getSystemHandle();

    // --- Enable per-pixel transparency (WS_EX_LAYERED + DWM extend frame) ---
    LONG_PTR exStyle = GetWindowLongPtr(hwnd, GWL_EXSTYLE);
    SetWindowLongPtr(hwnd, GWL_EXSTYLE, exStyle | WS_EX_LAYERED);
    MARGINS margins = { -1, -1, -1, -1 };
    DwmExtendFrameIntoClientArea(hwnd, &margins);

    // --- Always-on-top ---
    SetWindowPos(hwnd, HWND_TOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE);

    // --- Start click-through (mouse passthrough) ---
    bool interactive = false;
    bool panelOpen = false;
    auto SetClickThrough = [&](bool enable) {
        LONG_PTR es = GetWindowLongPtr(hwnd, GWL_EXSTYLE);
        if (enable) es |= WS_EX_TRANSPARENT;
        else        es &= ~WS_EX_TRANSPARENT;
        SetWindowLongPtr(hwnd, GWL_EXSTYLE, es);
    };
    SetClickThrough(true);

    // --- Install global mouse hook ---
    g_mouseHook = SetWindowsHookEx(WH_MOUSE_LL, MouseHookCallback, GetModuleHandle(nullptr), 0);

    // --- Font (REQUIRED: ship a ttf next to the exe) ---
    sf::Font font;
    if (!font.loadFromFile("resources\\arial.ttf")) {
        std::cerr << "Warning: could not load resources\\arial.ttf - UI text will not render.\n";
    }

    float panelW = 460.0f;
    float panelH = 420.0f;
    sf::FloatRect panelBounds((W - panelW) / 2.0f, (H - panelH) / 2.0f, panelW, panelH);

    // --- Trail sliders (always visible; control the ambient cursor trail) ---
    UiSlider sliderTrailSize  = { { panelBounds.left + 20, panelBounds.top + 55,  420, 10 }, 5.0f, 60.0f, "Trail Size" };
    UiSlider sliderTrailAlpha = { { panelBounds.left + 20, panelBounds.top + 105, 420, 10 }, 0.0f, 0.3f, "Trail Alpha" };

    // --- Per-effect sliders (re-bound to whichever box below is selected) ---
    UiSlider sliderEffSize   = { { panelBounds.left + 20, panelBounds.top + 230, 420, 10 }, 5.0f,  60.0f, "Size" };
    UiSlider sliderEffAlpha  = { { panelBounds.left + 20, panelBounds.top + 280, 420, 10 }, 0.05f, 1.0f,  "Alpha" };
    UiSlider sliderEffSpread = { { panelBounds.left + 20, panelBounds.top + 330, 420, 10 }, 0.3f,  2.5f,  "Spread" };

    // --- Color text boxes: Left / Right / Middle click, and Scroll ---
    const float boxW = 94.0f, boxGap = 14.0f, boxTop = panelBounds.top + 165.0f;
    UiTextBox boxL = { { panelBounds.left + 20 + 0 * (boxW + boxGap), boxTop, boxW, 28 }, "yellow",  false };
    UiTextBox boxR = { { panelBounds.left + 20 + 1 * (boxW + boxGap), boxTop, boxW, 28 }, "blue",    false };
    UiTextBox boxM = { { panelBounds.left + 20 + 2 * (boxW + boxGap), boxTop, boxW, 28 }, "rainbow", false };
    UiTextBox boxS = { { panelBounds.left + 20 + 3 * (boxW + boxGap), boxTop, boxW, 28 }, "purple",  false };
    UiTextBox* boxes[EFFECT_COUNT] = { &boxL, &boxR, &boxM, &boxS };

    EffectConfig configs[EFFECT_COUNT] = {
        /* Left   */ { 20.0f, 0.5f, 1.0f },
        /* Right  */ { 20.0f, 0.5f, 1.0f },
        /* Middle */ { 20.0f, 0.5f, 1.0f },
        /* Scroll */ { 20.0f, 1.0f, 1.0f },
    };

    std::vector<Particle> particles;
    particles.reserve(2048);

    float trailSize  = 20.0f;
    float trailAlpha = 0.03f;
    float trailRate  = 1.0f;

    // Load saved settings, if any, over the defaults above.
    LoadConfig(trailSize, trailAlpha, boxes, configs);

    int selectedEffect = EFFECT_LEFT; // which effect the 3 sliders above currently edit

    sf::Color leftColor       = sf::Color::Yellow;
    sf::Color rightColor      = sf::Color::Blue;
    sf::Color middleColor     = sf::Color::White;
    bool      middleIsRainbow = true;
    sf::Color scrollColor     = sf::Color(200, 122, 255);

    const float HOLD_MIN_ALPHA        = 0.03f;
    const float HOLD_DECAY_RATE       = 1.1f;
    const float HOLD_SPAWN_INTERVAL   = 0.06f;
    const int   HOLD_CONTINUOUS_COUNT = 6;

    bool leftWasDown = false, rightWasDown = false, middleWasDown = false;
    bool spaceWasDown = false;
    double leftPressTime = 0, rightPressTime = 0, middlePressTime = 0;
    double leftLastSpawn = 0, rightLastSpawn = 0, middleLastSpawn = 0;
    float trailAccum = 0.0f;

    sf::Clock clock;
    sf::Clock runClock;

    while (window.isOpen()) {
        sf::Event event;
        while (window.pollEvent(event)) {
            if (event.type == sf::Event::Closed) {
                window.close();
            } else if (event.type == sf::Event::TextEntered && panelOpen) {
                // Ignore raw spacebar text event so it doesn't type into textboxes
                // when trying to close the overlay
                unsigned int u = event.text.unicode;
                if (u != 32) {
                    UiTextBox* active = boxL.active ? &boxL : (boxM.active ? &boxM : (boxR.active ? &boxR : (boxS.active ? &boxS : nullptr)));
                    if (active) {
                        if (u == 8) { // backspace
                            if (!active->buffer.empty()) active->buffer.pop_back();
                        } else if (u >= 33 && u <= 125 && active->buffer.size() < 31) {
                            active->buffer.push_back(static_cast<char>(u));
                        }
                    }
                }
            } else if (event.type == sf::Event::KeyPressed && event.key.code == sf::Keyboard::Escape) {
                window.close();
            }
        }

        // Toggle UI: Checks if SFML window is in focus, then checks Spacebar
        bool spaceDown = window.hasFocus() && sf::Keyboard::isKeyPressed(sf::Keyboard::Space);
        if (spaceDown && !spaceWasDown) {
            interactive = !interactive;
            panelOpen = interactive;
            SetClickThrough(!interactive);
            if (!interactive) {
                boxL.active = boxM.active = boxR.active = boxS.active = false;
                SaveConfig(trailSize, trailAlpha, boxes, configs);
            }
        }
        spaceWasDown = spaceDown;

        sf::Vector2i mousePixel = sf::Mouse::getPosition(window);
        sf::Vector2f mPos(static_cast<float>(mousePixel.x), static_cast<float>(mousePixel.y));

        bool mouseOverPanel = panelOpen && panelBounds.contains(mPos);

        // Fetch scroll data from the Windows hook thread
        float scrollDelta = g_scrollAccum.exchange(0.0f);
        if (scrollDelta != 0.0f && !mouseOverPanel) {
            SpawnScroll(particles, mPos, configs[EFFECT_SCROLL].size, scrollDelta,
                        configs[EFFECT_SCROLL].alpha, scrollColor, configs[EFFECT_SCROLL].spread);
        }

        double now = runClock.getElapsedTime().asSeconds();
        // Fall back to GetAsyncKeyState for mouse buttons because SFML's sf::Mouse::isButtonPressed
        // doesn't always trigger reliably when clicking globally across other apps.
        bool leftDown   = (GetAsyncKeyState(VK_LBUTTON) & 0x8000) != 0;
        bool rightDown  = (GetAsyncKeyState(VK_RBUTTON) & 0x8000) != 0;
        bool middleDown = (GetAsyncKeyState(VK_MBUTTON) & 0x8000) != 0;

        // Captured before leftWasDown is overwritten below, so it can be reused
        // afterwards to detect "just clicked" for selecting a config box.
        bool leftClickEdge = leftDown && !leftWasDown;

        if (!mouseOverPanel) {
            if (leftClickEdge) {
                leftPressTime = now;
                leftLastSpawn = now;
                SpawnLeftClick(particles, mPos, configs[EFFECT_LEFT].size, configs[EFFECT_LEFT].alpha,
                               leftColor, configs[EFFECT_LEFT].spread, 24);
            } else if (leftDown && leftWasDown && now - leftLastSpawn >= HOLD_SPAWN_INTERVAL) {
                float held = static_cast<float>(now - leftPressTime);
                float a = ClampF(configs[EFFECT_LEFT].alpha * std::exp(-held * HOLD_DECAY_RATE), HOLD_MIN_ALPHA, configs[EFFECT_LEFT].alpha);
                SpawnLeftClick(particles, mPos, configs[EFFECT_LEFT].size, a, leftColor, configs[EFFECT_LEFT].spread, HOLD_CONTINUOUS_COUNT);
                leftLastSpawn = now;
            }

            if (rightDown && !rightWasDown) {
                rightPressTime = now;
                rightLastSpawn = now;
                SpawnRightClick(particles, mPos, configs[EFFECT_RIGHT].size, configs[EFFECT_RIGHT].alpha,
                                rightColor, configs[EFFECT_RIGHT].spread, 32);
            } else if (rightDown && rightWasDown && now - rightLastSpawn >= HOLD_SPAWN_INTERVAL) {
                float held = static_cast<float>(now - rightPressTime);
                float a = ClampF(configs[EFFECT_RIGHT].alpha * std::exp(-held * HOLD_DECAY_RATE), HOLD_MIN_ALPHA, configs[EFFECT_RIGHT].alpha);
                SpawnRightClick(particles, mPos, configs[EFFECT_RIGHT].size, a, rightColor, configs[EFFECT_RIGHT].spread, 12);
                rightLastSpawn = now;
            }

            if (middleDown && !middleWasDown) {
                middlePressTime = now;
                middleLastSpawn = now;
                SpawnMiddleClick(particles, mPos, configs[EFFECT_MIDDLE].size, configs[EFFECT_MIDDLE].alpha,
                                 middleColor, middleIsRainbow, configs[EFFECT_MIDDLE].spread, 40);
            } else if (middleDown && middleWasDown && now - middleLastSpawn >= HOLD_SPAWN_INTERVAL) {
                float held = static_cast<float>(now - middlePressTime);
                float a = ClampF(configs[EFFECT_MIDDLE].alpha * std::exp(-held * HOLD_DECAY_RATE), HOLD_MIN_ALPHA, configs[EFFECT_MIDDLE].alpha);
                SpawnMiddleClick(particles, mPos, configs[EFFECT_MIDDLE].size, a, middleColor, middleIsRainbow,
                                 configs[EFFECT_MIDDLE].spread, HOLD_CONTINUOUS_COUNT);
                middleLastSpawn = now;
            }
        }

        leftWasDown = leftDown;
        rightWasDown = rightDown;
        middleWasDown = middleDown;

        // Select which box gets keystrokes AND which effect the 3 sliders below edit.
        // Edge-triggered (fires once on the click), using leftClickEdge captured above.
        if (mouseOverPanel && leftClickEdge) {
            boxL.active = boxL.bounds.contains(mPos);
            boxR.active = boxR.bounds.contains(mPos);
            boxM.active = boxM.bounds.contains(mPos);
            boxS.active = boxS.bounds.contains(mPos);
            if (boxL.active) selectedEffect = EFFECT_LEFT;
            else if (boxR.active) selectedEffect = EFFECT_RIGHT;
            else if (boxM.active) selectedEffect = EFFECT_MIDDLE;
            else if (boxS.active) selectedEffect = EFFECT_SCROLL;
        }

        trailAccum += trailRate;
        while (trailAccum >= 1.0f) {
            SpawnTrail(particles, mPos, trailSize, trailAlpha);
            trailAccum -= 1.0f;
        }

        // Apply distinct physics per particle
        float dt = clock.restart().asSeconds();
        for (size_t i = 0; i < particles.size(); ) {
            Particle& p = particles[i];
            p.life += dt;

            p.vel.x *= p.friction;
            p.vel.y *= p.friction;
            p.vel.y += p.gravity * dt;

            p.pos.x += p.vel.x * dt;
            p.pos.y += p.vel.y * dt;

            float t = p.life / p.maxLife;
            p.radius = p.initialRadius * (1.0f - t);

            if (t >= 1.0f || p.radius <= 0.2f) {
                particles[i] = particles.back();
                particles.pop_back();
            } else {
                i++;
            }
        }

        window.clear(sf::Color(0, 0, 0, 0));

        // Guide text so you don't forget how to open it
        sf::Text guide("Click app in Taskbar, then press SPACEBAR to open settings", font, 16);
        guide.setPosition(10, H - 30);
        guide.setFillColor(Fade(RAYWHITE_C, 0.4f));
        window.draw(guide);

        // Draw particles
        for (const Particle& p : particles) {
            float t = p.life / p.maxLife;
            float alphaMul = 1.0f - t;
            sf::Color drawColor = Fade(p.color, (p.color.a / 255.0f) * alphaMul);

            if (p.isSquare) {
                sf::RectangleShape shape({ p.radius * 2.0f, p.radius * 2.0f });
                shape.setOrigin(p.radius, p.radius);
                shape.setPosition(p.pos);
                shape.setRotation(p.life * 200.0f); // Make the diamonds spin
                shape.setFillColor(drawColor);
                window.draw(shape);
            } else {
                sf::CircleShape shape(p.radius);
                shape.setOrigin(p.radius, p.radius);
                shape.setPosition(p.pos);
                shape.setFillColor(drawColor);
                window.draw(shape);
            }
        }

        if (panelOpen) {
            sf::RectangleShape panelBg({ panelBounds.width, panelBounds.height });
            panelBg.setPosition(panelBounds.left, panelBounds.top);
            panelBg.setFillColor(sf::Color::Black);
            panelBg.setOutlineThickness(2);
            panelBg.setOutlineColor(RAYWHITE_C);
            window.draw(panelBg);

            sf::Text title("Cursor Effect Settings (Spacebar to close)", font, 14);
            title.setPosition(panelBounds.left + 10, panelBounds.top + 10);
            title.setFillColor(RAYWHITE_C);
            window.draw(title);

            bool mouseDown = sf::Mouse::isButtonPressed(sf::Mouse::Left);

            // Always-visible trail controls
            UpdateAndDrawSlider(window, font, sliderTrailSize, &trailSize, mouseOverPanel, mPos, mouseDown);
            UpdateAndDrawSlider(window, font, sliderTrailAlpha, &trailAlpha, mouseOverPanel, mPos, mouseDown);

            sf::Text divider("Click a box below to edit that effect:", font, 13);
            divider.setPosition(panelBounds.left + 20, panelBounds.top + 143);
            divider.setFillColor(sf::Color(200, 200, 200));
            window.draw(divider);

            // Color boxes for Left / Right / Middle click, and Scroll
            UpdateAndDrawTextBox(window, font, boxL, "Left", mouseOverPanel);
            UpdateAndDrawTextBox(window, font, boxR, "Right", mouseOverPanel);
            UpdateAndDrawTextBox(window, font, boxM, "Middle", mouseOverPanel);
            UpdateAndDrawTextBox(window, font, boxS, "Scroll", mouseOverPanel);

            // The 3 sliders below always edit whichever box was last clicked.
            EffectConfig& sel = configs[selectedEffect];
            sliderEffSize.label   = std::string(kEffectNames[selectedEffect]) + " Size";
            sliderEffAlpha.label  = std::string(kEffectNames[selectedEffect]) + " Alpha";
            sliderEffSpread.label = std::string(kEffectNames[selectedEffect]) + " Spread";
            UpdateAndDrawSlider(window, font, sliderEffSize, &sel.size, mouseOverPanel, mPos, mouseDown);
            UpdateAndDrawSlider(window, font, sliderEffAlpha, &sel.alpha, mouseOverPanel, mPos, mouseDown);
            UpdateAndDrawSlider(window, font, sliderEffSpread, &sel.spread, mouseOverPanel, mPos, mouseDown);

            sf::Color parsedColor; bool isRainbowParsed;

            if (ParseColorName(boxL.buffer, parsedColor, isRainbowParsed) && !isRainbowParsed) leftColor = parsedColor;
            else leftColor = sf::Color::Yellow;

            if (ParseColorName(boxR.buffer, parsedColor, isRainbowParsed) && !isRainbowParsed) rightColor = parsedColor;
            else rightColor = sf::Color::Blue;

            if (ParseColorName(boxM.buffer, parsedColor, isRainbowParsed)) {
                if (isRainbowParsed) middleIsRainbow = true;
                else { middleIsRainbow = false; middleColor = parsedColor; }
            } else {
                middleIsRainbow = true;
            }

            if (ParseColorName(boxS.buffer, parsedColor, isRainbowParsed) && !isRainbowParsed) scrollColor = parsedColor;
            else scrollColor = sf::Color(200, 122, 255);

            sf::Text hint("Type a color name (e.g. red, skyblue) - Middle also accepts 'rainbow'", font, 12);
            hint.setPosition(panelBounds.left + 20, panelBounds.top + 365);
            hint.setFillColor(sf::Color(200, 200, 200));
            window.draw(hint);

            sf::Text hint2("Settings auto-save to ..\\resources\\cursor_effect_config.xml when you close this panel", font, 12);
            hint2.setPosition(panelBounds.left + 20, panelBounds.top + 385);
            hint2.setFillColor(sf::Color(150, 150, 150));
            window.draw(hint2);
        }

        window.display();
    }

    SaveConfig(trailSize, trailAlpha, boxes, configs);

    if (g_mouseHook) UnhookWindowsHookEx(g_mouseHook);

    return 0;
}