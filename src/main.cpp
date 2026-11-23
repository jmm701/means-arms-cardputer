#include "calculator.hpp"
#include "model.hpp"
#include "storage.hpp"

#include <SDL.h>
#include <SDL_ttf.h>

#include <fcntl.h>
#include <linux/fb.h>
#include <linux/input.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <unistd.h>

#include <algorithm>
#include <cctype>
#include <cerrno>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <string>
#include <vector>

namespace fs = std::filesystem;
using namespace means;

static constexpr int LOG_W = 320;
static constexpr int LOG_H = 170;

struct Color {
    uint8_t r, g, b, a;
};

static constexpr Color COL_BG        = {0, 0, 0, 255};
static constexpr Color COL_TOPBAR    = {20, 20, 20, 255};
static constexpr Color COL_CARD      = {28, 28, 28, 255};
static constexpr Color COL_ELEVATED  = {37, 37, 37, 255};
static constexpr Color COL_BORDER    = {51, 51, 51, 255};
static constexpr Color COL_BLUE      = {0, 210, 255, 255};
static constexpr Color COL_GREEN     = {0, 230, 118, 255};
static constexpr Color COL_AMBER     = {255, 179, 0, 255};
static constexpr Color COL_TEXT      = {255, 255, 255, 255};
static constexpr Color COL_TEXT_SEC  = {166, 166, 166, 255};
static constexpr Color COL_TEXT_MUT  = {115, 115, 115, 255};

static Color hex_color(uint32_t hex) {
    return {
        static_cast<uint8_t>((hex >> 16) & 0xFF),
        static_cast<uint8_t>((hex >> 8) & 0xFF),
        static_cast<uint8_t>(hex & 0xFF),
        255
    };
}

static std::string find_font(bool bold) {
    std::vector<std::string> candidates;
    if (bold) {
        candidates = {
            "/usr/share/fonts/truetype/dejavu/DejaVuSans-Bold.ttf",
            "/usr/share/fonts/truetype/liberation/LiberationSans-Bold.ttf",
            "/usr/share/fonts/truetype/noto/NotoSans-Bold.ttf",
            "/usr/share/fonts/TTF/JetBrainsMonoNerdFont-Bold.ttf",
            "/usr/share/fonts/Adwaita/AdwaitaMono-Bold.ttf",
            "/usr/share/fonts/TTF/trebucbd.ttf",
        };
    } else {
        candidates = {
            "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf",
            "/usr/share/fonts/truetype/liberation/LiberationSans-Regular.ttf",
            "/usr/share/fonts/truetype/noto/NotoSans-Regular.ttf",
            "/usr/share/fonts/Adwaita/AdwaitaSans-Regular.ttf",
            "/usr/share/fonts/TTF/JetBrainsMonoNerdFont-Regular.ttf",
            "/usr/share/fonts/TTF/trebuc.ttf",
        };
    }
    std::error_code ec;
    for (const auto& p : candidates) {
        if (fs::exists(p, ec)) return p;
    }
    return "";
}

// Software ARGB8888 320x170 canvas
struct Canvas {
    uint32_t pixels[LOG_W * LOG_H]{};

    static uint32_t pack(Color c) {
        return (static_cast<uint32_t>(c.a) << 24) |
               (static_cast<uint32_t>(c.r) << 16) |
               (static_cast<uint32_t>(c.g) << 8) |
               static_cast<uint32_t>(c.b);
    }

    void set_px(int x, int y, Color c) {
        if (x < 0 || y < 0 || x >= LOG_W || y >= LOG_H) return;
        pixels[y * LOG_W + x] = pack(c);
    }

    void blend_px(int x, int y, uint8_t r, uint8_t g, uint8_t b, uint8_t a) {
        if (x < 0 || y < 0 || x >= LOG_W || y >= LOG_H || a == 0) return;
        if (a == 255) {
            pixels[y * LOG_W + x] = (0xFFu << 24) | (r << 16) | (g << 8) | b;
            return;
        }
        uint32_t dst = pixels[y * LOG_W + x];
        uint32_t dr = (dst >> 16) & 0xFF;
        uint32_t dg = (dst >> 8) & 0xFF;
        uint32_t db = dst & 0xFF;
        uint32_t inv = 255 - a;
        uint32_t nr = (r * a + dr * inv) / 255;
        uint32_t ng = (g * a + dg * inv) / 255;
        uint32_t nb = (b * a + db * inv) / 255;
        pixels[y * LOG_W + x] = (0xFFu << 24) | (nr << 16) | (ng << 8) | nb;
    }

    void fill_rect(int x, int y, int w, int h, Color c) {
        if (w <= 0 || h <= 0) return;
        int x0 = std::max(0, x), y0 = std::max(0, y);
        int x1 = std::min(LOG_W, x + w), y1 = std::min(LOG_H, y + h);
        uint32_t p = pack(c);
        for (int py = y0; py < y1; ++py) {
            for (int px = x0; px < x1; ++px) {
                pixels[py * LOG_W + px] = p;
            }
        }
    }

    void draw_rect(int x, int y, int w, int h, Color c) {
        if (w <= 0 || h <= 0) return;
        fill_rect(x, y, w, 1, c);
        fill_rect(x, y + h - 1, w, 1, c);
        fill_rect(x, y, 1, h, c);
        fill_rect(x + w - 1, y, 1, h, c);
    }
};

static int text_width(TTF_Font* f, const std::string& s) {
    if (!f || s.empty()) return 0;
    int w = 0, h = 0;
    TTF_SizeUTF8(f, s.c_str(), &w, &h);
    return w;
}

static std::string truncate_to_width(TTF_Font* f, const std::string& s, int max_w) {
    if (!f || s.empty() || text_width(f, s) <= max_w) return s;
    std::string out = s;
    while (!out.empty() && text_width(f, out + "..") > max_w) {
        out.pop_back();
    }
    return out.empty() ? "" : (out + "..");
}

static void draw_text(Canvas& cv, TTF_Font* f, int x, int y, const std::string& s, Color c, int max_w = 0) {
    if (!f || s.empty()) return;
    std::string txt = (max_w > 0) ? truncate_to_width(f, s, max_w) : s;
    if (txt.empty()) return;
    SDL_Color sc{c.r, c.g, c.b, c.a};
    SDL_Surface* raw = TTF_RenderUTF8_Blended(f, txt.c_str(), sc);
    if (!raw) return;
    SDL_Surface* surf = SDL_ConvertSurfaceFormat(raw, SDL_PIXELFORMAT_ARGB8888, 0);
    SDL_FreeSurface(raw);
    if (!surf) return;

    const auto* src_bytes = static_cast<const uint8_t*>(surf->pixels);
    for (int sy = 0; sy < surf->h; ++sy) {
        const auto* row = reinterpret_cast<const uint32_t*>(src_bytes + sy * surf->pitch);
        for (int sx = 0; sx < surf->w; ++sx) {
            uint32_t px = row[sx];
            uint8_t a = static_cast<uint8_t>((px >> 24) & 0xFF);
            uint8_t r = static_cast<uint8_t>((px >> 16) & 0xFF);
            uint8_t g = static_cast<uint8_t>((px >> 8) & 0xFF);
            uint8_t b = static_cast<uint8_t>(px & 0xFF);
            cv.blend_px(x + sx, y + sy, r, g, b, a);
        }
    }
    SDL_FreeSurface(surf);
}

// Linux /dev/fb_lcd backend
struct FbDisplay {
    int fd = -1;
    uint8_t* map = nullptr;
    size_t map_len = 0;
    fb_var_screeninfo vinfo{};
    fb_fix_screeninfo finfo{};

    bool open_device() {
        const char* paths[] = {"/dev/fb_lcd"};
        for (const char* p : paths) {
            fd = ::open(p, O_RDWR);
            if (fd >= 0) break;
        }
        if (fd < 0) return false;
        if (ioctl(fd, FBIOGET_FSCREENINFO, &finfo) != 0 || ioctl(fd, FBIOGET_VSCREENINFO, &vinfo) != 0) {
            ::close(fd);
            fd = -1;
            return false;
        }
        map_len = static_cast<size_t>(finfo.smem_len);
        void* m = mmap(nullptr, map_len, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
        if (m == MAP_FAILED) {
            ::close(fd);
            fd = -1;
            return false;
        }
        map = static_cast<uint8_t*>(m);
        return true;
    }

    void present(const Canvas& cv) {
        if (!map) return;
        int w = std::min(LOG_W, static_cast<int>(vinfo.xres));
        int h = std::min(LOG_H, static_cast<int>(vinfo.yres));
        if (vinfo.bits_per_pixel == 16) {
            for (int y = 0; y < h; ++y) {
                auto* dst = reinterpret_cast<uint16_t*>(map + (y + vinfo.yoffset) * finfo.line_length) + vinfo.xoffset;
                for (int x = 0; x < w; ++x) {
                    uint32_t p = cv.pixels[y * LOG_W + x];
                    uint32_t r = (p >> 16) & 0xFF;
                    uint32_t g = (p >> 8) & 0xFF;
                    uint32_t b = p & 0xFF;
                    uint16_t out = static_cast<uint16_t>(
                        ((r >> (8 - vinfo.red.length)) << vinfo.red.offset) |
                        ((g >> (8 - vinfo.green.length)) << vinfo.green.offset) |
                        ((b >> (8 - vinfo.blue.length)) << vinfo.blue.offset));
                    dst[x] = out;
                }
            }
        } else if (vinfo.bits_per_pixel == 32) {
            for (int y = 0; y < h; ++y) {
                auto* dst = reinterpret_cast<uint32_t*>(map + (y + vinfo.yoffset) * finfo.line_length) + vinfo.xoffset;
                for (int x = 0; x < w; ++x) {
                    uint32_t p = cv.pixels[y * LOG_W + x];
                    uint32_t r = (p >> 16) & 0xFF;
                    uint32_t g = (p >> 8) & 0xFF;
                    uint32_t b = p & 0xFF;
                    dst[x] = (0xFFu << 24) |
                             ((r >> (8 - vinfo.red.length)) << vinfo.red.offset) |
                             ((g >> (8 - vinfo.green.length)) << vinfo.green.offset) |
                             ((b >> (8 - vinfo.blue.length)) << vinfo.blue.offset);
                }
            }
        }
    }

    void close() {
        if (map) { munmap(map, map_len); map = nullptr; }
        if (fd >= 0) { ::close(fd); fd = -1; }
    }
};

struct KeyEvent {
    enum Type { NONE, UP, DOWN, LEFT, RIGHT, ENTER, ESC, BACKSPACE, TAB, DEL, CHAR };
    Type type = NONE;
    char ch = 0;
    bool is_repeat = false;
};

struct EvdevKeyboard {
    std::vector<int> fds;
    bool shift = false;
    bool fn = false;
    bool esc_held = false;
    uint32_t esc_down_ms = 0;

    void open_all() {
        std::vector<std::string> paths;
        std::error_code ec;
        auto add_unique = [&](const std::string& p) {
            std::string canon = fs::canonical(p, ec).string();
            if (canon.empty()) canon = p;
            if (std::find(paths.begin(), paths.end(), canon) == paths.end()) {
                paths.push_back(canon);
            }
        };
        const char* primary = "/dev/input/by-path/platform-3f804000.i2c-event";
        if (fs::exists(primary, ec)) add_unique(primary);
        if (fs::exists("/dev/input", ec)) {
            for (const auto& e : fs::directory_iterator("/dev/input", ec)) {
                auto name = e.path().filename().string();
                if (name.rfind("event", 0) == 0) {
                    add_unique(e.path().string());
                }
            }
        }
        for (const auto& p : paths) {
            int fd = ::open(p.c_str(), O_RDONLY | O_NONBLOCK);
            if (fd >= 0) fds.push_back(fd);
        }
    }

    void close_all() {
        for (int fd : fds) ::close(fd);
        fds.clear();
    }

    char map_tca8418_key(uint16_t code) const {
        // Exact mapping from /usr/share/keymaps/tca8418_keypad_m5stack_keymap.map
        static const struct { uint16_t c; char ch; } tca[] = {
            {26,'!'},{27,'@'},{39,'#'},{40,'$'},{41,'%'},{43,'^'},
            {51,'&'},{52,'*'},{53,'('},{94,')'},{55,'~'},{69,'`'},
            {70,'_'},{71,'-'},{72,'+'},{73,'='},{74,'['},{75,']'},
            {76,'{'},{77,'}'},{79,';'},{80,':'},{81,'\''},{82,'"'},
            {83,'<'},{85,'>'},{86,'\\'},{89,'|'},{90,','},{91,'.'},
            {92,'/'},{93,'?'},
        };
        for (const auto& e : tca) {
            if (e.c == code) return e.ch;
        }
        return 0;
    }

    char map_linux_key(uint16_t code) const {
        if (char tc = map_tca8418_key(code)) return tc;
        if (code >= KEY_1 && code <= KEY_9) {
            static const char* sh = "!@#$%^&*(";
            return shift ? sh[code - KEY_1] : static_cast<char>('1' + (code - KEY_1));
        }
        if (code == KEY_0) return shift ? ')' : '0';
        static const struct { uint16_t c; char n; char s; } tbl[] = {
            {KEY_Q,'q','Q'},{KEY_W,'w','W'},{KEY_E,'e','E'},{KEY_R,'r','R'},{KEY_T,'t','T'},
            {KEY_Y,'y','Y'},{KEY_U,'u','U'},{KEY_I,'i','I'},{KEY_O,'o','O'},{KEY_P,'p','P'},
            {KEY_A,'a','A'},{KEY_S,'s','S'},{KEY_D,'d','D'},{KEY_F,'f','F'},{KEY_G,'g','G'},
            {KEY_H,'h','H'},{KEY_J,'j','J'},{KEY_K,'k','K'},{KEY_L,'l','L'},
            {KEY_Z,'z','Z'},{KEY_X,'x','X'},{KEY_C,'c','C'},{KEY_V,'v','V'},{KEY_B,'b','B'},
            {KEY_N,'n','N'},{KEY_M,'m','M'},
            {KEY_SPACE,' ',' '},
        };
        for (const auto& e : tbl) {
            if (e.c == code) return shift ? e.s : e.n;
        }
        return 0;
    }

    std::vector<KeyEvent> poll(uint32_t now_ms, bool direct_nav_keys) {
        std::vector<KeyEvent> out;
        input_event ev{};
        for (int fd : fds) {
            while (::read(fd, &ev, sizeof(ev)) == static_cast<ssize_t>(sizeof(ev))) {
                if (ev.type != EV_KEY) continue;
                if (ev.code == KEY_LEFTSHIFT || ev.code == KEY_RIGHTSHIFT) {
                    shift = (ev.value != 0);
                    continue;
                }
                if (ev.code == KEY_FN) {
                    fn = (ev.value != 0);
                    continue;
                }
                bool is_esc_key = (ev.code == KEY_ESC) || ((fn || direct_nav_keys) && (ev.code == 69 || ev.code == 55));
                if (is_esc_key) {
                    if (ev.value == 1) { esc_held = true; esc_down_ms = now_ms; }
                    else if (ev.value == 0) { esc_held = false; esc_down_ms = 0; }
                }
                if (ev.value == 0) continue; // only press (1) and repeat (2)
                KeyEvent ke;
                ke.is_repeat = (ev.value == 2);
                if (fn || (direct_nav_keys && !shift)) {
                    switch (ev.code) {
                        case KEY_F: ke.type = KeyEvent::UP; break;    // CardputerZero Up
                        case KEY_X: ke.type = KeyEvent::DOWN; break;  // CardputerZero Down
                        case KEY_Z: ke.type = KeyEvent::LEFT; break;  // CardputerZero Left
                        case KEY_C: ke.type = KeyEvent::RIGHT; break; // CardputerZero Right
                        case 69:                                      // `
                        case 55: ke.type = KeyEvent::ESC; break;      // ~
                        case KEY_BACKSPACE: if (fn) ke.type = KeyEvent::DEL; break;
                        default: break;
                    }
                }
                if (ke.type == KeyEvent::NONE) {
                    switch (ev.code) {
                        case KEY_UP: ke.type = KeyEvent::UP; break;
                        case KEY_DOWN: ke.type = KeyEvent::DOWN; break;
                        case KEY_LEFT: ke.type = KeyEvent::LEFT; break;
                        case KEY_RIGHT: ke.type = KeyEvent::RIGHT; break;
                        case KEY_ENTER:
                        case KEY_KPENTER: ke.type = KeyEvent::ENTER; break;
                        case KEY_ESC: ke.type = KeyEvent::ESC; break;
                        case KEY_BACKSPACE: ke.type = KeyEvent::BACKSPACE; break;
                        case KEY_TAB: ke.type = KeyEvent::TAB; break;
                        case KEY_DELETE: ke.type = KeyEvent::DEL; break;
                        default: {
                            char c = map_linux_key(ev.code);
                            if (c != 0) { ke.type = KeyEvent::CHAR; ke.ch = c; }
                            break;
                        }
                    }
                }
                if (ke.type != KeyEvent::NONE) out.push_back(ke);
            }
        }
        return out;
    }
};

static std::string fmt_money(double v, int decimals = 2) {
    char buf[32];
    if (decimals == 3) std::snprintf(buf, sizeof(buf), "$%.3f", v);
    else std::snprintf(buf, sizeof(buf), "$%.2f", v);
    return buf;
}

static std::string fmt_num(double v, int decimals = 1) {
    char buf[32];
    if (decimals == 0) std::snprintf(buf, sizeof(buf), "%.0f", v);
    else if (decimals == 1) std::snprintf(buf, sizeof(buf), "%.1f", v);
    else std::snprintf(buf, sizeof(buf), "%.2f", v);
    return buf;
}

static std::string to_lower(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return s;
}

enum class Screen { LIST, DETAIL, IMPORT };

struct FieldDef {
    std::string label;
    bool is_toggle = false;
    std::string (*getter)(const ReloadLoad&);
    void (*setter)(ReloadLoad&, const std::string&);
};

#define FIELD_STR(LBL, MEMBER) \
    FieldDef{LBL, false, [](const ReloadLoad& l) -> std::string { return l.MEMBER; }, \
             [](ReloadLoad& l, const std::string& v) { l.MEMBER = v; }}

static std::vector<FieldDef> specs_fields() {
    return {
        FIELD_STR("Load Name", loadName),
        FIELD_STR("Caliber", caliber),
        FieldDef{"Ammo Type", true,
            [](const ReloadLoad& l) { return ammo_type_str(l.ammoType); },
            [](ReloadLoad& l, const std::string&) {
                l.ammoType = (l.ammoType == AmmoType::CARTRIDGE) ? AmmoType::SHOTSHELL : AmmoType::CARTRIDGE;
            }},
        FieldDef{"Units", true,
            [](const ReloadLoad& l) { return unit_str(l.unitOfMeasure); },
            [](ReloadLoad& l, const std::string&) {
                l.unitOfMeasure = (l.unitOfMeasure == UnitOfMeasure::IMPERIAL) ? UnitOfMeasure::METRIC : UnitOfMeasure::IMPERIAL;
            }},
        FIELD_STR("Powder (gr/g)", powderGrains),
        FIELD_STR("COAL", overallLength),
        FIELD_STR("Case Trim", caseTrimLength),
        FIELD_STR("Bullet Desc", bulletDescription),
        FIELD_STR("Bullet Wt(gr)", bulletWeightGrains),
        FIELD_STR("Bullet Style", bulletStyle),
        FIELD_STR("Powder Desc", powderDescription),
        FIELD_STR("Primer Desc", primerDescription),
        FIELD_STR("Case Desc", caseDescription),
    };
}

static std::vector<FieldDef> cost_fields() {
    return {
        FIELD_STR("Bullet Qty", bulletQuantity),
        FIELD_STR("Bullet Cost$", bulletCost),
        FIELD_STR("Powder Bulk", powderWeightBulk),
        FIELD_STR("Powder Cost$", powderCost),
        FIELD_STR("Primer Qty", primerQuantity),
        FIELD_STR("Primer Cost$", primerCost),
        FIELD_STR("Case Qty", caseQuantity),
        FIELD_STR("Case Cost$", caseCost),
        FIELD_STR("Case Uses", caseTimesUsed),
        FIELD_STR("Retail Rds", retailRoundCount),
        FIELD_STR("Retail Cost$", retailCost),
    };
}

static std::vector<FieldDef> ballistic_fields() {
    return {
        FIELD_STR("Velocity", velocity),
        FieldDef{"Vel Unit", true,
            [](const ReloadLoad& l) { return l.shortVelocityUnit(); },
            [](ReloadLoad& l, const std::string&) {
                l.velocityUnit = (l.shortVelocityUnit() == "fps") ? "Meters/Second (m/s)" : "Feet/Second (fps)";
            }},
        FIELD_STR("SD", standardDeviation),
        FIELD_STR("ES", extremeSpread),
        FIELD_STR("BC", ballisticCoefficient),
        FieldDef{"BC Type", true,
            [](const ReloadLoad& l) { return l.ballisticCoefficientType; },
            [](ReloadLoad& l, const std::string&) {
                l.ballisticCoefficientType = (l.ballisticCoefficientType == "G1") ? "G7" : "G1";
            }},
        FIELD_STR("Bullet Len", bulletLength),
        FIELD_STR("Group Size", groupSize),
        FIELD_STR("Shot Count", shotCount),
        FIELD_STR("Target Dist", targetDistance),
    };
}

int main(int argc, char* argv[]) {
    (void)argc; (void)argv;

    // Prefer /dev/fb_lcd directly on CardputerZero (even if WAYLAND_DISPLAY/DISPLAY is set)
    bool use_fb = false;
    FbDisplay fb;
    EvdevKeyboard evdev;

    if (fb.open_device()) {
        use_fb = true;
        evdev.open_all();
    }

    SDL_Window* window = nullptr;
    SDL_Renderer* renderer = nullptr;
    SDL_Texture* fb_tex = nullptr;

    if (use_fb) {
        if (SDL_Init(SDL_INIT_TIMER) != 0) {
            std::fprintf(stderr, "SDL_Init(TIMER) failed: %s\n", SDL_GetError());
            fb.close();
            return 1;
        }
    } else {
        if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_TIMER) != 0) {
            if (fb.open_device()) {
                use_fb = true;
                evdev.open_all();
                SDL_Init(SDL_INIT_TIMER);
            } else {
                std::fprintf(stderr, "SDL_Init failed: %s\n", SDL_GetError());
                return 1;
            }
        }
        if (!use_fb) {
            window = SDL_CreateWindow("Means Arms Ammo", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
                                      LOG_W * 3, LOG_H * 3, SDL_WINDOW_SHOWN | SDL_WINDOW_RESIZABLE);
            renderer = SDL_CreateRenderer(window, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
            if (!renderer) renderer = SDL_CreateRenderer(window, -1, SDL_RENDERER_SOFTWARE);
            SDL_RenderSetLogicalSize(renderer, LOG_W, LOG_H);
            fb_tex = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STREAMING, LOG_W, LOG_H);
        }
    }

    if (TTF_Init() != 0) {
        std::fprintf(stderr, "TTF_Init failed: %s\n", TTF_GetError());
        if (use_fb) fb.close();
        SDL_Quit();
        return 1;
    }

    std::string reg_path = find_font(false);
    std::string bold_path = find_font(true);
    if (bold_path.empty()) bold_path = reg_path;
    TTF_Font* font_sm = TTF_OpenFont(reg_path.c_str(), 10);
    TTF_Font* font_md = TTF_OpenFont(reg_path.c_str(), 11);
    TTF_Font* font_bd = TTF_OpenFont(bold_path.c_str(), 11);
    if (!font_sm || !font_md || !font_bd) {
        std::fprintf(stderr, "TTF_OpenFont failed (%s): %s\n", reg_path.c_str(), TTF_GetError());
        return 1;
    }

    std::vector<ReloadLoad> loads = Storage::load_all();
    Screen screen = Screen::LIST;
    LoadSortOrder sort_order = LoadSortOrder::BULLET_WEIGHT;
    int caliber_tab = 0;
    int list_idx = 0;
    int list_scroll = 0;

    bool searching = false;
    std::string search_query;

    std::string active_load_id;
    int detail_tab = 0;
    int field_idx = 0;
    int field_scroll = 0;

    bool editing = false;
    std::string edit_buf;

    bool confirm_delete = false;
    std::string status_msg;
    uint32_t status_until = 0;

    std::vector<std::string> import_files;
    int import_idx = 0;

    uint32_t sdl_esc_down_at = 0;

    auto set_status = [&](const std::string& msg) {
        status_msg = msg;
        status_until = SDL_GetTicks() + 2500;
    };

    auto get_calibers = [&]() -> std::vector<std::string> {
        std::vector<std::string> cals;
        for (const auto& l : loads) {
            std::string c = l.caliber.empty() ? "No Caliber" : l.caliber;
            if (std::find(cals.begin(), cals.end(), c) == cals.end()) {
                cals.push_back(c);
            }
        }
        std::sort(cals.begin(), cals.end(), [&](const std::string& a, const std::string& b) {
            return to_lower(a) < to_lower(b);
        });
        return cals;
    };

    auto filtered_indices = [&]() -> std::vector<int> {
        auto cals = get_calibers();
        std::string active_cal;
        if (!cals.empty()) {
            if (caliber_tab >= static_cast<int>(cals.size())) caliber_tab = static_cast<int>(cals.size()) - 1;
            if (caliber_tab < 0) caliber_tab = 0;
            active_cal = cals[caliber_tab];
        }
        std::vector<int> idxs;
        std::string q = to_lower(search_query);
        for (int i = 0; i < static_cast<int>(loads.size()); ++i) {
            const auto& l = loads[i];
            std::string c = l.caliber.empty() ? "No Caliber" : l.caliber;
            if (!active_cal.empty() && c != active_cal) continue;
            if (q.empty()) {
                idxs.push_back(i);
            } else {
                if (to_lower(l.loadName).find(q) != std::string::npos ||
                    to_lower(l.caliber).find(q) != std::string::npos ||
                    to_lower(l.powderDescription).find(q) != std::string::npos ||
                    to_lower(l.bulletDescription).find(q) != std::string::npos) {
                    idxs.push_back(i);
                }
            }
        }
        std::sort(idxs.begin(), idxs.end(), [&](int a, int b) {
            const auto& la = loads[a];
            const auto& lb = loads[b];
            switch (sort_order) {
                case LoadSortOrder::NAME: return to_lower(la.loadName) < to_lower(lb.loadName);
                case LoadSortOrder::CALIBER: return to_lower(la.caliber) < to_lower(lb.caliber);
                case LoadSortOrder::COST:
                    return ReloadCalculator::calculateComponentCosts(la).totalCostPerRound <
                           ReloadCalculator::calculateComponentCosts(lb).totalCostPerRound;
                case LoadSortOrder::BULLET_WEIGHT: {
                    double wa = la.bulletWeightGrainsVal();
                    double wb = lb.bulletWeightGrainsVal();
                    if (wa <= 0.0 && wb > 0.0) return false;
                    if (wb <= 0.0 && wa > 0.0) return true;
                    if (wa != wb) return wa < wb;
                    return to_lower(la.loadName) < to_lower(lb.loadName);
                }
                case LoadSortOrder::UPDATED:
                default:
                    return la.updatedAt > lb.updatedAt;
            }
        });
        return idxs;
    };

    auto find_load_ptr = [&](const std::string& id) -> ReloadLoad* {
        for (auto& l : loads) {
            if (l.id == id) return &l;
        }
        return nullptr;
    };

    auto f_specs = specs_fields();
    auto f_costs = cost_fields();
    auto f_bal = ballistic_fields();

    Canvas cv;
    bool running = true;

    while (running) {
        uint32_t now = SDL_GetTicks();
        std::vector<KeyEvent> events;

        if (use_fb) {
            events = evdev.poll(now, !editing && !searching);
            if (evdev.esc_held && evdev.esc_down_ms > 0 && (now - evdev.esc_down_ms) >= 1000) {
                running = false;
                break;
            }
        } else {
            SDL_Event ev;
            while (SDL_PollEvent(&ev)) {
                if (ev.type == SDL_QUIT) { running = false; break; }
                if (ev.type == SDL_KEYUP && ev.key.keysym.sym == SDLK_ESCAPE) sdl_esc_down_at = 0;
                if (ev.type == SDL_TEXTINPUT && ev.text.text[0]) {
                    KeyEvent ke;
                    ke.type = KeyEvent::CHAR;
                    ke.ch = ev.text.text[0];
                    events.push_back(ke);
                }
                if (ev.type == SDL_KEYDOWN) {
                    SDL_Keycode sym = ev.key.keysym.sym;
                    if (sym == SDLK_ESCAPE && ev.key.repeat == 0) sdl_esc_down_at = now;
                    KeyEvent ke;
                    ke.is_repeat = (ev.key.repeat != 0);
                    if (sym == SDLK_UP) ke.type = KeyEvent::UP;
                    else if (sym == SDLK_DOWN) ke.type = KeyEvent::DOWN;
                    else if (sym == SDLK_LEFT) ke.type = KeyEvent::LEFT;
                    else if (sym == SDLK_RIGHT) ke.type = KeyEvent::RIGHT;
                    else if (sym == SDLK_RETURN || sym == SDLK_KP_ENTER) ke.type = KeyEvent::ENTER;
                    else if (sym == SDLK_ESCAPE) ke.type = KeyEvent::ESC;
                    else if (sym == SDLK_BACKSPACE) ke.type = KeyEvent::BACKSPACE;
                    else if (sym == SDLK_TAB) ke.type = KeyEvent::TAB;
                    else if (sym == SDLK_DELETE) ke.type = KeyEvent::DEL;
                    else if (!editing && !(screen == Screen::LIST && searching) && sym >= 32 && sym < 127) {
                        ke.type = KeyEvent::CHAR;
                        ke.ch = static_cast<char>(sym);
                    }
                    if (ke.type != KeyEvent::NONE) events.push_back(ke);
                }
            }
            if (sdl_esc_down_at > 0 && (now - sdl_esc_down_at) >= 1000) {
                running = false;
                break;
            }
        }

        for (const auto& ke : events) {
            if (editing) {
                if (ke.type == KeyEvent::ENTER) {
                    if (auto* l = find_load_ptr(active_load_id)) {
                        if (detail_tab == 0 && field_idx < static_cast<int>(f_specs.size())) {
                            f_specs[field_idx].setter(*l, edit_buf);
                        } else if (detail_tab == 1 && field_idx >= 6 && field_idx - 6 < static_cast<int>(f_costs.size())) {
                            f_costs[field_idx - 6].setter(*l, edit_buf);
                        } else if (detail_tab == 2 && field_idx < static_cast<int>(f_bal.size())) {
                            f_bal[field_idx].setter(*l, edit_buf);
                        } else if (detail_tab == 3) {
                            l->notes = edit_buf;
                        }
                        l->updatedAt = now_ms();
                        Storage::save_all(loads);
                        set_status("Saved");
                    }
                    editing = false;
                    if (!use_fb) SDL_StopTextInput();
                } else if (ke.type == KeyEvent::ESC) {
                    editing = false;
                    if (!use_fb) SDL_StopTextInput();
                } else if (ke.type == KeyEvent::BACKSPACE) {
                    if (!edit_buf.empty()) edit_buf.pop_back();
                } else if (ke.type == KeyEvent::CHAR && ke.ch >= 32) {
                    edit_buf.push_back(ke.ch);
                }
                continue;
            }

            if (screen == Screen::LIST) {
                if (confirm_delete) {
                    if (ke.type == KeyEvent::ENTER || (ke.type == KeyEvent::CHAR && (ke.ch == 'y' || ke.ch == 'Y'))) {
                        auto idxs = filtered_indices();
                        if (list_idx >= 0 && list_idx < static_cast<int>(idxs.size())) {
                            loads.erase(loads.begin() + idxs[list_idx]);
                            Storage::save_all(loads);
                            if (list_idx > 0 && list_idx >= static_cast<int>(loads.size())) --list_idx;
                            set_status("Deleted load");
                        }
                    }
                    confirm_delete = false;
                    continue;
                }

                if (searching) {
                    if (ke.type == KeyEvent::ESC) {
                        searching = false;
                        search_query.clear();
                        if (!use_fb) SDL_StopTextInput();
                    } else if (ke.type == KeyEvent::ENTER) {
                        searching = false;
                        if (!use_fb) SDL_StopTextInput();
                    } else if (ke.type == KeyEvent::BACKSPACE) {
                        if (!search_query.empty()) {
                            search_query.pop_back();
                            list_idx = 0;
                            list_scroll = 0;
                        }
                    } else if (ke.type == KeyEvent::CHAR && ke.ch >= 32) {
                        search_query.push_back(ke.ch);
                        list_idx = 0;
                        list_scroll = 0;
                    }
                    continue;
                }

                auto idxs = filtered_indices();
                int n = static_cast<int>(idxs.size());
                char lc = (ke.type == KeyEvent::CHAR) ? static_cast<char>(std::tolower(static_cast<unsigned char>(ke.ch))) : 0;

                if (ke.type == KeyEvent::DOWN || lc == 'j') {
                    if (list_idx + 1 < n) ++list_idx;
                } else if (ke.type == KeyEvent::UP || lc == 'k') {
                    if (list_idx > 0) --list_idx;
                } else if (ke.type == KeyEvent::LEFT) {
                    auto cals = get_calibers();
                    if (!cals.empty()) {
                        caliber_tab = (caliber_tab + static_cast<int>(cals.size()) - 1) % static_cast<int>(cals.size());
                        list_idx = 0;
                        list_scroll = 0;
                    }
                } else if (ke.type == KeyEvent::RIGHT || ke.type == KeyEvent::TAB) {
                    auto cals = get_calibers();
                    if (!cals.empty()) {
                        caliber_tab = (caliber_tab + 1) % static_cast<int>(cals.size());
                        list_idx = 0;
                        list_scroll = 0;
                    }
                } else if (ke.type == KeyEvent::ENTER) {
                    if (n > 0 && list_idx < n) {
                        active_load_id = loads[idxs[list_idx]].id;
                        screen = Screen::DETAIL;
                        detail_tab = 0;
                        field_idx = 0;
                        field_scroll = 0;
                    }
                } else if (lc == 'n') {
                    auto cals = get_calibers();
                    ReloadLoad nl;
                    nl.loadName = "New Load";
                    if (!cals.empty() && caliber_tab < static_cast<int>(cals.size()) && cals[caliber_tab] != "No Caliber") {
                        nl.caliber = cals[caliber_tab];
                    }
                    loads.insert(loads.begin(), nl);
                    Storage::save_all(loads);
                    active_load_id = nl.id;
                    screen = Screen::DETAIL;
                    detail_tab = 0;
                    field_idx = 0;
                    field_scroll = 0;
                    set_status("Created new load");
                } else if (lc == 'd') {
                    if (n > 0 && list_idx < n) {
                        ReloadLoad copy = loads[idxs[list_idx]];
                        copy.id = make_uuid();
                        copy.createdAt = now_ms();
                        copy.updatedAt = copy.createdAt;
                        copy.loadName += " (Copy)";
                        loads.insert(loads.begin(), copy);
                        Storage::save_all(loads);
                        list_idx = 0;
                        list_scroll = 0;
                        set_status("Duplicated load");
                    }
                } else if (ke.type == KeyEvent::DEL || ke.type == KeyEvent::BACKSPACE) {
                    if (n > 0) confirm_delete = true;
                } else if (lc == '/') {
                    searching = true;
                    search_query.clear();
                    if (!use_fb) SDL_StartTextInput();
                } else if (lc == 's') {
                    sort_order = (sort_order == LoadSortOrder::BULLET_WEIGHT)
                                     ? LoadSortOrder::NAME
                                     : LoadSortOrder::BULLET_WEIGHT;
                    list_idx = 0;
                    list_scroll = 0;
                } else if (lc == 'e') {
                    std::string ep = Storage::default_export_path();
                    if (Storage::export_json(loads, ep)) set_status("Exported ~/means_arms_backup.json");
                    else set_status("Export failed");
                } else if (lc == 'i') {
                    import_files = Storage::find_backup_candidates();
                    import_idx = 0;
                    screen = Screen::IMPORT;
                } else if (lc == 'q') {
                    running = false;
                }
            } else if (screen == Screen::IMPORT) {
                char lc = (ke.type == KeyEvent::CHAR) ? static_cast<char>(std::tolower(static_cast<unsigned char>(ke.ch))) : 0;
                if (ke.type == KeyEvent::ESC) {
                    screen = Screen::LIST;
                } else if (ke.type == KeyEvent::DOWN || lc == 'j') {
                    if (import_idx + 1 < static_cast<int>(import_files.size())) ++import_idx;
                } else if (ke.type == KeyEvent::UP || lc == 'k') {
                    if (import_idx > 0) --import_idx;
                } else if (ke.type == KeyEvent::ENTER) {
                    if (!import_files.empty() && import_idx < static_cast<int>(import_files.size())) {
                        std::string err;
                        auto imported = Storage::import_json_file(import_files[import_idx], err);
                        if (!err.empty() || imported.empty()) {
                            set_status(err.empty() ? "No loads in file" : err);
                        } else {
                            int added = 0;
                            for (auto& imp : imported) {
                                if (auto* existing = find_load_ptr(imp.id)) {
                                    *existing = imp;
                                } else {
                                    loads.push_back(imp);
                                    ++added;
                                }
                            }
                            Storage::save_all(loads);
                            set_status("Imported " + std::to_string(imported.size()) + " loads (" + std::to_string(added) + " new)");
                            screen = Screen::LIST;
                        }
                    }
                }
            } else if (screen == Screen::DETAIL) {
                auto* l = find_load_ptr(active_load_id);
                if (!l) { screen = Screen::LIST; continue; }

                int max_items = 0;
                if (detail_tab == 0) max_items = static_cast<int>(f_specs.size());
                else if (detail_tab == 1) max_items = 6 + static_cast<int>(f_costs.size());
                else if (detail_tab == 2) max_items = static_cast<int>(f_bal.size()) + 5;
                else max_items = 1;

                char lc = (ke.type == KeyEvent::CHAR) ? static_cast<char>(std::tolower(static_cast<unsigned char>(ke.ch))) : 0;

                if (ke.type == KeyEvent::ESC) {
                    screen = Screen::LIST;
                } else if (ke.type == KeyEvent::LEFT || ke.type == KeyEvent::RIGHT || ke.type == KeyEvent::TAB ||
                           lc == '1' || lc == '2' || lc == '3' || lc == '4') {
                    if (lc == '1') detail_tab = 0;
                    else if (lc == '2') detail_tab = 1;
                    else if (lc == '3') detail_tab = 2;
                    else if (lc == '4') detail_tab = 3;
                    else if (ke.type == KeyEvent::LEFT) detail_tab = (detail_tab + 3) % 4;
                    else detail_tab = (detail_tab + 1) % 4;
                    field_idx = 0;
                    field_scroll = 0;
                } else if (ke.type == KeyEvent::DOWN || lc == 'j') {
                    if (field_idx + 1 < max_items) ++field_idx;
                } else if (ke.type == KeyEvent::UP || lc == 'k') {
                    if (field_idx > 0) --field_idx;
                } else if (ke.type == KeyEvent::ENTER) {
                    if (detail_tab == 0 && field_idx < static_cast<int>(f_specs.size())) {
                        if (f_specs[field_idx].is_toggle) {
                            f_specs[field_idx].setter(*l, "");
                            l->updatedAt = now_ms();
                            Storage::save_all(loads);
                        } else {
                            editing = true;
                            edit_buf = f_specs[field_idx].getter(*l);
                            if (!use_fb) SDL_StartTextInput();
                        }
                    } else if (detail_tab == 1 && field_idx >= 6 && field_idx - 6 < static_cast<int>(f_costs.size())) {
                        editing = true;
                        edit_buf = f_costs[field_idx - 6].getter(*l);
                        if (!use_fb) SDL_StartTextInput();
                    } else if (detail_tab == 2 && field_idx < static_cast<int>(f_bal.size())) {
                        if (f_bal[field_idx].is_toggle) {
                            f_bal[field_idx].setter(*l, "");
                            l->updatedAt = now_ms();
                            Storage::save_all(loads);
                        } else {
                            editing = true;
                            edit_buf = f_bal[field_idx].getter(*l);
                            if (!use_fb) SDL_StartTextInput();
                        }
                    } else if (detail_tab == 3) {
                        editing = true;
                        edit_buf = l->notes;
                        if (!use_fb) SDL_StartTextInput();
                    }
                }
            }
        }

        // ---- RENDER (320x170) ----
        cv.fill_rect(0, 0, LOG_W, LOG_H, COL_BG);

        if (screen == Screen::LIST) {
            cv.fill_rect(0, 0, LOG_W, 17, COL_TOPBAR);
            draw_text(cv, font_bd, 5, 2, "MEANS ARMS", COL_BLUE);
            auto cals = get_calibers();
            auto idxs = filtered_indices();
            draw_text(cv, font_sm, 92, 3, "(" + std::to_string(idxs.size()) + "/" + std::to_string(loads.size()) + ")", COL_TEXT_SEC);

            const char* sort_lbl = (sort_order == LoadSortOrder::NAME) ? "S:Name" : "S:Wt";
            int sw = text_width(font_sm, sort_lbl);
            draw_text(cv, font_sm, LOG_W - sw - 5, 3, sort_lbl, COL_BLUE);

            // Caliber tab strip (y=18..33)
            cv.fill_rect(0, 17, LOG_W, 16, COL_TOPBAR);
            if (!cals.empty()) {
                // Keep active tab centered/visible
                int tab_start = 0;
                int total_w = 0;
                for (int t = caliber_tab; t >= 0; --t) {
                    int tw = text_width(font_sm, cals[t]) + 10;
                    if (t < caliber_tab && total_w + tw > LOG_W - 20) break;
                    total_w += tw + 2;
                    tab_start = t;
                }
                int tx = (tab_start > 0) ? 10 : 4;
                if (tab_start > 0) draw_text(cv, font_sm, 2, 19, "<", COL_BLUE);
                for (int t = tab_start; t < static_cast<int>(cals.size()); ++t) {
                    std::string lbl = truncate_to_width(font_sm, cals[t], 110);
                    int tw = text_width(font_sm, lbl) + 8;
                    if (tx + tw > LOG_W - 10) {
                        draw_text(cv, font_sm, LOG_W - 8, 19, ">", COL_BLUE);
                        break;
                    }
                    bool active = (t == caliber_tab);
                    cv.fill_rect(tx, 18, tw, 13, active ? COL_BLUE : COL_CARD);
                    if (!active) cv.draw_rect(tx, 18, tw, 13, COL_BORDER);
                    draw_text(cv, active ? font_bd : font_sm, tx + 4, 19, lbl, active ? COL_BG : COL_TEXT_SEC);
                    tx += tw + 2;
                }
            }

            int y_start = 35;
            if (searching) {
                cv.fill_rect(4, 35, LOG_W - 8, 15, COL_ELEVATED);
                cv.draw_rect(4, 35, LOG_W - 8, 15, COL_BLUE);
                draw_text(cv, font_sm, 8, 37, "Find: " + search_query + "_", COL_TEXT, LOG_W - 16);
                y_start = 52;
            }

            int row_h = 26;
            int vis_rows = (154 - y_start) / row_h;
            if (list_idx < list_scroll) list_scroll = list_idx;
            if (list_idx >= list_scroll + vis_rows) list_scroll = list_idx - vis_rows + 1;

            if (idxs.empty()) {
                draw_text(cv, font_md, 16, 68, "No loads found. Press N for new load.", COL_TEXT_SEC);
            } else {
                for (int i = 0; i < vis_rows && (list_scroll + i) < static_cast<int>(idxs.size()); ++i) {
                    int idx = idxs[list_scroll + i];
                    const auto& l = loads[idx];
                    bool sel = (list_scroll + i == list_idx);
                    int ry = y_start + i * row_h;

                    cv.fill_rect(4, ry, LOG_W - 8, row_h - 2, sel ? COL_ELEVATED : COL_CARD);
                    cv.draw_rect(4, ry, LOG_W - 8, row_h - 2, sel ? COL_BLUE : COL_BORDER);

                    std::string vdisp = l.velocityDisplay();
                    int vw = vdisp.empty() ? 0 : text_width(font_sm, vdisp) + 8;
                    if (!vdisp.empty()) {
                        cv.fill_rect(LOG_W - vw - 8, ry + 2, vw, 11, COL_TOPBAR);
                        draw_text(cv, font_sm, LOG_W - vw - 4, ry + 2, vdisp, COL_BLUE);
                    }
                    std::string title = l.loadName.empty() ? "(Unnamed)" : l.loadName;
                    draw_text(cv, font_bd, 9, ry + 1, title, COL_TEXT, LOG_W - vw - 20);

                    auto costs = ReloadCalculator::calculateComponentCosts(l);
                    std::string sub;
                    if (!l.bulletWeightGrains.empty()) {
                        sub = l.bulletWeightGrains + "gr";
                    }
                    if (!l.powderGrains.empty()) {
                        if (!sub.empty()) sub += " | ";
                        sub += l.powderGrains + (l.unitOfMeasure == UnitOfMeasure::METRIC ? "g" : "gr");
                    }
                    if (!l.powderDescription.empty()) {
                        if (!sub.empty()) sub += " ";
                        sub += l.powderDescription;
                    }
                    if (sub.empty()) {
                        sub = l.caliber.empty() ? "No specs" : l.caliber;
                    }
                    std::string cost_s = costs.totalCostPerRound > 0.0 ? (fmt_money(costs.totalCostPerRound) + "/rd") : "";
                    Color ccol = (costs.savingsPerRound > 0.0) ? COL_GREEN : COL_TEXT_SEC;
                    int cw = cost_s.empty() ? 0 : text_width(font_sm, cost_s);
                    if (!cost_s.empty()) {
                        draw_text(cv, font_sm, LOG_W - cw - 10, ry + 13, cost_s, ccol);
                    }
                    draw_text(cv, font_sm, 9, ry + 13, sub, COL_TEXT_SEC, LOG_W - cw - 24);
                }
            }

            if (confirm_delete) {
                cv.fill_rect(30, 56, 260, 52, COL_TOPBAR);
                cv.draw_rect(30, 56, 260, 52, COL_AMBER);
                draw_text(cv, font_bd, 44, 66, "Delete selected load?", COL_AMBER);
                draw_text(cv, font_sm, 44, 86, "Press Y / Enter to confirm, Esc cancel", COL_TEXT);
            }

            cv.fill_rect(0, 155, LOG_W, 15, COL_TOPBAR);
            if (!status_msg.empty() && now < status_until) {
                draw_text(cv, font_sm, 5, 157, status_msg, COL_GREEN, LOG_W - 10);
            } else {
                draw_text(cv, font_sm, 5, 157, "Z/C:Cal F/X:Sel Ent:Open N:New /:Find Q:Quit", COL_TEXT_MUT, LOG_W - 10);
            }
        } else if (screen == Screen::IMPORT) {
            cv.fill_rect(0, 0, LOG_W, 18, COL_TOPBAR);
            draw_text(cv, font_bd, 5, 2, "IMPORT BACKUP JSON", COL_BLUE);
            if (import_files.empty()) {
                draw_text(cv, font_md, 10, 42, "No .json files found in:", COL_TEXT);
                draw_text(cv, font_sm, 10, 60, "~/, ~/Downloads, /boot/firmware", COL_TEXT_SEC);
                draw_text(cv, font_sm, 10, 82, "Copy your MeansArmsAmmo backup via scp.", COL_TEXT_MUT);
            } else {
                for (int i = 0; i < 5 && i < static_cast<int>(import_files.size()); ++i) {
                    int ry = 22 + i * 24;
                    bool sel = (i == import_idx);
                    cv.fill_rect(4, ry, LOG_W - 8, 22, sel ? COL_ELEVATED : COL_CARD);
                    cv.draw_rect(4, ry, LOG_W - 8, 22, sel ? COL_BLUE : COL_BORDER);
                    draw_text(cv, font_sm, 9, ry + 5, import_files[i], COL_TEXT, LOG_W - 18);
                }
            }
            cv.fill_rect(0, 155, LOG_W, 15, COL_TOPBAR);
            draw_text(cv, font_sm, 5, 157, "Enter:Import & Merge   Esc:Back", COL_TEXT_MUT);
        } else if (screen == Screen::DETAIL) {
            auto* l = find_load_ptr(active_load_id);
            if (!l) { screen = Screen::LIST; continue; }

            cv.fill_rect(0, 0, LOG_W, 18, COL_TOPBAR);
            draw_text(cv, font_bd, 4, 2, l->loadName.empty() ? "Load" : l->loadName, COL_TEXT, 118);

            const char* tabs[4] = {"1:Spc", "2:Cst", "3:Bal", "4:Not"};
            int tx = 126;
            for (int t = 0; t < 4; ++t) {
                bool active = (t == detail_tab);
                cv.fill_rect(tx, 2, 46, 14, active ? COL_BLUE : COL_CARD);
                draw_text(cv, font_sm, tx + 4, 3, tabs[t], active ? COL_BG : COL_TEXT_SEC);
                tx += 48;
            }

            if (detail_tab == 0) {
                int row_h = 20;
                int vis = 6;
                if (field_idx < field_scroll) field_scroll = field_idx;
                if (field_idx >= field_scroll + vis) field_scroll = field_idx - vis + 1;
                for (int i = 0; i < vis && (field_scroll + i) < static_cast<int>(f_specs.size()); ++i) {
                    int fi = field_scroll + i;
                    bool sel = (fi == field_idx);
                    int ry = 22 + i * (row_h + 2);
                    cv.fill_rect(4, ry, LOG_W - 8, row_h, sel ? COL_ELEVATED : COL_CARD);
                    cv.draw_rect(4, ry, LOG_W - 8, row_h, sel ? COL_BLUE : COL_BORDER);
                    draw_text(cv, font_sm, 8, ry + 4, f_specs[fi].label, COL_TEXT_SEC, 95);
                    std::string val = (sel && editing) ? (edit_buf + "_") : f_specs[fi].getter(*l);
                    if (val.empty()) val = "-";
                    draw_text(cv, font_md, 108, ry + 3, val, f_specs[fi].is_toggle ? COL_BLUE : COL_TEXT, LOG_W - 116);
                }
            } else if (detail_tab == 1) {
                auto c = ReloadCalculator::calculateComponentCosts(*l);
                auto slices = ReloadCalculator::calculatePieChartSlices(c);
                auto savings = ReloadCalculator::calculateSavingsTable(*l, {20, 50, 100, 500, 1000});

                cv.fill_rect(4, 20, LOG_W - 8, 36, COL_CARD);
                cv.draw_rect(4, 20, LOG_W - 8, 36, COL_BORDER);
                draw_text(cv, font_bd, 8, 22, "Reload: " + fmt_money(c.totalCostPerRound, 3) + "/rd", COL_BLUE);
                draw_text(cv, font_sm, 140, 23, "Retail: " + fmt_money(c.retailCostPerRound), COL_TEXT_SEC);
                draw_text(cv, font_bd, 232, 22, "Save " + fmt_num(c.savingsPercent, 0) + "%", c.savingsPerRound > 0 ? COL_GREEN : COL_TEXT_MUT);

                int bx = 8, bw = LOG_W - 16;
                cv.fill_rect(bx, 38, bw, 6, COL_TOPBAR);
                if (!slices.empty()) {
                    int cx = bx;
                    for (size_t si = 0; si < slices.size(); ++si) {
                        int sw = (si + 1 == slices.size()) ? (bx + bw - cx) : static_cast<int>((slices[si].percentage / 100.0) * bw);
                        cv.fill_rect(cx, 38, sw, 6, hex_color(slices[si].colorHex));
                        cx += sw;
                    }
                }
                std::string leg = "B:" + fmt_money(c.bulletCostPerRound) + " P:" + fmt_money(c.powderCostPerRound) +
                                  " Pr:" + fmt_money(c.primerCostPerRound) + " C:" + fmt_money(c.caseCostPerRound);
                draw_text(cv, font_sm, 8, 45, leg, COL_TEXT_SEC, LOG_W - 16);

                int total_rows = 6 + static_cast<int>(f_costs.size());
                int vis = 5;
                if (field_idx < field_scroll) field_scroll = field_idx;
                if (field_idx >= field_scroll + vis) field_scroll = field_idx - vis + 1;
                for (int i = 0; i < vis && (field_scroll + i) < total_rows; ++i) {
                    int ri = field_scroll + i;
                    bool sel = (ri == field_idx);
                    int ry = 59 + i * 19;
                    cv.fill_rect(4, ry, LOG_W - 8, 17, sel ? COL_ELEVATED : COL_CARD);
                    cv.draw_rect(4, ry, LOG_W - 8, 17, sel ? COL_BLUE : COL_BORDER);
                    if (ri == 0) {
                        std::string unit_lbl = (l->unitOfMeasure == UnitOfMeasure::METRIC) ? "Loads/kg:" : "Loads/lb:";
                        draw_text(cv, font_sm, 8, ry + 3, unit_lbl, COL_TEXT_SEC);
                        draw_text(cv, font_sm, 80, ry + 3, fmt_num(c.powderLoadsPerBulkUnit, 0) + " rds", COL_TEXT);
                    } else if (ri >= 1 && ri <= 5) {
                        const auto& sr = savings[ri - 1];
                        std::string row_s = std::to_string(sr.quantity) + " rds: " + fmt_money(sr.costToReload) +
                                            " vs " + fmt_money(sr.retailCost) + " (Save " + fmt_money(sr.savings) + ")";
                        draw_text(cv, font_sm, 8, ry + 3, row_s, sr.savings > 0 ? COL_GREEN : COL_TEXT_SEC, LOG_W - 16);
                    } else {
                        int fi = ri - 6;
                        draw_text(cv, font_sm, 8, ry + 3, f_costs[fi].label, COL_TEXT_SEC, 95);
                        std::string val = (sel && editing) ? (edit_buf + "_") : f_costs[fi].getter(*l);
                        draw_text(cv, font_sm, 108, ry + 3, val.empty() ? "-" : val, COL_TEXT, LOG_W - 116);
                    }
                }
            } else if (detail_tab == 2) {
                double mv = ReloadCalculator::velocityInFps(*l);
                double me = ReloadCalculator::muzzleEnergyFtLbs(l->bulletWeightGrainsVal(), mv);
                auto drops = ReloadCalculator::calculateDropTable(*l, 100.0, 1.5, {100, 200, 300, 400, 500});

                cv.fill_rect(4, 20, LOG_W - 8, 20, COL_CARD);
                cv.draw_rect(4, 20, LOG_W - 8, 20, COL_BORDER);
                std::string hdr = "MV:" + (mv > 0 ? fmt_num(mv, 0) + "fps" : "-") +
                                  "  ME:" + (me > 0 ? fmt_num(me, 0) + "ft-lb" : "-") +
                                  "  SD:" + (l->standardDeviation.empty() ? "-" : l->standardDeviation);
                draw_text(cv, font_sm, 8, 24, hdr, COL_BLUE, LOG_W - 16);

                int total_rows = static_cast<int>(f_bal.size()) + 5;
                int vis = 6;
                if (field_idx < field_scroll) field_scroll = field_idx;
                if (field_idx >= field_scroll + vis) field_scroll = field_idx - vis + 1;
                for (int i = 0; i < vis && (field_scroll + i) < total_rows; ++i) {
                    int ri = field_scroll + i;
                    bool sel = (ri == field_idx);
                    int ry = 43 + i * 18;
                    cv.fill_rect(4, ry, LOG_W - 8, 16, sel ? COL_ELEVATED : COL_CARD);
                    cv.draw_rect(4, ry, LOG_W - 8, 16, sel ? COL_BLUE : COL_BORDER);
                    if (ri < static_cast<int>(f_bal.size())) {
                        draw_text(cv, font_sm, 8, ry + 2, f_bal[ri].label, COL_TEXT_SEC, 88);
                        std::string val = (sel && editing) ? (edit_buf + "_") : f_bal[ri].getter(*l);
                        draw_text(cv, font_sm, 100, ry + 2, val.empty() ? "-" : val,
                                  f_bal[ri].is_toggle ? COL_BLUE : COL_TEXT, LOG_W - 108);
                    } else {
                        int di = ri - static_cast<int>(f_bal.size());
                        if (di < static_cast<int>(drops.size())) {
                            const auto& d = drops[di];
                            char buf[96];
                            std::snprintf(buf, sizeof(buf), "%dyd: %.1f\" | %.1fMOA | %.1fMIL | %.0ffps",
                                d.rangeYards, d.dropInches, d.dropMoa, d.dropMil, d.velocityFps);
                            draw_text(cv, font_sm, 8, ry + 2, buf, COL_GREEN, LOG_W - 16);
                        } else {
                            draw_text(cv, font_sm, 8, ry + 2, "Enter Velocity & BC for drop table", COL_TEXT_MUT);
                        }
                    }
                }
            } else if (detail_tab == 3) {
                cv.fill_rect(4, 22, LOG_W - 8, 128, COL_CARD);
                cv.draw_rect(4, 22, LOG_W - 8, 128, editing ? COL_BLUE : COL_BORDER);
                std::string txt = editing ? (edit_buf + "_") : (l->notes.empty() ? "(No notes — press Enter to edit)" : l->notes);
                int y = 26;
                size_t pos = 0;
                while (pos < txt.size() && y < 140) {
                    size_t nl = txt.find('\n', pos);
                    std::string line = (nl == std::string::npos) ? txt.substr(pos) : txt.substr(pos, nl - pos);
                    while (line.size() > 44 && y < 140) {
                        draw_text(cv, font_sm, 8, y, line.substr(0, 44), COL_TEXT);
                        line = line.substr(44);
                        y += 13;
                    }
                    if (y < 140) {
                        draw_text(cv, font_sm, 8, y, line, l->notes.empty() && !editing ? COL_TEXT_MUT : COL_TEXT);
                        y += 13;
                    }
                    if (nl == std::string::npos) break;
                    pos = nl + 1;
                }
            }

            cv.fill_rect(0, 155, LOG_W, 15, COL_TOPBAR);
            if (!status_msg.empty() && now < status_until) {
                draw_text(cv, font_sm, 5, 157, status_msg, COL_GREEN, LOG_W - 10);
            } else if (editing) {
                draw_text(cv, font_sm, 5, 157, "Enter:Save   Esc:Cancel", COL_BLUE);
            } else {
                draw_text(cv, font_sm, 5, 157, "Up/Dn:Sel  Ent:Edit  L/R:Tab  Esc:Back", COL_TEXT_MUT);
            }
        }

        if (use_fb) {
            fb.present(cv);
        } else {
            SDL_UpdateTexture(fb_tex, nullptr, cv.pixels, LOG_W * sizeof(uint32_t));
            SDL_RenderClear(renderer);
            SDL_RenderCopy(renderer, fb_tex, nullptr, nullptr);
            SDL_RenderPresent(renderer);
        }
        SDL_Delay(16);
    }

    TTF_CloseFont(font_sm);
    TTF_CloseFont(font_md);
    TTF_CloseFont(font_bd);
    TTF_Quit();
    if (use_fb) {
        evdev.close_all();
        fb.close();
    } else {
        SDL_DestroyTexture(fb_tex);
        SDL_DestroyRenderer(renderer);
        SDL_DestroyWindow(window);
    }
    SDL_Quit();
    return 0;
}
