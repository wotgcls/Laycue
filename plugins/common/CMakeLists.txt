# laycue-common: the pack's shared look and feel. A static library linked into each plugin, so the
# plugins stay independent (nothing is shared at run time).

add_library(laycue-common STATIC)

target_sources(
  laycue-common
  PRIVATE
    src/core/update-info.cpp
    src/core/xml-lite.cpp
    src/core/xml-lite.hpp
    src/core/update-info.hpp
    src/hotkey-set.cpp
    src/hotkey-set.hpp
    src/net/http.cpp
    src/net/http.hpp
    src/obs/scene-presence.cpp
    src/obs/scene-presence.hpp
    src/ui/brand-mark.cpp
    src/ui/brand-mark.hpp
    src/slides/slide-control.cpp
    src/slides/slide-control.hpp
    src/slides/slide-render.cpp
    src/slides/slide-render.hpp
    src/slides/slide-source.cpp
    src/slides/slide-source.hpp
    src/slides/slide-style-io.cpp
    src/slides/slide-style-io.hpp
    src/slides/slide-style.cpp
    src/slides/slide-style.hpp
    src/slides/style-editor.cpp
    src/slides/style-editor.hpp
    src/ui/controls.cpp
    src/ui/controls.hpp
    src/ui/laycue-menu.cpp
    src/ui/laycue-menu.hpp
    src/ui/pack-switcher.cpp
    src/ui/pack-switcher.hpp
    src/ui/pages/helpers.hpp
    src/ui/presence-text.cpp
    src/ui/presence-text.hpp
    src/ui/pages/page-frame.cpp
    src/ui/pages/page-frame.hpp
    src/ui/style.cpp
    src/ui/style.hpp
    src/ui/text.cpp
    src/ui/text.hpp
    src/ui/tokens.cpp
    src/ui/tokens.hpp
    src/ui/stage-splitter.cpp
    src/ui/stage-splitter.hpp
    src/ui/update-card.cpp
    src/ui/update-card.hpp
    src/update/updater.cpp
    src/update/updater.hpp
)

if(WIN32)
  # Windows' own secure web client, used when OBS's Qt has no TLS part (part of Windows, not a download).
  target_sources(laycue-common PRIVATE src/net/http-win.cpp)
  target_link_libraries(laycue-common PUBLIC winhttp)
endif()

target_include_directories(laycue-common PUBLIC src include)
target_link_libraries(
  laycue-common
  PUBLIC OBS::libobs OBS::obs-frontend-api Qt6::Core Qt6::Gui Qt6::Network Qt6::Widgets
)
set_target_properties(laycue-common PROPERTIES POSITION_INDEPENDENT_CODE ON AUTOMOC ON)
target_compile_options(
  laycue-common
  PRIVATE $<$<C_COMPILER_ID:Clang,AppleClang>:-Wno-quoted-include-in-framework-header -Wno-comma>
)
