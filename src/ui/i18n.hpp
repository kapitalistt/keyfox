#pragma once
// Russian / English UI strings. English is the default; F2 switches the language at runtime.

#include <cstdint>
#include <string>

namespace kf {

enum class Lang { Ru, En };

inline Lang& currentLang() {
    static Lang lang = Lang::En;
    return lang;
}

struct Tr {
    const char* ru;
    const char* en;
};

inline std::string T(const Tr& t) { return currentLang() == Lang::Ru ? t.ru : t.en; }

// Russian plural forms: 1 год, 2 года, 5 лет.
inline const char* ruPlural(std::int64_t n, const char* one, const char* few, const char* many) {
    n = n < 0 ? -n : n;
    const auto d = n % 10, dd = n % 100;
    if (d == 1 && dd != 11) return one;
    if (d >= 2 && d <= 4 && (dd < 12 || dd > 14)) return few;
    return many;
}

namespace tr {
// splash
inline constexpr Tr tagline{"Менеджер паролей · Argon2id + XChaCha20-Poly1305",
                            "a smart password manager · Argon2id + XChaCha20-Poly1305"};
inline constexpr Tr booting{"загрузка libsodium…", "loading libsodium…"};
inline constexpr Tr boot_ready{"готово · нажмите любую клавишу", "ready · press any key"};

// unlock / create
inline constexpr Tr sign_in{"вход", "sign in"};
inline constexpr Tr new_vault{"новое хранилище", "new vault"};
inline constexpr Tr vault{"Хранилище", "Vault"};
inline constexpr Tr master{"Мастер-пароль", "Master password"};
inline constexpr Tr repeat{"Повторите мастер-пароль", "Repeat master password"};
inline constexpr Tr ph_master{"введите мастер-пароль", "enter master password"};
inline constexpr Tr ph_repeat{"ещё раз", "once more"};
inline constexpr Tr create_note{"Мастер-пароль нельзя восстановить. Запомните его или запишите в надёжном месте.",
                                "The master password cannot be recovered. Remember it or keep it somewhere safe."};
inline constexpr Tr deriving{"Вычисляю ключ", "Deriving key"};
inline constexpr Tr creating{"Создаю хранилище", "Creating vault"};
inline constexpr Tr hints_unlock{"[Enter] открыть   [F2] English   [Esc] выход",
                                 "[Enter] open   [F2] Русский   [Esc] quit"};
inline constexpr Tr hints_create{"[Enter] создать   [Tab] след. поле   [F2] English   [Esc] выход",
                                 "[Enter] create   [Tab] next field   [F2] Русский   [Esc] quit"};
inline constexpr Tr err_wrong{"Неверный мастер-пароль", "Wrong master password"};
inline constexpr Tr err_format{"Файл повреждён или это не хранилище KeyFox", "File is corrupted or not a KeyFox vault"};
inline constexpr Tr err_short{"Мастер-пароль: минимум 8 символов", "Master password: at least 8 characters"};
inline constexpr Tr err_mismatch{"Пароли не совпадают", "Passwords do not match"};
inline constexpr Tr caps{"⚠ Включён Caps Lock", "⚠ Caps Lock is on"};
inline constexpr Tr granted{"Доступ разрешён", "Access granted"};
inline constexpr Tr locked{"Хранилище заблокировано", "Vault locked"};

// header / sidebar / list
inline constexpr Tr entries_count{"записей", "entries"};
inline constexpr Tr unlocked{"открыто", "unlocked"};
inline constexpr Tr autolock_in{"автоблок через", "auto-lock in"};
inline constexpr Tr folders{"Папки", "Folders"};
inline constexpr Tr all{"Все", "All"};
inline constexpr Tr favorites{"Избранное", "Favorites"};
inline constexpr Tr twofa{"2FA", "2FA"};
inline constexpr Tr tags{"Теги", "Tags"};
inline constexpr Tr list_title{"Записи", "Entries"};
inline constexpr Tr ph_search{"поиск… (/)", "search… (/)"};
inline constexpr Tr of{"из", "of"};
inline constexpr Tr nothing_found{"Ничего не найдено", "Nothing found"};
inline constexpr Tr empty_vault{"Пусто. Нажмите n, чтобы добавить первую запись.", "Empty. Press n to add your first entry."};

// details
inline constexpr Tr login{"Логин", "Login"};
inline constexpr Tr password{"Пароль", "Password"};
inline constexpr Tr website{"Сайт", "Website"};
inline constexpr Tr folder{"Папка", "Folder"};
inline constexpr Tr strength{"Надёжность", "Strength"};
inline constexpr Tr code2fa{"Код 2FA", "2FA code"};
inline constexpr Tr left_s{"ещё", "left:"};
inline constexpr Tr notes{"Заметки", "Notes"};
inline constexpr Tr modified{"Изменено", "Modified"};
inline constexpr Tr created{"создано", "created"};
inline constexpr Tr show{"[s] показать", "[s] show"};
inline constexpr Tr hide{"[s] скрыть", "[s] hide"};
inline constexpr Tr bad_totp{"неверный секрет 2FA", "invalid 2FA secret"};
inline constexpr Tr bits{"бит", "bits"};

// footer
inline constexpr Tr k_new{"новая", "new"};
inline constexpr Tr k_edit{"изменить", "edit"};
inline constexpr Tr k_del{"удалить", "delete"};
inline constexpr Tr k_pw{"пароль", "password"};
inline constexpr Tr k_login{"логин", "login"};
inline constexpr Tr k_gen{"генератор", "generator"};
inline constexpr Tr k_search{"поиск", "search"};
inline constexpr Tr k_lock{"блок", "lock"};
inline constexpr Tr k_help{"справка", "help"};
inline constexpr Tr k_quit{"выход", "quit"};

// toasts
inline constexpr Tr t_pw{"✓ Пароль скопирован · буфер очистится через", "✓ Password copied · clipboard clears in"};
inline constexpr Tr t_login{"✓ Логин скопирован", "✓ Login copied"};
inline constexpr Tr t_code{"✓ Код 2FA скопирован", "✓ 2FA code copied"};
inline constexpr Tr t_gen{"✓ Сгенерированный пароль скопирован", "✓ Generated password copied"};
inline constexpr Tr t_empty{"Поле пустое", "Field is empty"};
inline constexpr Tr t_clip_fail{"Буфер обмена недоступен", "Clipboard is unavailable"};
inline constexpr Tr t_saved{"🔒 Зашифровано и сохранено", "🔒 Encrypted and saved"};
inline constexpr Tr t_save_fail{"Ошибка сохранения:", "Save failed:"};
inline constexpr Tr t_deleted{"Запись удалена", "Entry deleted"};
inline constexpr Tr t_fav_on{"★ В избранном", "★ Added to favorites"};
inline constexpr Tr t_fav_off{"Убрано из избранного", "Removed from favorites"};
inline constexpr Tr t_no_url{"У записи нет сайта", "This entry has no website"};
inline constexpr Tr t_master{"🔑 Мастер-пароль изменён", "🔑 Master password changed"};
inline constexpr Tr sec{"с", "s"};

// editor
inline constexpr Tr e_new{"Новая запись", "New entry"};
inline constexpr Tr e_edit{"Изменить запись", "Edit entry"};
inline constexpr Tr e_title{"Название", "Title"};
inline constexpr Tr e_totp{"2FA секрет", "2FA secret"};
inline constexpr Tr e_fav{"Избранное", "Favorite"};
inline constexpr Tr ph_title{"например, GitHub", "e.g. GitHub"};
inline constexpr Tr ph_tags{"через пробел: dev main", "space separated: dev main"};
inline constexpr Tr ph_totp{"base32 или otpauth://…", "base32 or otpauth://…"};
inline constexpr Tr ph_url{"https://…", "https://…"};
inline constexpr Tr ph_folder{"Работа, Личное…", "Work, Personal…"};
inline constexpr Tr e_hints{"[Ctrl+S] сохранить  [Tab] далее  [Ctrl+G] генератор  [F3] показать  [F4] избранное  [Esc] отмена",
                            "[Ctrl+S] save  [Tab] next  [Ctrl+G] generator  [F3] show  [F4] favorite  [Esc] cancel"};
inline constexpr Tr e_err_title{"Укажите название", "Title is required"};
inline constexpr Tr e_now{"сейчас", "now"};
inline constexpr Tr yes{"да", "yes"};
inline constexpr Tr no{"нет", "no"};

// generator
inline constexpr Tr g_title{"⚡ Генератор паролей", "⚡ Password generator"};
inline constexpr Tr g_mode{"Режим", "Mode"};
inline constexpr Tr g_chars{"Символы", "Characters"};
inline constexpr Tr g_phrase{"Фраза", "Passphrase"};
inline constexpr Tr g_length{"Длина", "Length"};
inline constexpr Tr g_words{"Слов", "Words"};
inline constexpr Tr g_upper{"A–Z  заглавные", "A–Z  uppercase"};
inline constexpr Tr g_lower{"a–z  строчные", "a–z  lowercase"};
inline constexpr Tr g_digits{"0–9  цифры", "0–9  digits"};
inline constexpr Tr g_symbols{"!@#$ символы", "!@#$ symbols"};
inline constexpr Tr g_similar{"без похожих (Il1O0)", "no look-alikes (Il1O0)"};
inline constexpr Tr g_repeat{"без повторов", "no repeats"};
inline constexpr Tr g_crack{"Перебор при 10^12 попыток/с:", "Brute force at 10^12 guesses/s:"};
inline constexpr Tr g_hints_ed{"[Enter] вставить  [r] ещё  [c] копировать  [↑↓] пункт  [←→/Space] изменить  [Esc] закрыть",
                               "[Enter] insert  [r] again  [c] copy  [↑↓] option  [←→/Space] change  [Esc] close"};
inline constexpr Tr g_hints{"[Enter] копировать и закрыть  [r] ещё  [↑↓] пункт  [←→/Space] изменить  [Esc] закрыть",
                            "[Enter] copy & close  [r] again  [↑↓] option  [←→/Space] change  [Esc] close"};
inline constexpr Tr lvl0{"очень слабый", "very weak"};
inline constexpr Tr lvl1{"слабый", "weak"};
inline constexpr Tr lvl2{"средний", "fair"};
inline constexpr Tr lvl3{"надёжный", "strong"};
inline constexpr Tr lvl4{"отлично", "excellent"};
inline constexpr Tr instantly{"мгновенно", "instantly"};
inline constexpr Tr u_min{"мин", "min"};
inline constexpr Tr u_h{"ч", "h"};
inline constexpr Tr u_days{"дн.", "days"};

// confirm / master / help / exit
inline constexpr Tr del_q{"Удалить «", "Delete “"};
inline constexpr Tr del_q2{"»?", "”?"};
inline constexpr Tr del_note{"Это действие нельзя отменить.", "This cannot be undone."};
inline constexpr Tr del_hints{"[Enter] удалить    [Esc] отмена", "[Enter] delete    [Esc] cancel"};
inline constexpr Tr m_title{"🔑 Смена мастер-пароля", "🔑 Change master password"};
inline constexpr Tr m_new{"Новый пароль", "New password"};
inline constexpr Tr m_hints{"[Enter] сменить   [Tab] поле   [Esc] отмена", "[Enter] change   [Tab] field   [Esc] cancel"};
inline constexpr Tr m_busy{"Перешифровываю хранилище…", "Re-encrypting vault…"};
inline constexpr Tr h_title{"Справка · горячие клавиши", "Help · keyboard shortcuts"};
inline constexpr Tr h_nav{"навигация по списку", "move in list"};
inline constexpr Tr h_panels{"папки ↔ записи", "folders ↔ entries"};
inline constexpr Tr h_enter{"скопировать пароль", "copy password"};
inline constexpr Tr h_new{"новая запись", "new entry"};
inline constexpr Tr h_edit{"изменить запись", "edit entry"};
inline constexpr Tr h_del{"удалить запись", "delete entry"};
inline constexpr Tr h_user{"скопировать логин", "copy login"};
inline constexpr Tr h_totp{"скопировать код 2FA", "copy 2FA code"};
inline constexpr Tr h_show{"показать / скрыть пароль", "show / hide password"};
inline constexpr Tr h_fav{"в избранное", "toggle favorite"};
inline constexpr Tr h_open{"открыть сайт в браузере", "open website in browser"};
inline constexpr Tr h_gen{"генератор паролей", "password generator"};
inline constexpr Tr h_search{"поиск", "search"};
inline constexpr Tr h_master{"сменить мастер-пароль", "change master password"};
inline constexpr Tr h_lock{"заблокировать", "lock"};
inline constexpr Tr h_lang{"русский / English", "Русский / English"};
inline constexpr Tr h_quit{"выход", "quit"};
inline constexpr Tr h_layout{"Клавиши работают и в русской раскладке.", "Hotkeys also work with a Russian keyboard layout."};
inline constexpr Tr h_close{"[Esc] закрыть", "[Esc] close"};
inline constexpr Tr sealed{"Хранилище запечатано", "Vault sealed"};
inline constexpr Tr bye{"до встречи", "see you"};
inline constexpr Tr too_small{"Увеличьте окно терминала, нужно минимум", "Please enlarge the terminal window, minimum is"};
}  // namespace tr

}  // namespace kf
