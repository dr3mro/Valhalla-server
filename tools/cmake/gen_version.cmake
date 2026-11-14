# Writes ${BINARY_DIR}/version.cpp from src/version.cpp.in with the current tag.
#
# Runs at configure time (include()d from CMakeLists.txt, so the file exists)
# and on every build (the valhalla_version target runs it with cmake -P), so
# a new commit or tag is picked up without reconfiguring. configure_file only
# rewrites the file when the tag changed, so an unchanged tag rebuilds nothing.
#
# Inputs: SOURCE_DIR, BINARY_DIR. get_tag.sh uses $VALHALLA_VERSION if set
# (Docker builds have no .git), otherwise git describe.
execute_process(
  COMMAND "${SOURCE_DIR}/get_tag.sh"
  WORKING_DIRECTORY "${SOURCE_DIR}"
  OUTPUT_VARIABLE _valhalla_tag_macro
  OUTPUT_STRIP_TRAILING_WHITESPACE)
string(STRIP "${_valhalla_tag_macro}" _valhalla_tag_macro)

# get_tag.sh prints -DGIT_TAG="<value>"; keep just <value>.
if(_valhalla_tag_macro MATCHES "GIT_TAG=\"([^\"]+)\"")
  set(GIT_TAG_VALUE "${CMAKE_MATCH_1}")
else()
  set(GIT_TAG_VALUE "unknown")
endif()

configure_file("${SOURCE_DIR}/src/version.cpp.in" "${BINARY_DIR}/version.cpp" @ONLY)
