// Laycue brand tokens for the panel (see assets/brand/README.md). Every colour, size and
// font the dock uses comes from here; widgets never hardcode their own values.
#pragma once

#include <QColor>
#include <QFont>
#include <QString>

namespace laycue::ui {

namespace color {
inline const QColor ink{0x15, 0x17, 0x1A};           // panel base
inline const QColor graphite{0x1E, 0x20, 0x24};      // raised surfaces (cards)
inline const QColor stage{0x28, 0x2B, 0x30};         // the most raised surface: Live, Up next and the main buttons
inline const QColor recess{0x0E, 0x0F, 0x11};        // sunken tracks (tab bar, switches off)
inline const QColor control{0x30, 0x34, 0x3A};       // selected segment, quiet buttons
inline const QColor graphiteHover{0x25, 0x28, 0x2D}; // card under the mouse
inline const QColor line{0x30, 0x34, 0x3A};          // dividers, quiet outlines
inline const QColor lineStrong{0x42, 0x46, 0x4D};    // secondary button outline
inline const QColor cream{0xF1, 0xEB, 0xDD};         // text, output, primary actions
inline const QColor textSecondary{0xB4, 0xB8, 0xBF};
inline const QColor textTertiary{0x86, 0x8B, 0x94};
inline const QColor textDisabled{0x6F, 0x6A, 0x61};
inline const QColor live{0xFF, 0x4D, 0x2E}; // Tally Red: on air, only
inline const QColor livePressed{0xE6, 0x43, 0x28};
inline const QColor next{0x2F, 0xD1, 0x8B}; // Cue Green: up next
inline const QColor nextTint{0x13, 0x24, 0x1B};
inline const QColor overlay{0xFF, 0xB3, 0x1A}; // Overlay Amber: overlays and notices
inline const QColor setup{0x2E, 0x8B, 0xFF};   // Signal Blue: setup, edit, focus
} // namespace color

namespace size {
constexpr int radius = 8;         // controls
constexpr int pictureRadius = 10; // Live / Up next pictures
constexpr int touch = 44;         // minimum height of anything tappable during a service
constexpr int bigButton = 52;     // Go Live / Cut
constexpr int gapSmall = 8;
constexpr int gap = 14;
constexpr int gapLarge = 24;
constexpr int margin = 12;
constexpr int controlHeight = 32; // desktop density: buttons, rows, tabs
constexpr int shadowMargin = 8;   // room a raised surface needs for its soft shadow
} // namespace size

// Registers the bundled Archivo and JetBrains Mono fonts. Safe to call more than once;
// if a font file is missing the panel falls back to the system UI font.
void loadBrandFonts();

// Archivo at a pixel size and weight (400 regular … 700 bold).
QFont uiFont(int pixelSize, int weight = 400);

// The Archivo family name (or "" if it failed to load), for the panel style sheet: OBS
// themes set a font family on every widget, which would otherwise win over setFont().
QString uiFamily();

// JetBrains Mono, for hotkeys and technical details.
QFont monoFont(int pixelSize);

// Style sheet for a label: its colour plus its font size and weight. OBS themes set font
// sizes in their style sheets, which would otherwise win over setFont().
QString labelStyle(const QColor &color, const QFont &font);

// "#RRGGBB" for use in the few Qt style sheets the dock needs.
QString hex(const QColor &color);

} // namespace laycue::ui
