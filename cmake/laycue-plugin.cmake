# laycue_add_plugin(<name> SOURCES <files...> [LIBS <libs...>])
#
# Makes one OBS plugin of the Laycue pack: a MODULE named <name> (e.g. laycue-scenes) with its own
# plugin-support, linked to libobs, the frontend API, Qt and the shared laycue-common library.
# Call it from the plugin's own folder (plugins/<x>/CMakeLists.txt): that folder's data/ directory
# is installed with the plugin, together with plugins/common/data (fonts, brand images).

include_guard(GLOBAL)

set(LAYCUE_COMMON_DATA_DIR "${CMAKE_SOURCE_DIR}/plugins/common/data")

function(laycue_add_plugin name)
  cmake_parse_arguments(PARSE_ARGV 1 ARG "" "" "SOURCES;LIBS")

  add_library(${name} MODULE)

  set(PLUGIN_TARGET_NAME ${name})
  configure_file(
    "${CMAKE_SOURCE_DIR}/plugins/common/plugin-support.c.in"
    "${CMAKE_CURRENT_BINARY_DIR}/${name}-support.c"
    @ONLY
  )

  target_sources(${name} PRIVATE ${ARG_SOURCES} "${CMAKE_CURRENT_BINARY_DIR}/${name}-support.c")
  target_include_directories(${name} PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src")
  target_link_libraries(${name} PRIVATE OBS::libobs OBS::obs-frontend-api laycue-common ${ARG_LIBS})
  target_compile_options(
    ${name}
    PRIVATE $<$<C_COMPILER_ID:Clang,AppleClang>:-Wno-quoted-include-in-framework-header -Wno-comma>
  )
  set_target_properties(
    ${name}
    PROPERTIES AUTOMOC ON AUTOUIC ON AUTORCC ON
  )

  set_target_properties_plugin(${name} PROPERTIES OUTPUT_NAME ${name})
endfunction()
