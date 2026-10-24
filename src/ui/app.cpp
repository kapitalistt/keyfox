#include "ui/app.hpp"

#include "core/generator.hpp"
#include "core/strength.hpp"
#include "core/totp.hpp"
#include "core/utf8.hpp"
#include "core/vault.hpp"
#include "platform/clipboard.hpp"
#include "platform/win.hpp"
#include "ui/anim.hpp"
#include "ui/art.hpp"
#include "ui/i18n.hpp"
#include "ui/theme.hpp"

#include <ftxui/component/component.hpp>
#include <ftxui/component/component_options.hpp>
#include <ftxui/component/event.hpp>
#include <ftxui/component/mouse.hpp>
#include <ftxui/component/screen_interactive.hpp>
#include <ftxui/dom/elements.hpp>
#include <ftxui/screen/box.hpp>
#include <ftxui/screen/terminal.hpp>

#include <algorithm>
#include <atomic>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <functional>
#include <future>
#include <map>
#include <memory>
#include <optional>
#include <thread>

namespace kf {

namespace {

using namespace ftxui;
using namespace std::chrono_literals;
namespace th = theme;
using anim::Clock;

constexpr int kMinWidth = 96;
constexpr int kMinHeight = 28;

const Event kCtrlS = Event::Special("\x13");
const Event kCtrlG = Event::Special("\x07");
const Event kCtrlF = Event::Special("\x06");

// ------------------------------------------------------------------ helpers

std::string repeat(const std::string& glyph, int n) {
    std::string s;
    for (int i = 0; i < n; ++i) s += glyph;
    return s;
}

Element txt(const std::string& s, th::Rgb c) { return text(s) | color(th::c(c)); }

Element spacer(int w) { return filler() | size(WIDTH, EQUAL, std::max(0, w)); }

Element keyhint(const std::string& key, const std::string& label) {
    return hbox({text(" " + key + " ") | bgcolor(th::c(th::amber)) | color(th::c(th::ink)) | bold,
                 text(" " + label + "  ") | color(th::c(th::dim))});
}

Element panel(const std::string& title, Element content, bool focused) {
    return window(text(" " + title + " ") | bold | color(th::c(th::amber)), std::move(content) | color(th::c(th::fg))) |
           color(th::c(focused ? th::orange : th::line));
}

// Coloured block bar: cells go red -> yellow -> green from left to right.
Element heatBar(double fraction, int width) {
    const int filled = static_cast<int>(std::lround(anim::clamp01(fraction) * width));
    Elements cells;
    for (int i = 0; i < width; ++i) {
        if (i < filled)
            cells.push_back(text("█") | color(th::c(th::heat(width > 1 ? double(i) / (width - 1) : 1.0))));
        else
            cells.push_back(text("░") | color(th::c(th::dim2)));
    }
    return hbox(std::move(cells));
}

// Indeterminate progress: a glowing comet bouncing back and forth.
Element sweepBar(double t, int width) {
    const double head = (0.5 - 0.5 * std::cos(t * 2.4)) * (width - 1);
    Elements cells;
    for (int i = 0; i < width; ++i) {
        const double a = std::max(0.0, 1.0 - std::abs(i - head) / 7.0);
        const th::Rgb c = a > 0 ? th::mix(th::track, th::mix(th::deep, th::amber, a), 0.35 + 0.65 * a) : th::track;
        cells.push_back(text("━") | color(th::c(c)));
    }
    return hbox(std::move(cells));
}

Element lineBar(double fraction, int width, th::Rgb c) {
    const int filled = static_cast<int>(std::lround(anim::clamp01(fraction) * width));
    return hbox({text(repeat("━", filled)) | color(th::c(c)), text(repeat("━", width - filled)) | color(th::c(th::track))});
}

std::string levelName(int lvl) {
    static const Tr* names[] = {&tr::lvl0, &tr::lvl1, &tr::lvl2, &tr::lvl3, &tr::lvl4};
    return T(*names[std::clamp(lvl, 0, 4)]);
}

th::Rgb levelColor(int lvl) {
    switch (std::clamp(lvl, 0, 4)) {
        case 0: return th::red;
        case 1: return th::mix(th::red, th::yellow, 0.5);
        case 2: return th::yellow;
        case 3: return th::mix(th::yellow, th::green, 0.6);
        default: return th::green;
    }
}

std::string crackTime(double bits) {
    const double s = crackSeconds(bits);
    const bool ru = currentLang() == Lang::Ru;
    auto num = [](double v) { return std::to_string(static_cast<long long>(std::llround(v))); };
    if (s < 1) return T(tr::instantly);
    if (s < 60) return "≈ " + num(s) + " " + T(tr::sec);
    if (s < 3600) return "≈ " + num(s / 60) + " " + T(tr::u_min);
    if (s < 86400) return "≈ " + num(s / 3600) + " " + T(tr::u_h);
    const double days = s / 86400;
    if (days < 365) return "≈ " + num(days) + " " + T(tr::u_days);
    const double years = days / 365.25;
    if (years < 1e6) {
        const auto n = static_cast<long long>(std::llround(years));
        return "≈ " + std::to_string(n) + " " + (ru ? ruPlural(n, "год", "года", "лет") : (n == 1 ? "year" : "years"));
    }
    return "≈ 10^" + std::to_string(static_cast<int>(std::floor(std::log10(years)))) + " " + (ru ? "лет" : "years");
}

std::string fmtDate(std::int64_t t) {
    if (t <= 0) return "—";
    const std::time_t tt = static_cast<std::time_t>(t);
    std::tm tm{};
    localtime_s(&tm, &tt);
    char buf[32];
    std::strftime(buf, sizeof buf, "%d.%m.%Y %H:%M", &tm);
    return buf;
}

std::string trim(const std::string& s) {
    const auto b = s.find_first_not_of(" \t\r\n");
    if (b == std::string::npos) return {};
    const auto e = s.find_last_not_of(" \t\r\n");
    return s.substr(b, e - b + 1);
}

std::vector<std::string> splitTags(const std::string& s) {
    std::vector<std::string> out;
    std::string cur;
    auto flush = [&] {
        std::string t = trim(cur);
        while (!t.empty() && t.front() == '#') t.erase(t.begin());
        if (!t.empty() && std::find(out.begin(), out.end(), t) == out.end()) out.push_back(t);
        cur.clear();
    };
    for (char c : s) {
        if (c == ' ' || c == ',' || c == ';') flush();
        else cur += c;
    }
    flush();
    return out;
}

std::string joinTags(const std::vector<std::string>& tags, const char* prefix) {
    std::string s;
    for (const auto& t : tags) s += (s.empty() ? "" : " ") + std::string(prefix) + t;
    return s;
}

std::string groupCode(const std::string& code) {
    if (code.size() < 6) return code;
    const std::size_t half = code.size() / 2;
    return code.substr(0, half) + " " + code.substr(half);
}

// Hotkeys work in both the Latin and the Russian (ЙЦУКЕН) layout.
struct RuKey {
    char latin;
    const char* lower;
    const char* upper;
};
constexpr RuKey kRuKeys[] = {
    {'n', "т", "Т"}, {'e', "у", "У"}, {'d', "в", "В"}, {'c', "с", "С"}, {'u', "г", "Г"}, {'t', "е", "Е"},
    {'s', "ы", "Ы"}, {'f', "а", "А"}, {'o', "щ", "Щ"}, {'g', "п", "П"}, {'l', "д", "Д"}, {'m', "ь", "Ь"},
    {'q', "й", "Й"}, {'r', "к", "К"}, {'y', "н", "Н"}, {'h', "р", "Р"}};

bool isKey(const Event& e, char latin) {
    if (!e.is_character()) return false;
    const std::string& ch = e.character();
    if (ch.size() == 1) return std::tolower(static_cast<unsigned char>(ch[0])) == latin;
    for (const auto& k : kRuKeys)
        if (k.latin == latin) return ch == k.lower || ch == k.upper;
    return false;
}

// ------------------------------------------------------------------ app

struct SideItem {
    enum Kind { All, Fav, Totp, Folder, Tag, Sep, Heading } kind;
    std::string key;
    int count = 0;
    bool selectable() const { return kind != Sep && kind != Heading; }
};

struct Toast {
    std::string msg;
    th::Rgb col = th::green;
    Clock::time_point at{};
    double dur = 3.0;
    bool scramble = false;
    bool on = false;
};

class App {
public:
    explicit App(AppOptions o);
    int run();

private:
    enum class Stage { Splash, Unlock, Main, Sealing };
    enum class Modal { None, Editor, Generator, Confirm, Master, Help };

    // --- lifecycle
    void setStage(Stage s);
    void setModal(Modal m);
    void syncTab();
    void updatePlaceholders();
    double at(Clock::time_point p) const { return opt_.animations ? anim::since(p) : 1e3; }
    bool animating() const;
    void tick();
    bool onEvent(Event e);
    Element render();

    // --- unlock
    bool onUnlockKey(const Event& e);
    void submitUnlock();
    void failUnlock(const std::string& msg);
    void pollUnlock();
    void enterMain();
    void lock();
    Element renderSplash(double t);
    Element renderUnlock(double t);
    Element padlock(double t);
    Element inputBox(const Component& c, const std::string& icon);

    // --- main
    void rebuildSidebar();
    void refilter(bool keep_selection = true);
    void selectId(const std::string& id);
    void moveSel(int d);
    void moveSide(int d);
    void selectionChanged();
    Entry* current();
    bool onMainKey(const Event& e);
    bool onMainMouse(Event& e);
    Element renderMain(double t);
    Element renderHeader();
    Element renderSidebar(double t);
    Element renderList(double t);
    Element renderDetails();
    Element renderFooter();
    Element renderToast();
    void toast(std::string msg, th::Rgb col = th::green, bool scramble = false, double dur = 3.0);
    bool save(bool announce = true);
    void copyValue(const std::string& v, const Tr& msg, bool with_timer);

    // --- modals
    Element modalFrame(const std::string& title, Element content, int width, th::Rgb border);
    void openEditor(const Entry* e);
    void closeEditor();
    void submitEditor();
    int editorFocus() const;
    bool onEditorKey(const Event& e);
    Element renderEditor();
    void openGenerator(bool for_editor);
    void closeGenerator();
    void regenerate();
    bool onGeneratorKey(const Event& e);
    Element renderGenerator();
    bool onConfirmKey(const Event& e);
    Element renderConfirm();
    void openMaster();
    void submitMaster();
    void pollMaster();
    bool onMasterKey(const Event& e);
    Element renderMaster();
    Element renderHelp();

    // --- exit
    void beginSeal();
    Element renderSeal(double t);

    AppOptions opt_;
    ScreenInteractive screen_ = ScreenInteractive::Fullscreen();
    Clipboard clip_;
    std::unique_ptr<Vault> vault_;

    Stage stage_ = Stage::Splash;
    Modal modal_ = Modal::None;
    int tab_ = 4;
    Clock::time_point stage_at_ = Clock::now();
    Clock::time_point modal_at_ = Clock::now();
    Clock::time_point last_activity_ = Clock::now();
    std::atomic<bool> fast_{true};
    std::atomic<bool> running_{true};
    std::thread ticker_;

    // placeholders (pointers are handed to the Input components)
    std::string ph_pw1_, ph_pw2_, ph_search_, ph_title_, ph_url_, ph_folder_, ph_tags_, ph_totp_, ph_none_;

    // unlock
    bool creating_ = false;
    std::string pw1_, pw2_;
    std::string unlock_err_;
    std::optional<Clock::time_point> shake_at_, opened_at_, locked_at_;
    std::future<std::unique_ptr<Vault>> unlocking_;
    Clock::time_point unlock_started_{};

    // main
    std::vector<SideItem> side_;
    int side_sel_ = 0;
    std::vector<std::size_t> rows_;
    int sel_ = 0;
    bool focus_side_ = false;
    std::string search_;
    bool search_active_ = false;
    bool reveal_ = false;
    Clock::time_point reveal_at_{}, main_at_{}, detail_at_{};
    Box side_box_, list_box_;
    std::vector<Box> side_boxes_, row_boxes_;
    Toast toast_;

    // editor
    std::string e_title_, e_user_, e_pass_, e_url_, e_folder_, e_tags_, e_totp_, e_notes_;
    bool e_fav_ = false;
    bool e_mask_ = true;
    std::string e_id_, e_err_;
    std::vector<Component> e_inputs_;

    // generator
    GenOptions gopt_;
    GenResult gen_;
    int gsel_ = 0;
    bool gen_for_editor_ = false;
    Clock::time_point gen_at_{};

    // confirm
    std::string confirm_id_;

    // master password change
    std::string m1_, m2_, m_err_;
    std::future<Vault::Rekey> rekeying_;
    Clock::time_point rekey_at_{};

    // components
    Component pw1_in_, pw2_in_, search_in_, m1_in_, m2_in_;
    Component tabs_, root_;
};

App::App(AppOptions o) : opt_(std::move(o)) {
    auto field = [](std::string* content, std::string* placeholder, Ref<bool> masked, bool multiline,
                    std::function<void()> on_change = {}) {
        InputOption opt;
        opt.password = std::move(masked);
        opt.multiline = multiline;
        if (on_change) opt.on_change = std::move(on_change);
        opt.transform = [](InputState s) {
            s.element |= color(th::c(s.is_placeholder ? th::dim2 : th::fg));
            if (s.focused) s.element |= bgcolor(th::c(th::input_bg));
            return s.element;
        };
        return Input(content, placeholder, opt);
    };

    pw1_in_ = field(&pw1_, &ph_pw1_, true, false, [this] { unlock_err_.clear(); });
    pw2_in_ = field(&pw2_, &ph_pw2_, true, false);
    search_in_ = field(&search_, &ph_search_, false, false, [this] { refilter(); });
    e_inputs_ = {
        field(&e_title_, &ph_title_, false, false),
        field(&e_user_, &ph_none_, false, false),
        field(&e_pass_, &ph_none_, &e_mask_, false),
        field(&e_url_, &ph_url_, false, false),
        field(&e_folder_, &ph_folder_, false, false),
        field(&e_tags_, &ph_tags_, false, false),
        field(&e_totp_, &ph_totp_, false, false),
        field(&e_notes_, &ph_none_, false, true),
    };
    m1_in_ = field(&m1_, &ph_pw1_, true, false, [this] { m_err_.clear(); });
    m2_in_ = field(&m2_, &ph_pw2_, true, false);

    tabs_ = Container::Tab(
        {
            Container::Vertical({pw1_in_, pw2_in_}),
            Container::Vertical({search_in_}),
            Container::Vertical(e_inputs_),
            Container::Vertical({m1_in_, m2_in_}),
            Renderer([] { return emptyElement(); }),
        },
        &tab_);
    root_ = CatchEvent(Renderer(tabs_, [this] { return render(); }), [this](Event e) { return onEvent(e); });

    updatePlaceholders();
    setStage(opt_.animations ? Stage::Splash : Stage::Unlock);
}

int App::run() {
    ticker_ = std::thread([this] {
        while (running_) {
            std::this_thread::sleep_for(fast_ ? 33ms : 200ms);
            screen_.PostEvent(Event::Custom);
        }
    });
    screen_.Loop(root_);
    running_ = false;
    ticker_.join();

    // Leave nothing behind.
    for (auto* s : {&pw1_, &pw2_, &m1_, &m2_, &search_, &e_title_, &e_user_, &e_pass_, &e_url_, &e_folder_,
                    &e_tags_, &e_totp_, &e_notes_, &gen_.value})
        wipe(*s);
    vault_.reset();
    clip_.clearIfOurs();
    return 0;
}

// ------------------------------------------------------------------ lifecycle

void App::setStage(Stage s) {
    stage_ = s;
    stage_at_ = Clock::now();
    modal_ = Modal::None;
    if (s == Stage::Unlock) {
        std::error_code ec;
        creating_ = !std::filesystem::exists(opt_.vault_path, ec);
        pw1_in_->TakeFocus();
    }
    syncTab();
}

void App::setModal(Modal m) {
    modal_ = m;
    modal_at_ = Clock::now();
    syncTab();
}

void App::syncTab() {
    if (stage_ == Stage::Unlock) tab_ = 0;
    else if (stage_ != Stage::Main) tab_ = 4;
    else if (modal_ == Modal::None) tab_ = 1;
    else if (modal_ == Modal::Editor) tab_ = 2;
    else if (modal_ == Modal::Master) tab_ = 3;
    else tab_ = 4;
}

void App::updatePlaceholders() {
    ph_pw1_ = T(tr::ph_master);
    ph_pw2_ = T(tr::ph_repeat);
    ph_search_ = T(tr::ph_search);
    ph_title_ = T(tr::ph_title);
    ph_url_ = T(tr::ph_url);
    ph_folder_ = T(tr::ph_folder);
    ph_tags_ = T(tr::ph_tags);
    ph_totp_ = T(tr::ph_totp);
}

bool App::animating() const {
    if (stage_ != Stage::Main) return true;
    if (toast_.on || modal_ != Modal::None) return true;
    return anim::since(main_at_) < 2.0 || anim::since(detail_at_) < 1.0 || (reveal_ && anim::since(reveal_at_) < 0.6);
}

void App::tick() {
    pollUnlock();
    pollMaster();
    if (stage_ == Stage::Splash && at(stage_at_) > 3.4) setStage(Stage::Unlock);
    if (stage_ == Stage::Sealing && at(stage_at_) > 1.8) screen_.Exit();
    if (stage_ == Stage::Main && !rekeying_.valid() &&
        anim::since(last_activity_) > static_cast<double>(opt_.autolock_seconds))
        lock();
    if (toast_.on && anim::since(toast_.at) > toast_.dur) toast_.on = false;
}

bool App::onEvent(Event e) {
    if (e == Event::Custom) {
        tick();
        return true;
    }
    if (e.is_mouse()) {
        const auto& m = e.mouse();
        if (m.button != Mouse::None) last_activity_ = Clock::now();
        if (stage_ == Stage::Main && modal_ == Modal::None) return onMainMouse(e);
        if (stage_ == Stage::Unlock || modal_ == Modal::Editor || modal_ == Modal::Master) return false;
        return true;
    }
    last_activity_ = Clock::now();

    if (e == Event::F2) {
        currentLang() = currentLang() == Lang::Ru ? Lang::En : Lang::Ru;
        updatePlaceholders();
        return true;
    }

    switch (stage_) {
        case Stage::Splash:
            setStage(Stage::Unlock);
            return true;
        case Stage::Sealing:
            return true;
        case Stage::Unlock:
            return onUnlockKey(e);
        case Stage::Main:
            switch (modal_) {
                case Modal::None: return onMainKey(e);
                case Modal::Editor: return onEditorKey(e);
                case Modal::Generator: return onGeneratorKey(e);
                case Modal::Confirm: return onConfirmKey(e);
                case Modal::Master: return onMasterKey(e);
                case Modal::Help:
                    if (e == Event::Escape || e == Event::Return || e == Event::F1 || isKey(e, 'q') ||
                        e == Event::Character("?") || e == Event::Character(","))
                        setModal(Modal::None);
                    return true;
            }
    }
    return true;
}

Element App::render() {
    const auto size = Terminal::Size();
    fast_ = animating();

    Element view;
    if (size.dimx < kMinWidth || size.dimy < kMinHeight) {
        view = vbox({filler(),
                     text("🦊 KeyFox") | bold | color(th::c(th::orange)) | hcenter,
                     text(T(tr::too_small)) | hcenter,
                     text(std::to_string(kMinWidth) + "×" + std::to_string(kMinHeight) + "  (" +
                          std::to_string(size.dimx) + "×" + std::to_string(size.dimy) + ")") |
                         color(th::c(th::amber)) | hcenter,
                     filler()});
    } else {
        switch (stage_) {
            case Stage::Splash: view = renderSplash(at(stage_at_)); break;
            case Stage::Unlock: view = renderUnlock(anim::since(stage_at_)); break;
            case Stage::Main: view = renderMain(at(main_at_)); break;
            case Stage::Sealing: view = renderSeal(at(stage_at_)); break;
        }
    }
    return view | color(th::c(th::fg)) | bgcolor(th::c(th::bg));
}

// ------------------------------------------------------------------ splash

Element App::renderSplash(double t) {
    // 1. The fox rises in line by line, then a diagonal shine sweeps across it.
    Elements fox;
    const int rows = static_cast<int>(art::kFox.size());
    for (int y = 0; y < rows; ++y) {
        const auto g = utf8::glyphs(art::kFox[static_cast<std::size_t>(y)]);
        const double a = anim::easeOutCubic(anim::phase(t, 0.08 * y, 0.35));
        const th::Rgb base = th::foxGradient(double(y) / (rows - 1));
        const double shine = (t - 1.15) * 38;
        Elements cells;
        for (int x = 0; x < static_cast<int>(g.size()); ++x) {
            const double d = std::abs((x + y * 2) - shine);
            const th::Rgb c = d < 3 ? th::mix(base, th::Rgb{255, 244, 225}, (3 - d) / 3) : base;
            cells.push_back(text(g[static_cast<std::size_t>(x)]) | color(th::fade(c, a)));
        }
        fox.push_back(hbox(std::move(cells)));
    }

    // 2. The logo is written left to right by a bright cursor and then shimmers.
    Elements logo;
    const double front = (t - 0.7) * 95;
    for (std::size_t y = 0; y < art::kLogo.size(); ++y) {
        const auto g = utf8::glyphs(art::kLogo[y]);
        Elements cells;
        for (int x = 0; x < static_cast<int>(g.size()); ++x) {
            if (x > front) {
                cells.push_back(text(" "));
                continue;
            }
            const double k = 0.5 + 0.5 * std::sin(x * 0.17 - t * 4.0 + y * 0.4);
            th::Rgb c = th::mix(th::amber, th::deep, k);
            if (front - x < 3) c = th::mix(th::white, c, (front - x) / 3);
            cells.push_back(text(g[static_cast<std::size_t>(x)]) | color(th::c(c)));
        }
        logo.push_back(hbox(std::move(cells)));
    }

    // 3. Typewriter tagline + boot progress bar.
    const std::string tagline = anim::typed(T(tr::tagline), anim::phase(t, 1.3, 0.9));
    const double p = anim::easeInOut(anim::phase(t, 0.3, 2.0));
    const int bw = 34;
    const int filled = static_cast<int>(std::lround(p * bw));
    Element status = p < 1 ? hbox({text(anim::spinner(t)) | color(th::c(th::orange)), text(" "),
                                   text(repeat("━", filled)) | color(th::c(th::orange)),
                                   text(repeat("━", bw - filled)) | color(th::c(th::dim2)),
                                   text("  " + T(tr::booting)) | color(th::c(th::dim))})
                           : hbox({text("✓ ") | color(th::c(th::green)), text(repeat("━", bw)) | color(th::c(th::green)),
                                   text("  " + T(tr::boot_ready)) | color(th::c(th::dim))});

    return vbox({filler(), vbox(std::move(fox)) | hcenter, text(""), vbox(std::move(logo)) | hcenter, text(""),
                 text(tagline) | color(th::c(th::dim)) | hcenter, text(""), status | hcenter, filler()});
}

// ------------------------------------------------------------------ unlock

Element App::padlock(double t) {
    const std::array<std::string_view, 7>* frame = &art::kLockClosed;
    th::Rgb c = th::mix(th::amber, th::orange, 0.5 + 0.5 * std::sin(t * 2.2));  // idle "breathing"

    if (opened_at_) {
        frame = &art::kLockOpen;
        c = th::green;
    } else if (unlocking_.valid()) {
        const double s = anim::since(unlock_started_);
        frame = (static_cast<int>(s * 7) % 2) ? &art::kLockLifted : &art::kLockClosed;  // rattling
        c = th::mix(th::orange, th::amber, 0.5 + 0.5 * std::sin(s * 9));
    } else if (locked_at_ && at(*locked_at_) < 0.6) {
        const double s = at(*locked_at_);  // closing: open -> lifted -> click
        frame = s < 0.3 ? &art::kLockOpen : s < 0.45 ? &art::kLockLifted : &art::kLockClosed;
        c = s < 0.45 ? th::amber : th::mix(th::white, th::amber, (s - 0.45) / 0.15);
    } else if (shake_at_ && anim::since(*shake_at_) < 1.2) {
        c = th::red;
    }

    Elements rows;
    for (const auto& l : *frame) rows.push_back(text(std::string(l)) | color(th::c(c)));
    return vbox(std::move(rows));
}

Element App::inputBox(const Component& c, const std::string& icon) {
    const bool f = c->Focused();
    return hbox({text(" " + icon + " ") | color(th::c(f ? th::orange : th::dim)), c->Render() | flex}) | borderRounded |
           color(th::c(f ? th::orange : th::line));
}

Element App::renderUnlock(double t) {
    const bool busy = unlocking_.valid();
    const std::string kdf_info = "Argon2id · " + std::to_string(opt_.kdf.memlimit_kib / 1024) + " MiB · t=" +
                                 std::to_string(opt_.kdf.opslimit);
    Elements body;
    body.push_back(padlock(t) | hcenter);
    body.push_back(text(""));
    body.push_back(hbox({txt(T(tr::vault) + "  ", th::dim), text(win::pathUtf8(opt_.vault_path)) | color(th::c(th::white))}));
    body.push_back(text(""));
    body.push_back(txt(T(tr::master), th::dim));
    body.push_back(inputBox(pw1_in_, "🔑"));
    if (creating_) {
        const Strength s = estimateStrength(pw1_);
        body.push_back(hbox({heatBar(std::min(1.0, s.bits / 100), 30), text("  "),
                             txt(pw1_.empty() ? "" : levelName(s.level), levelColor(s.level))}));
        body.push_back(txt(T(tr::repeat), th::dim));
        body.push_back(inputBox(pw2_in_, "🔑"));
    }
    body.push_back(text(""));

    // Status line.
    if (opened_at_) {
        body.push_back(txt("✓ " + T(tr::granted), th::green) | bold);
        body.push_back(lineBar(1.0, 60, th::green));
    } else if (busy) {
        const double s = anim::since(unlock_started_);
        body.push_back(hbox({txt(anim::spinner(s), th::orange), text(" "),
                             txt(T(creating_ ? tr::creating : tr::deriving), th::amber) | bold,
                             txt(" · " + kdf_info, th::dim)}));
        body.push_back(sweepBar(s, 60));
    } else if (!unlock_err_.empty()) {
        body.push_back(txt("✗ " + unlock_err_, th::red) | bold);
        body.push_back(text(""));
    } else if (locked_at_ && anim::since(*locked_at_) < 3) {
        body.push_back(txt("🔒 " + T(tr::locked), th::amber));
        body.push_back(text(""));
    } else if (win::capsLockOn()) {
        body.push_back(txt(T(tr::caps), th::yellow));
        body.push_back(text(""));
    } else if (creating_) {
        body.push_back(paragraph(T(tr::create_note)) | color(th::c(th::dim)));
    } else {
        body.push_back(txt(kdf_info, th::dim2));
        body.push_back(text(""));
    }
    body.push_back(txt(T(creating_ ? tr::hints_create : tr::hints_unlock), th::dim));

    th::Rgb border = th::orange;
    if (opened_at_) border = th::green;
    else if (busy) border = th::mix(th::deep, th::amber, 0.5 + 0.5 * std::sin(anim::since(unlock_started_) * 5));
    else if (shake_at_ && anim::since(*shake_at_) < 1.2) border = th::red;

    Element card = window(text(" 🦊 KeyFox · " + T(creating_ ? tr::new_vault : tr::sign_in) + " ") | bold |
                              color(th::c(th::amber)),
                          vbox(std::move(body)) | color(th::c(th::fg))) |
                   color(th::c(border)) | size(WIDTH, EQUAL, 72);

    // Wrong password: the card shakes and settles.
    int offset = 0;
    if (shake_at_) {
        const double s = anim::since(*shake_at_);
        if (s < 0.55) offset = static_cast<int>(std::lround(std::sin(s * 48) * 3 * (1 - s / 0.55)));
    }
    return vbox({filler(), hbox({filler(), spacer(4 + offset), card, spacer(4 - offset), filler()}), filler()});
}

bool App::onUnlockKey(const Event& e) {
    if (unlocking_.valid() || opened_at_) return true;
    if (e == Event::Escape) {
        beginSeal();
        return true;
    }
    if (e == Event::Return) {
        if (creating_ && pw1_in_->Focused()) pw2_in_->TakeFocus();
        else submitUnlock();
        return true;
    }
    if (e == Event::Tab || e == Event::TabReverse || e == Event::ArrowDown || e == Event::ArrowUp) {
        if (creating_) (pw1_in_->Focused() ? pw2_in_ : pw1_in_)->TakeFocus();
        return true;
    }
    return false;  // typing goes to the password field
}

void App::failUnlock(const std::string& msg) {
    unlock_err_ = msg;
    shake_at_ = Clock::now();
    pw1_in_->TakeFocus();
}

void App::submitUnlock() {
    if (creating_) {
        if (utf8::length(pw1_) < 8) return failUnlock(T(tr::err_short));
        if (pw1_ != pw2_) return failUnlock(T(tr::err_mismatch));
    } else if (pw1_.empty()) {
        return;
    }
    // The password travels to the worker in a zero-on-free buffer; UI copies are wiped now.
    auto pw = std::make_shared<SecureBytes>(pw1_.begin(), pw1_.end());
    wipe(pw1_);
    wipe(pw2_);
    unlock_err_.clear();
    locked_at_.reset();
    unlock_started_ = Clock::now();
    const auto path = opt_.vault_path;
    const auto kdf = opt_.kdf;
    const bool create = creating_;
    unlocking_ = std::async(std::launch::async, [pw, path, kdf, create] {
        const std::string_view sv(reinterpret_cast<const char*>(pw->data()), pw->size());
        return create ? Vault::create(path, sv, kdf) : Vault::open(path, sv);
    });
}

void App::pollUnlock() {
    if (unlocking_.valid() && unlocking_.wait_for(0s) == std::future_status::ready) {
        try {
            vault_ = unlocking_.get();
            opened_at_ = Clock::now();
        } catch (const crypto::AuthError&) {
            failUnlock(T(tr::err_wrong));
        } catch (const crypto::FormatError&) {
            failUnlock(T(tr::err_format));
        } catch (const std::exception& ex) {
            failUnlock(ex.what());
        }
    }
    if (opened_at_ && at(*opened_at_) > 0.9) {
        opened_at_.reset();
        enterMain();
    }
}

void App::enterMain() {
    creating_ = false;
    side_sel_ = 0;
    wipe(search_);
    search_active_ = false;
    focus_side_ = false;
    reveal_ = false;
    rebuildSidebar();
    refilter(false);
    main_at_ = detail_at_ = last_activity_ = Clock::now();
    setStage(Stage::Main);
    search_in_->TakeFocus();
}

void App::lock() {
    for (auto* s : {&e_title_, &e_user_, &e_pass_, &e_url_, &e_folder_, &e_tags_, &e_totp_, &e_notes_, &m1_, &m2_,
                    &search_, &gen_.value})
        wipe(*s);
    vault_.reset();
    rows_.clear();
    side_.clear();
    reveal_ = false;
    clip_.clearIfOurs();
    unlock_err_.clear();
    shake_at_.reset();
    locked_at_ = Clock::now();
    setStage(Stage::Unlock);
}

// ------------------------------------------------------------------ main: data

Entry* App::current() {
    if (!vault_ || rows_.empty()) return nullptr;
    sel_ = std::clamp(sel_, 0, static_cast<int>(rows_.size()) - 1);
    return &vault_->entries()[rows_[static_cast<std::size_t>(sel_)]];
}

void App::rebuildSidebar() {
    SideItem prev = side_sel_ < static_cast<int>(side_.size()) ? side_[static_cast<std::size_t>(side_sel_)]
                                                              : SideItem{SideItem::All, "", 0};
    side_.clear();
    const auto& es = vault_->entries();
    const int fav = static_cast<int>(std::count_if(es.begin(), es.end(), [](const Entry& e) { return e.favorite; }));
    const int otp = static_cast<int>(std::count_if(es.begin(), es.end(), [](const Entry& e) { return !e.totp.empty(); }));
    side_.push_back({SideItem::All, "", static_cast<int>(es.size())});
    side_.push_back({SideItem::Fav, "", fav});
    side_.push_back({SideItem::Totp, "", otp});

    std::map<std::string, int> folders, tags;
    for (const auto& e : es) {
        if (!e.folder.empty()) ++folders[e.folder];
        for (const auto& t : e.tags) ++tags[t];
    }
    if (!folders.empty()) {
        side_.push_back({SideItem::Sep, "", 0});
        for (const auto& [k, v] : folders) side_.push_back({SideItem::Folder, k, v});
    }
    if (!tags.empty()) {
        side_.push_back({SideItem::Sep, "", 0});
        side_.push_back({SideItem::Heading, "", 0});
        for (const auto& [k, v] : tags) side_.push_back({SideItem::Tag, k, v});
    }

    side_sel_ = 0;
    for (std::size_t i = 0; i < side_.size(); ++i)
        if (side_[i].kind == prev.kind && side_[i].key == prev.key) side_sel_ = static_cast<int>(i);
}

void App::refilter(bool keep_selection) {
    if (!vault_) return;
    std::string keep_id;
    if (keep_selection)
        if (Entry* e = current()) keep_id = e->id;

    const auto& es = vault_->entries();
    const std::string q = utf8::lower(trim(search_));
    const SideItem side = side_sel_ < static_cast<int>(side_.size()) ? side_[static_cast<std::size_t>(side_sel_)]
                                                                    : SideItem{SideItem::All, "", 0};
    rows_.clear();
    for (std::size_t i = 0; i < es.size(); ++i) {
        const Entry& e = es[i];
        bool ok = true;
        switch (side.kind) {
            case SideItem::Fav: ok = e.favorite; break;
            case SideItem::Totp: ok = !e.totp.empty(); break;
            case SideItem::Folder: ok = e.folder == side.key; break;
            case SideItem::Tag: ok = std::find(e.tags.begin(), e.tags.end(), side.key) != e.tags.end(); break;
            default: break;
        }
        if (ok && !q.empty()) {
            ok = utf8::containsCi(e.title, q) || utf8::containsCi(e.username, q) || utf8::containsCi(e.url, q) ||
                 utf8::containsCi(e.folder, q) || utf8::containsCi(joinTags(e.tags, ""), q);
        }
        if (ok) rows_.push_back(i);
    }
    std::sort(rows_.begin(), rows_.end(), [&](std::size_t a, std::size_t b) {
        const auto la = utf8::lower(es[a].title), lb = utf8::lower(es[b].title);
        return la != lb ? la < lb : es[a].username < es[b].username;
    });

    sel_ = 0;
    if (!keep_id.empty()) selectId(keep_id);
    selectionChanged();
}

void App::selectId(const std::string& id) {
    for (std::size_t i = 0; i < rows_.size(); ++i)
        if (vault_->entries()[rows_[i]].id == id) sel_ = static_cast<int>(i);
}

void App::selectionChanged() {
    reveal_ = false;
    detail_at_ = Clock::now();
}

void App::moveSel(int d) {
    if (rows_.empty()) return;
    const int before = sel_;
    sel_ = std::clamp(sel_ + d, 0, static_cast<int>(rows_.size()) - 1);
    if (sel_ != before) selectionChanged();
}

void App::moveSide(int d) {
    if (side_.empty()) return;
    int i = side_sel_;
    const int step = d > 0 ? 1 : -1;
    for (int n = std::abs(d); n > 0; --n) {
        int j = i + step;
        while (j >= 0 && j < static_cast<int>(side_.size()) && !side_[static_cast<std::size_t>(j)].selectable()) j += step;
        if (j < 0 || j >= static_cast<int>(side_.size())) break;
        i = j;
    }
    if (i != side_sel_) {
        side_sel_ = i;
        refilter(false);
    }
}

void App::toast(std::string msg, th::Rgb col, bool scramble, double dur) {
    toast_ = Toast{std::move(msg), col, Clock::now(), dur, scramble && opt_.animations, true};
}

bool App::save(bool announce) {
    try {
        vault_->save();
        if (announce) toast(T(tr::t_saved), th::green, true, 2.6);
        return true;
    } catch (const std::exception& ex) {
        toast(T(tr::t_save_fail) + " " + ex.what(), th::red, false, 6);
        return false;
    }
}

void App::copyValue(const std::string& v, const Tr& msg, bool with_timer) {
    if (v.empty()) return toast(T(tr::t_empty), th::yellow);
    if (!clip_.copy(v, opt_.clipboard_seconds)) return toast(T(tr::t_clip_fail), th::red);
    toast(with_timer ? T(msg) + " " + std::to_string(opt_.clipboard_seconds) + " " + T(tr::sec) : T(msg));
}

// ------------------------------------------------------------------ main: input

bool App::onMainKey(const Event& e) {
    if (search_active_) {
        if (e == Event::Escape || e == Event::Return || e == Event::Tab) {
            search_active_ = false;
            return true;
        }
        if (e == Event::ArrowUp) { moveSel(-1); return true; }
        if (e == Event::ArrowDown) { moveSel(+1); return true; }
        return false;  // typing goes to the search field (on_change refilters)
    }

    if (e == Event::ArrowUp) { focus_side_ ? moveSide(-1) : moveSel(-1); return true; }
    if (e == Event::ArrowDown) { focus_side_ ? moveSide(+1) : moveSel(+1); return true; }
    if (e == Event::PageUp) { focus_side_ ? moveSide(-10) : moveSel(-10); return true; }
    if (e == Event::PageDown) { focus_side_ ? moveSide(+10) : moveSel(+10); return true; }
    if (e == Event::Home) { focus_side_ ? moveSide(-1000) : moveSel(-100000); return true; }
    if (e == Event::End) { focus_side_ ? moveSide(+1000) : moveSel(+100000); return true; }
    if (e == Event::ArrowLeft) { focus_side_ = true; return true; }
    if (e == Event::ArrowRight) { focus_side_ = false; return true; }
    if (e == Event::Tab || e == Event::TabReverse) { focus_side_ = !focus_side_; return true; }
    if (e == Event::Escape) {
        if (!search_.empty()) {
            wipe(search_);
            refilter();
        }
        return true;
    }
    if (e == Event::Return) {
        if (focus_side_) focus_side_ = false;
        else if (Entry* c = current()) copyValue(c->password, tr::t_pw, true);
        return true;
    }
    if (e == Event::Character("/") || e == Event::Character(".") || e == kCtrlF) {
        search_active_ = true;
        focus_side_ = false;
        search_in_->TakeFocus();
        return true;
    }
    if (e == Event::F1 || e == Event::Character("?") || e == Event::Character(",")) { setModal(Modal::Help); return true; }
    if (isKey(e, 'n')) { openEditor(nullptr); return true; }
    if (isKey(e, 'g')) { openGenerator(false); return true; }
    if (isKey(e, 'l')) { lock(); return true; }
    if (isKey(e, 'm')) { openMaster(); return true; }
    if (isKey(e, 'q')) { beginSeal(); return true; }

    Entry* c = current();
    if (!c) return true;
    if (isKey(e, 'e')) openEditor(c);
    else if (isKey(e, 'd') || e == Event::Delete) {
        confirm_id_ = c->id;
        setModal(Modal::Confirm);
    } else if (isKey(e, 'c')) copyValue(c->password, tr::t_pw, true);
    else if (isKey(e, 'u')) copyValue(c->username, tr::t_login, false);
    else if (isKey(e, 't')) {
        if (auto cfg = parseTotp(c->totp)) copyValue(totpCode(*cfg, std::time(nullptr)), tr::t_code, false);
        else toast(T(c->totp.empty() ? tr::t_empty : tr::bad_totp), th::yellow);
    } else if (isKey(e, 's')) {
        reveal_ = !reveal_;
        reveal_at_ = Clock::now();
    } else if (isKey(e, 'f')) {
        c->favorite = !c->favorite;
        const bool fav = c->favorite;
        const std::string id = c->id;
        rebuildSidebar();
        refilter(false);
        selectId(id);
        if (save(false)) toast(T(fav ? tr::t_fav_on : tr::t_fav_off), th::yellow);
    } else if (isKey(e, 'o')) {
        if (c->url.empty() || !win::openUrl(c->url)) toast(T(tr::t_no_url), th::yellow);
    }
    return true;  // never let hotkeys leak into the search field
}

bool App::onMainMouse(Event& e) {
    auto& m = e.mouse();
    const bool in_side = side_box_.Contain(m.x, m.y);
    if (m.button == Mouse::WheelUp) { in_side ? moveSide(-1) : moveSel(-1); return true; }
    if (m.button == Mouse::WheelDown) { in_side ? moveSide(+1) : moveSel(+1); return true; }
    if (m.button == Mouse::Left && m.motion == Mouse::Pressed) {
        if (list_box_.Contain(m.x, m.y)) {
            focus_side_ = false;
            for (std::size_t i = 0; i < row_boxes_.size(); ++i) {
                if (row_boxes_[i].Contain(m.x, m.y) && static_cast<int>(i) != sel_) {
                    sel_ = static_cast<int>(i);
                    selectionChanged();
                }
            }
        } else if (in_side) {
            focus_side_ = true;
            for (std::size_t i = 0; i < side_boxes_.size() && i < side_.size(); ++i) {
                if (side_boxes_[i].Contain(m.x, m.y) && side_[i].selectable() && static_cast<int>(i) != side_sel_) {
                    side_sel_ = static_cast<int>(i);
                    refilter(false);
                }
            }
        }
    }
    return true;
}

// ------------------------------------------------------------------ main: rendering

Element App::renderMain(double t) {
    Element base = vbox({renderHeader(), hbox({renderSidebar(t), renderList(t), renderDetails()}) | flex, renderFooter()});
    Element layers = dbox({base, renderToast()});
    if (modal_ == Modal::None) return layers;

    Element overlay;
    switch (modal_) {
        case Modal::Editor: overlay = renderEditor(); break;
        case Modal::Generator: overlay = renderGenerator(); break;
        case Modal::Confirm: overlay = renderConfirm(); break;
        case Modal::Master: overlay = renderMaster(); break;
        case Modal::Help: overlay = renderHelp(); break;
        default: overlay = emptyElement(); break;
    }
    return dbox({layers | dim, overlay | clear_under | center});
}

Element App::renderHeader() {
    const int left = std::max(0, opt_.autolock_seconds - static_cast<int>(anim::since(last_activity_)));
    char buf[16];
    std::snprintf(buf, sizeof buf, "%02d:%02d", left / 60, left % 60);
    return hbox({text(" 🦊 KeyFox") | bold | color(th::c(th::orange)),
                 txt(" · " + win::pathUtf8(opt_.vault_path.filename()) + " · " + T(tr::entries_count) + ": " +
                         std::to_string(vault_->entries().size()),
                     th::dim),
                 filler(), txt("🔓 " + T(tr::unlocked), th::green), txt(" · " + T(tr::autolock_in) + " ", th::dim),
                 text(buf) | bold | color(th::c(left < 30 ? th::red : th::amber)), text(" ")});
}

Element App::renderSidebar(double t) {
    Elements rows;
    side_boxes_.assign(side_.size(), Box{});
    for (std::size_t i = 0; i < side_.size(); ++i) {
        const auto& it = side_[i];
        const double a = anim::easeOutCubic(anim::phase(t, 0.04 * static_cast<double>(std::min<std::size_t>(i, 20)), 0.3));
        if (it.kind == SideItem::Sep) {
            rows.push_back(separator() | color(th::fade(th::line, a)));
            continue;
        }
        if (it.kind == SideItem::Heading) {
            rows.push_back(text(" " + T(tr::tags)) | color(th::fade(th::dim, a)));
            continue;
        }
        std::string label;
        th::Rgb c = th::fg;
        switch (it.kind) {
            case SideItem::All: label = "◆ " + T(tr::all); break;
            case SideItem::Fav: label = "★ " + T(tr::favorites); c = th::yellow; break;
            case SideItem::Totp: label = "⏱ " + T(tr::twofa); c = th::cyan; break;
            case SideItem::Folder: label = "▸ " + it.key; break;
            case SideItem::Tag: label = "#" + it.key; c = th::amber; break;
            default: break;
        }
        const bool selected = static_cast<int>(i) == side_sel_;
        Element row = hbox({text(selected ? "▌" : " ") | color(th::fade(th::orange, a)),
                            text(label) | color(th::fade(selected ? th::white : c, a)), filler(),
                            text(std::to_string(it.count) + " ") | color(th::fade(selected ? th::fg : th::dim, a))});
        if (selected) row = row | bold | bgcolor(th::c(focus_side_ ? th::sel : th::track)) | focus;
        rows.push_back(row | reflect(side_boxes_[i]));
    }
    return panel(T(tr::folders), vbox(std::move(rows)) | vscroll_indicator | yframe | flex,
                 focus_side_ && modal_ == Modal::None) |
           size(WIDTH, EQUAL, 26) | reflect(side_box_);
}

Element App::renderList(double t) {
    Element search;
    if (search_active_) {
        search = hbox({text(" 🔍 ") | color(th::c(th::orange)), search_in_->Render() | flex}) | borderRounded |
                 color(th::c(th::orange));
    } else if (search_.empty()) {
        search = hbox({text(" 🔍 ") | color(th::c(th::dim)), txt(T(tr::ph_search), th::dim2), filler()}) | borderRounded |
                 color(th::c(th::line));
    } else {
        search = hbox({text(" 🔍 ") | color(th::c(th::amber)), txt(search_, th::fg), filler(), txt("[Esc] ✕ ", th::dim)}) |
                 borderRounded | color(th::c(th::line));
    }

    const auto& es = vault_->entries();
    Elements rows;
    row_boxes_.assign(rows_.size(), Box{});
    for (std::size_t i = 0; i < rows_.size(); ++i) {
        const Entry& e = es[rows_[i]];
        // Cascade: rows slide in one after another when the vault opens.
        const double a = anim::easeOutCubic(anim::phase(t, 0.15 + 0.035 * static_cast<double>(std::min<std::size_t>(i, 30)), 0.3));
        const bool selected = static_cast<int>(i) == sel_;
        const int indent = static_cast<int>(std::lround((1 - a) * 6));
        Element row = hbox({text(selected ? "▌" : " ") | color(th::c(th::orange)), spacer(indent),
                            text(e.favorite ? "★ " : "  ") | color(th::fade(th::yellow, a)),
                            text(e.title) | color(th::fade(selected ? th::white : th::fg, a)), filler(),
                            text(e.username) | color(th::fade(th::dim, a)) | size(WIDTH, LESS_THAN, 28),
                            text(e.totp.empty() ? "   " : "  ⏱") | color(th::fade(th::cyan, a)), text(" ")});
        if (selected) row = row | bold | bgcolor(th::c(focus_side_ ? th::track : th::sel)) | focus;
        rows.push_back(row | reflect(row_boxes_[i]));
    }

    Element body;
    if (es.empty()) body = paragraph(T(tr::empty_vault)) | color(th::c(th::dim)) | hcenter | vcenter | flex;
    else if (rows_.empty()) body = txt(T(tr::nothing_found), th::dim) | center | flex;
    else body = vbox(std::move(rows)) | vscroll_indicator | yframe | flex;

    const std::string status = " " + std::to_string(rows_.size()) + " " + T(tr::of) + " " + std::to_string(es.size());
    return panel(T(tr::list_title), vbox({search, body, txt(status, th::dim)}), !focus_side_ && modal_ == Modal::None) |
           flex | reflect(list_box_);
}

Element App::renderDetails() {
    const int width = std::clamp(Terminal::Size().dimx * 2 / 5, 44, 72);
    Entry* e = current();
    if (!e) {
        Elements fox;
        for (const auto& l : art::kFox) fox.push_back(txt(std::string(l), th::dim2));
        return panel("—", vbox({filler(), vbox(std::move(fox)) | hcenter, text(""),
                                txt(T(tr::empty_vault), th::dim) | hcenter, filler()}),
                     false) |
               size(WIDTH, EQUAL, width);
    }

    const double dt = at(detail_at_);
    auto label = [](const Tr& l) { return txt(T(l), th::dim) | size(WIDTH, EQUAL, 12); };
    auto value = [](const std::string& v, th::Rgb c) { return v.empty() ? txt("—", th::dim2) : txt(v, c); };

    Elements kv;
    kv.push_back(hbox({label(tr::login), value(e->username, th::white)}));

    std::string shown;
    if (reveal_) shown = anim::scramble(e->password, anim::phase(at(reveal_at_), 0, 0.45), anim::frameNo(anim::since(reveal_at_)));
    else shown = repeat("●", std::min<int>(static_cast<int>(utf8::length(e->password)), 24));
    kv.push_back(hbox({label(tr::password), value(shown, reveal_ ? th::amber : th::fg),
                       txt("  " + T(reveal_ ? tr::hide : tr::show), th::dim)}));
    kv.push_back(hbox({label(tr::website), value(e->url, th::cyan)}));
    kv.push_back(hbox({label(tr::folder), value(e->folder, th::fg)}));
    kv.push_back(hbox({label(tr::tags), value(joinTags(e->tags, "#"), th::amber)}));
    if (!e->password.empty()) {
        const Strength s = estimateStrength(e->password);
        const double grow = anim::easeOutCubic(anim::phase(dt, 0.05, 0.7));
        kv.push_back(hbox({label(tr::strength), heatBar(std::min(1.0, s.bits / 100) * grow, 18),
                           txt("  " + std::to_string(static_cast<int>(s.bits)) + " " + T(tr::bits), levelColor(s.level))}));
    }

    Elements body = {vbox(std::move(kv))};

    if (!e->totp.empty()) {
        body.push_back(separator() | color(th::c(th::line)));
        if (auto cfg = parseTotp(e->totp)) {
            const auto now = static_cast<std::int64_t>(std::time(nullptr));
            const int left = totpSecondsLeft(*cfg, now);
            const th::Rgb c = left > 10 ? th::green : left > 5 ? th::yellow : th::red;
            body.push_back(txt(T(tr::code2fa), th::dim));
            body.push_back(hbox({text(groupCode(totpCode(*cfg, now))) | bold | color(th::c(th::amber)),
                                 txt("   " + T(tr::left_s) + " " + std::to_string(left) + " " + T(tr::sec), th::dim)}));
            body.push_back(lineBar(double(left) / cfg->period, width - 4, c));
        } else {
            body.push_back(txt("⚠ " + T(tr::bad_totp), th::red));
        }
    }

    if (!e->notes.empty()) {
        body.push_back(separator() | color(th::c(th::line)));
        body.push_back(txt(T(tr::notes), th::dim));
        std::size_t start = 0;
        while (start <= e->notes.size()) {
            const auto nl = e->notes.find('\n', start);
            body.push_back(paragraph(e->notes.substr(start, nl == std::string::npos ? std::string::npos : nl - start)));
            if (nl == std::string::npos) break;
            start = nl + 1;
        }
    }

    body.push_back(filler());
    body.push_back(txt(T(tr::modified) + " " + fmtDate(e->modified) + " · " + T(tr::created) + " " + fmtDate(e->created),
                       th::dim2));

    // Details fade in whenever the selection changes.
    const double a = anim::easeOutCubic(anim::phase(dt, 0, 0.25));
    return panel(e->title, vbox(std::move(body)) | color(th::fade(th::fg, a)), false) | size(WIDTH, EQUAL, width);
}

Element App::renderFooter() {
    struct K {
        const char* key;
        std::string label;
    };
    const std::vector<K> keys = {{"n", T(tr::k_new)},   {"e", T(tr::k_edit)},   {"c", T(tr::k_pw)},
                                 {"u", T(tr::k_login)}, {"t", "2FA"},           {"g", T(tr::k_gen)},
                                 {"/", T(tr::k_search)}, {"l", T(tr::k_lock)},  {"q", T(tr::k_quit)},
                                 {"d", T(tr::k_del)},   {"?", T(tr::k_help)}};
    const int width = Terminal::Size().dimx - 2;
    int used = 1;
    Elements out = {text(" ")};
    for (const auto& k : keys) {
        const int w = 3 + 3 + static_cast<int>(utf8::length(k.label));
        if (used + w > width) break;
        used += w;
        out.push_back(keyhint(k.key, k.label));
    }
    return hbox(std::move(out));
}

Element App::renderToast() {
    if (!toast_.on) return emptyElement();
    const double t = anim::since(toast_.at);
    const double a = std::min(anim::phase(t, 0, 0.18), 1 - anim::phase(t, toast_.dur - 0.4, 0.4));
    const std::string msg = toast_.scramble ? anim::scramble(toast_.msg, anim::phase(t, 0.05, 0.6), anim::frameNo(t)) : toast_.msg;
    const int slide = opt_.animations ? static_cast<int>(std::lround((1 - anim::easeOutBack(t / 0.35)) * 10)) : 0;
    Element box = text(" " + msg + " ") | color(th::fade(toast_.col, a)) | borderRounded |
                  color(th::fade(toast_.col, a)) | bgcolor(th::c(th::bg));
    return vbox({filler(), hbox({filler(), box, spacer(2 + std::max(0, slide))}), spacer(1) | size(HEIGHT, EQUAL, 1)});
}

// ------------------------------------------------------------------ modals

Element App::modalFrame(const std::string& title, Element content, int width, th::Rgb border) {
    // Pop-in: the frame grows slightly past its size and settles, the border fades in.
    const double p = at(modal_at_);
    const double grow = 0.85 + 0.15 * anim::easeOutBack(p / 0.3);
    const int w = static_cast<int>(std::lround(width * grow));
    return window(text(" " + title + " ") | bold | color(th::fade(th::amber, anim::phase(p, 0, 0.2))),
                  std::move(content) | color(th::c(th::fg))) |
           color(th::fade(border, anim::phase(p, 0, 0.25))) | size(WIDTH, EQUAL, w) | bgcolor(th::c(th::bg));
}

void App::openEditor(const Entry* e) {
    e_id_ = e ? e->id : "";
    e_title_ = e ? e->title : "";
    e_user_ = e ? e->username : "";
    e_pass_ = e ? e->password : "";
    e_url_ = e ? e->url : "";
    e_folder_ = e ? e->folder : "";
    e_tags_ = e ? joinTags(e->tags, "") : "";
    e_totp_ = e ? e->totp : "";
    e_notes_ = e ? e->notes : "";
    e_fav_ = e && e->favorite;
    e_mask_ = true;
    e_err_.clear();
    setModal(Modal::Editor);
    e_inputs_[0]->TakeFocus();
}

void App::closeEditor() {
    for (auto* s : {&e_title_, &e_user_, &e_pass_, &e_url_, &e_folder_, &e_tags_, &e_totp_, &e_notes_}) wipe(*s);
    setModal(Modal::None);
    search_in_->TakeFocus();
}

void App::submitEditor() {
    const std::string title = trim(e_title_);
    if (title.empty()) {
        e_err_ = T(tr::e_err_title);
        e_inputs_[0]->TakeFocus();
        return;
    }
    const std::string totp = trim(e_totp_);
    if (!totp.empty() && !parseTotp(totp)) {
        e_err_ = T(tr::bad_totp);
        e_inputs_[6]->TakeFocus();
        return;
    }

    Entry* target = e_id_.empty() ? nullptr : vault_->find(e_id_);
    Entry ne = target ? *target : Entry{};
    ne.title = title;
    ne.username = trim(e_user_);
    ne.password = e_pass_;
    ne.url = trim(e_url_);
    ne.folder = trim(e_folder_);
    ne.tags = splitTags(e_tags_);
    ne.totp = totp;
    ne.notes = e_notes_;
    ne.favorite = e_fav_;

    std::string id;
    if (target) {
        ne.modified = static_cast<std::int64_t>(std::time(nullptr));
        *target = std::move(ne);
        id = target->id;
    } else {
        id = vault_->add(std::move(ne)).id;
    }
    closeEditor();
    rebuildSidebar();
    refilter(false);
    selectId(id);
    selectionChanged();
    save();
}

int App::editorFocus() const {
    for (std::size_t i = 0; i < e_inputs_.size(); ++i)
        if (e_inputs_[i]->Focused()) return static_cast<int>(i);
    return 0;
}

bool App::onEditorKey(const Event& e) {
    if (e == Event::Escape) { closeEditor(); return true; }
    if (e == kCtrlS) { submitEditor(); return true; }
    if (e == kCtrlG) { openGenerator(true); return true; }
    if (e == Event::F3) { e_mask_ = !e_mask_; return true; }
    if (e == Event::F4) { e_fav_ = !e_fav_; return true; }

    const int n = static_cast<int>(e_inputs_.size());
    const int notes = n - 1;
    const int idx = editorFocus();
    if (e == Event::Tab || (e == Event::Return && idx != notes) || (e == Event::ArrowDown && idx != notes)) {
        e_inputs_[static_cast<std::size_t>((idx + 1) % n)]->TakeFocus();
        return true;
    }
    if (e == Event::TabReverse || (e == Event::ArrowUp && idx != notes)) {
        e_inputs_[static_cast<std::size_t>((idx - 1 + n) % n)]->TakeFocus();
        return true;
    }
    if (e.is_character()) e_err_.clear();
    return false;  // typing goes to the focused field
}

Element App::renderEditor() {
    auto row = [&](const Tr& label, int idx, Element extra) {
        const bool f = e_inputs_[static_cast<std::size_t>(idx)]->Focused();
        Element in = e_inputs_[static_cast<std::size_t>(idx)]->Render() | flex | bgcolor(th::c(th::input_bg));
        if (idx == 7) in = in | size(HEIGHT, EQUAL, 4);
        return hbox({text(f ? "▶ " : "  ") | color(th::c(th::orange)),
                     txt(T(label), f ? th::amber : th::dim) | size(WIDTH, EQUAL, 12), in, std::move(extra)});
    };

    const Strength s = estimateStrength(e_pass_);
    Element pw_extra = hbox({text(" "), heatBar(std::min(1.0, s.bits / 100), 10), txt(e_mask_ ? " [F3] 👁" : " [F3] ●", th::dim)});

    Element totp_extra = emptyElement();
    const std::string totp = trim(e_totp_);
    if (!totp.empty()) {
        if (auto cfg = parseTotp(totp))
            totp_extra = txt("  " + T(tr::e_now) + ": " + groupCode(totpCode(*cfg, std::time(nullptr))), th::green);
        else
            totp_extra = txt("  ✗", th::red);
    }

    Elements rows = {
        row(tr::e_title, 0, emptyElement()),
        row(tr::login, 1, emptyElement()),
        row(tr::password, 2, std::move(pw_extra)),
        row(tr::website, 3, emptyElement()),
        row(tr::folder, 4, emptyElement()),
        row(tr::tags, 5, emptyElement()),
        row(tr::e_totp, 6, std::move(totp_extra)),
        row(tr::notes, 7, emptyElement()),
        hbox({text("  "), txt(T(tr::e_fav), th::dim) | size(WIDTH, EQUAL, 12),
              txt(e_fav_ ? "★ " + T(tr::yes) : "☆ " + T(tr::no), e_fav_ ? th::yellow : th::dim), txt("   [F4]", th::dim2)}),
        text(""),
        e_err_.empty() ? text("") : txt("✗ " + e_err_, th::red) | bold,
        paragraph(T(tr::e_hints)) | color(th::c(th::dim)),
    };
    return modalFrame(T(e_id_.empty() ? tr::e_new : tr::e_edit), vbox(std::move(rows)), 88, th::orange);
}

void App::openGenerator(bool for_editor) {
    gen_for_editor_ = for_editor;
    gsel_ = 0;
    regenerate();
    setModal(Modal::Generator);
}

void App::closeGenerator() {
    wipe(gen_.value);
    if (gen_for_editor_) {
        setModal(Modal::Editor);
        e_inputs_[2]->TakeFocus();
    } else {
        setModal(Modal::None);
    }
}

void App::regenerate() {
    wipe(gen_.value);
    gen_ = generate(gopt_);
    gen_at_ = Clock::now();
}

bool App::onGeneratorKey(const Event& e) {
    const bool phrase = gopt_.mode == GenOptions::Mode::Phrase;
    const int last_row = phrase ? 1 : 7;

    if (e == Event::Escape) { closeGenerator(); return true; }
    if (e == Event::Return) {
        if (gen_for_editor_) {
            e_pass_ = gen_.value;
            e_mask_ = false;
        } else {
            copyValue(gen_.value, tr::t_gen, false);
        }
        closeGenerator();
        return true;
    }
    if (isKey(e, 'r')) { regenerate(); return true; }
    if (isKey(e, 'c')) { copyValue(gen_.value, tr::t_gen, false); return true; }
    if (e == Event::ArrowUp) { gsel_ = std::max(0, gsel_ - 1); return true; }
    if (e == Event::ArrowDown || e == Event::Tab) { gsel_ = std::min(last_row, gsel_ + 1); return true; }

    bool* toggles[] = {&gopt_.upper, &gopt_.lower, &gopt_.digits, &gopt_.symbols, &gopt_.no_similar, &gopt_.no_repeat};
    auto toggle = [&](int i) {
        bool* b = toggles[i];
        if (i < 4 && *b) {  // keep at least one character class
            int on = 0;
            for (int k = 0; k < 4; ++k) on += *toggles[k] ? 1 : 0;
            if (on == 1) return;
        }
        *b = !*b;
        regenerate();
    };
    if (e.is_character() && e.character().size() == 1 && e.character()[0] >= '1' && e.character()[0] <= '6' && !phrase) {
        toggle(e.character()[0] - '1');
        return true;
    }

    const bool left = e == Event::ArrowLeft, right = e == Event::ArrowRight, space = e == Event::Character(" ");
    if (!(left || right || space)) return true;
    const int d = left ? -1 : 1;
    if (gsel_ == 0) {
        gopt_.mode = phrase ? GenOptions::Mode::Chars : GenOptions::Mode::Phrase;
        gsel_ = std::min(gsel_, gopt_.mode == GenOptions::Mode::Phrase ? 1 : 7);
        regenerate();
    } else if (gsel_ == 1) {
        if (phrase) gopt_.words = std::clamp(gopt_.words + d, 3, 12);
        else gopt_.length = std::clamp(gopt_.length + d, 4, 128);
        regenerate();
    } else {
        toggle(gsel_ - 2);
    }
    return true;
}

Element App::renderGenerator() {
    const double tg = at(gen_at_);
    const bool phrase = gopt_.mode == GenOptions::Mode::Phrase;
    const int width = 84;
    const auto g = utf8::glyphs(gen_.value);
    const std::string pool = generatorPool(gopt_);
    const int n = static_cast<int>(g.size());
    const double step = n > 24 ? 0.012 : 0.045;

    // Slot machine: every reel spins and stops one after another, left to right.
    auto glyphAt = [&](int i, bool& done, bool& landing) {
        const double stop = 0.15 + i * step;
        done = tg >= stop;
        landing = done && tg - stop < 0.12;
        if (done || g[static_cast<std::size_t>(i)] == "-") return g[static_cast<std::size_t>(i)];
        return std::string(1, pool[anim::hash(static_cast<std::uint32_t>(i) * 7919U + anim::frameNo(tg, 40)) % pool.size()]);
    };

    Element reels;
    if (!phrase && n * 3 <= width - 6) {
        Elements cells;
        for (int i = 0; i < n; ++i) {
            bool done = false, landing = false;
            const std::string ch = glyphAt(i, done, landing);
            Element cell = text(ch) | bold |
                           color(th::c(done ? (landing ? th::amber : th::white) : th::mix(th::amber, th::bg, 0.3))) |
                           borderRounded | color(th::c(done ? th::orange : th::line));
            if (landing) cell = cell | bgcolor(th::c(th::sel));  // the reel "lands" with a flash
            cells.push_back(std::move(cell));
        }
        reels = hbox(std::move(cells)) | hcenter;
    } else {
        Elements lines, cur;
        int col = 0;
        for (int i = 0; i < n; ++i) {
            bool done = false, landing = false;
            const std::string ch = glyphAt(i, done, landing);
            cur.push_back(text(ch) | bold | color(th::c(done ? (landing ? th::amber : th::white) : th::dim)));
            if (++col >= width - 8) {
                lines.push_back(hbox(std::move(cur)));
                cur.clear();
                col = 0;
            }
        }
        if (!cur.empty()) lines.push_back(hbox(std::move(cur)));
        reels = vbox({text(""), vbox(std::move(lines)) | hcenter, text("")});
    }

    auto option = [&](int idx, Element content, bool enabled = true) {
        const bool s = gsel_ == idx;
        Element r = hbox({text(s ? " ▶ " : "   ") | color(th::c(th::orange)), std::move(content), filler()});
        if (!enabled) r = r | dim;
        if (s) r = r | bgcolor(th::c(th::track));
        return r;
    };
    auto radio = [](bool on, const std::string& label) {
        return hbox({txt(on ? "(●) " : "( ) ", on ? th::orange : th::dim), txt(label, on ? th::white : th::fg), text("   ")});
    };
    auto check = [](bool on, const std::string& label) {
        return hbox({txt(on ? "[x] " : "[ ] ", on ? th::orange : th::dim), txt(label, th::fg)});
    };

    const int value = phrase ? gopt_.words : gopt_.length;
    const double pos = phrase ? (gopt_.words - 3) / 9.0 : (gopt_.length - 4) / 124.0;
    const int sw = 30, knob = static_cast<int>(std::lround(pos * (sw - 1)));
    Element slider = hbox({txt(repeat("━", knob), th::orange), txt("●", th::amber), txt(repeat("━", sw - 1 - knob), th::dim2)});

    Elements opts = {
        option(0, hbox({txt(T(tr::g_mode), th::dim) | size(WIDTH, EQUAL, 14), radio(!phrase, T(tr::g_chars)),
                        radio(phrase, T(tr::g_phrase))})),
        option(1, hbox({txt(T(phrase ? tr::g_words : tr::g_length), th::dim) | size(WIDTH, EQUAL, 14), txt("◀ ", th::orange),
                        text(std::to_string(value)) | bold | color(th::c(th::white)) | size(WIDTH, EQUAL, 3),
                        txt(" ▶   ", th::orange), slider})),
        option(2, check(gopt_.upper, "1  " + T(tr::g_upper)), !phrase),
        option(3, check(gopt_.lower, "2  " + T(tr::g_lower)), !phrase),
        option(4, check(gopt_.digits, "3  " + T(tr::g_digits)), !phrase),
        option(5, check(gopt_.symbols, "4  " + T(tr::g_symbols)), !phrase),
        option(6, check(gopt_.no_similar, "5  " + T(tr::g_similar)), !phrase),
        option(7, check(gopt_.no_repeat, "6  " + T(tr::g_repeat)), !phrase),
    };

    const int lvl = strengthLevel(gen_.entropy_bits);
    const double fill = std::min(1.0, gen_.entropy_bits / 128) * anim::easeOutCubic(anim::phase(tg, 0.1, 0.9));
    Element strength = hbox({txt(T(tr::strength) + "  ", th::dim), heatBar(fill, 40),
                             txt("  " + std::to_string(static_cast<int>(gen_.entropy_bits)) + " " + T(tr::bits) + " · " + levelName(lvl),
                                 levelColor(lvl)) |
                                 bold});

    return modalFrame(T(tr::g_title),
                      vbox({reels, text(""), vbox(std::move(opts)), text(""), strength,
                            hbox({txt(T(tr::g_crack) + " ", th::dim), txt(crackTime(gen_.entropy_bits), th::green)}),
                            text(""), paragraph(T(gen_for_editor_ ? tr::g_hints_ed : tr::g_hints)) | color(th::c(th::dim))}),
                      width, th::orange);
}

bool App::onConfirmKey(const Event& e) {
    if (e == Event::Return || isKey(e, 'y')) {
        vault_->remove(confirm_id_);
        wipe(confirm_id_);
        setModal(Modal::None);
        rebuildSidebar();
        const int keep = sel_;
        refilter(false);
        sel_ = std::clamp(keep, 0, std::max(0, static_cast<int>(rows_.size()) - 1));
        if (save(false)) toast(T(tr::t_deleted), th::yellow, true);
        return true;
    }
    if (e == Event::Escape || isKey(e, 'n')) {
        setModal(Modal::None);
        return true;
    }
    return true;
}

Element App::renderConfirm() {
    const Entry* e = vault_->find(confirm_id_);
    const std::string title = e ? e->title : "?";
    const th::Rgb pulse = th::mix(th::red, th::deep, 0.5 + 0.5 * std::sin(anim::since(modal_at_) * 5));
    return modalFrame("⚠ " + T(tr::k_del),
                      vbox({text(""), text(T(tr::del_q) + title + T(tr::del_q2)) | bold | color(th::c(th::white)) | hcenter,
                            txt(T(tr::del_note), th::dim) | hcenter, text(""), txt(T(tr::del_hints), th::dim) | hcenter}),
                      58, pulse);
}

void App::openMaster() {
    wipe(m1_);
    wipe(m2_);
    m_err_.clear();
    setModal(Modal::Master);
    m1_in_->TakeFocus();
}

void App::submitMaster() {
    if (rekeying_.valid()) return;
    if (utf8::length(m1_) < 8) { m_err_ = T(tr::err_short); return; }
    if (m1_ != m2_) { m_err_ = T(tr::err_mismatch); return; }
    auto pw = std::make_shared<SecureBytes>(m1_.begin(), m1_.end());
    wipe(m1_);
    wipe(m2_);
    const auto kdf = opt_.kdf_explicit ? opt_.kdf : vault_->kdf();
    rekey_at_ = Clock::now();
    rekeying_ = std::async(std::launch::async, [pw, kdf] {
        return Vault::prepareRekey(std::string_view(reinterpret_cast<const char*>(pw->data()), pw->size()), kdf);
    });
}

void App::pollMaster() {
    if (!rekeying_.valid() || rekeying_.wait_for(0s) != std::future_status::ready) return;
    try {
        auto r = rekeying_.get();
        if (vault_) {
            vault_->applyRekey(std::move(r));
            if (save(false)) toast(T(tr::t_master), th::green, true);
        }
        if (modal_ == Modal::Master) setModal(Modal::None);
    } catch (const std::exception& ex) {
        m_err_ = ex.what();
    }
}

bool App::onMasterKey(const Event& e) {
    if (rekeying_.valid()) return true;
    if (e == Event::Escape) {
        wipe(m1_);
        wipe(m2_);
        setModal(Modal::None);
        return true;
    }
    if (e == Event::Return) {
        if (m1_in_->Focused()) m2_in_->TakeFocus();
        else submitMaster();
        return true;
    }
    if (e == Event::Tab || e == Event::TabReverse || e == Event::ArrowDown || e == Event::ArrowUp) {
        (m1_in_->Focused() ? m2_in_ : m1_in_)->TakeFocus();
        return true;
    }
    return false;
}

Element App::renderMaster() {
    const Strength s = estimateStrength(m1_);
    Element status;
    if (rekeying_.valid()) {
        const double t = anim::since(rekey_at_);
        status = vbox({hbox({txt(anim::spinner(t), th::orange), txt(" " + T(tr::m_busy), th::amber)}), sweepBar(t, 56)});
    } else if (!m_err_.empty()) {
        status = txt("✗ " + m_err_, th::red) | bold;
    } else {
        status = text("");
    }
    return modalFrame(T(tr::m_title),
                      vbox({txt(T(tr::m_new), th::dim), inputBox(m1_in_, "🔑"),
                            hbox({heatBar(std::min(1.0, s.bits / 100), 30), text("  "),
                                  txt(m1_.empty() ? "" : levelName(s.level), levelColor(s.level))}),
                            txt(T(tr::repeat), th::dim), inputBox(m2_in_, "🔑"), text(""), status, text(""),
                            txt(T(tr::m_hints), th::dim)}),
                      64, th::orange);
}

Element App::renderHelp() {
    const std::vector<std::pair<std::string, const Tr*>> rows = {
        {"↑ ↓ PgUp PgDn", &tr::h_nav}, {"← → Tab", &tr::h_panels}, {"Enter", &tr::h_enter},
        {"n", &tr::h_new},            {"e", &tr::h_edit},         {"d / Del", &tr::h_del},
        {"c", &tr::h_enter},          {"u", &tr::h_user},         {"t", &tr::h_totp},
        {"s", &tr::h_show},           {"f", &tr::h_fav},          {"o", &tr::h_open},
        {"g", &tr::h_gen},            {"/  Ctrl+F", &tr::h_search}, {"m", &tr::h_master},
        {"l", &tr::h_lock},           {"F2", &tr::h_lang},        {"q", &tr::h_quit}};
    Elements lines;
    for (const auto& [k, d] : rows)
        lines.push_back(hbox({text("  "), txt(k, th::amber) | bold | size(WIDTH, EQUAL, 16), txt(T(*d), th::fg)}));
    lines.push_back(text(""));
    lines.push_back(txt("  " + T(tr::h_layout), th::dim));
    lines.push_back(txt("  " + T(tr::h_close), th::dim));
    return modalFrame(T(tr::h_title), vbox(std::move(lines)), 60, th::orange);
}

// ------------------------------------------------------------------ exit

void App::beginSeal() {
    lock();  // wipes everything and clears the clipboard
    locked_at_.reset();
    setStage(Stage::Sealing);
}

Element App::renderSeal(double t) {
    // The fox dissolves cell by cell while the padlock snaps shut.
    const double p = anim::phase(t, 0.35, 0.95);
    Elements fox;
    const int rows = static_cast<int>(art::kFox.size());
    for (int y = 0; y < rows; ++y) {
        const auto g = utf8::glyphs(art::kFox[static_cast<std::size_t>(y)]);
        Elements cells;
        for (int x = 0; x < static_cast<int>(g.size()); ++x) {
            const double th_ = anim::noise(static_cast<std::uint32_t>(x), static_cast<std::uint32_t>(y));
            const th::Rgb base = th::foxGradient(double(y) / (rows - 1));
            std::string ch = g[static_cast<std::size_t>(x)];
            double a = 1;
            if (p > th_) {
                const double k = (p - th_) / 0.2;
                ch = ch == " " ? " " : (k < 1 ? "·" : " ");
                a = 1 - anim::clamp01(k);
            }
            cells.push_back(text(ch) | color(th::fade(base, a)));
        }
        fox.push_back(hbox(std::move(cells)));
    }

    const std::array<std::string_view, 7>* frame =
        t < 0.25 ? &art::kLockOpen : t < 0.4 ? &art::kLockLifted : &art::kLockClosed;
    const th::Rgb lock_c = t < 0.4 ? th::amber : th::mix(th::white, th::amber, anim::phase(t, 0.4, 0.3));
    const double fade_out = 1 - anim::phase(t, 1.35, 0.4);
    Elements lock_rows;
    for (const auto& l : *frame) lock_rows.push_back(text(std::string(l)) | color(th::fade(lock_c, fade_out)));

    const double ta = std::min(anim::phase(t, 0.45, 0.35), fade_out);
    return vbox({filler(), vbox(std::move(fox)) | hcenter, text(""), vbox(std::move(lock_rows)) | hcenter, text(""),
                 text(T(tr::sealed)) | bold | color(th::fade(th::amber, ta)) | hcenter,
                 text(T(tr::bye) + " 🦊") | color(th::fade(th::dim, ta)) | hcenter, filler()});
}

}  // namespace

int runApp(const AppOptions& options) {
    App app(options);
    return app.run();
}

}  // namespace kf
