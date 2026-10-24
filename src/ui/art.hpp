#pragma once
// ASCII art. Every line of a piece has the same width so it can be centred as a block.

#include <array>
#include <string_view>

namespace kf::art {

inline constexpr std::array<std::string_view, 10> kFox = {
    " ▄                 ▄ ",
    " █▙               ▟█ ",
    " ███▙▄▄▄▄▄▄▄▄▄▄▄▟███ ",
    " ▜█████████████████▛ ",
    "  ███▀▀▀█████▀▀▀███  ",
    "  ▜██ ▄▄ ███ ▄▄ ██▛  ",
    "   ▜█▄▄▄█████▄▄▄█▛   ",
    "     ▀▜███████▛▀     ",
    "       ▜█████▛       ",
    "         ▀▀▀         ",
};

inline constexpr std::array<std::string_view, 6> kLogo = {
    "██╗  ██╗███████╗██╗   ██╗███████╗ ██████╗ ██╗  ██╗",
    "██║ ██╔╝██╔════╝╚██╗ ██╔╝██╔════╝██╔═══██╗╚██╗██╔╝",
    "█████╔╝ █████╗   ╚████╔╝ █████╗  ██║   ██║ ╚███╔╝ ",
    "██╔═██╗ ██╔══╝    ╚██╔╝  ██╔══╝  ██║   ██║ ██╔██╗ ",
    "██║  ██╗███████╗   ██║   ██║     ╚██████╔╝██╔╝ ██╗",
    "╚═╝  ╚═╝╚══════╝   ╚═╝   ╚═╝      ╚═════╝ ╚═╝  ╚═╝",
};

// Padlock frames, 7 rows x 11 columns.
inline constexpr std::array<std::string_view, 7> kLockClosed = {
    "           ",
    "  ╭─────╮  ",
    "  │     │  ",
    "╭─┴─────┴─╮",
    "│   ▐█▌   │",
    "│   ▟█▙   │",
    "╰─────────╯",
};

inline constexpr std::array<std::string_view, 7> kLockLifted = {
    "  ╭─────╮  ",
    "  │     │  ",
    "  │     │  ",
    "╭─┴─────┴─╮",
    "│   ▐█▌   │",
    "│   ▟█▙   │",
    "╰─────────╯",
};

inline constexpr std::array<std::string_view, 7> kLockOpen = {
    "  ╭─────╮  ",
    "  │     │  ",
    "  │        ",
    "╭─┴───────╮",
    "│   ▐█▌   │",
    "│   ▟█▙   │",
    "╰─────────╯",
};

}  // namespace kf::art
