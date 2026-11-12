# Run with: cmake -DSOURCE_DIR=<repo> -DBINARY_DIR=<build> -P tools/cmake/build_docs.cmake
# Regenerates docs/ via tools/docs/build.sh (when bash, npm and python3 exist), then re-embeds docs/api.html
# into ${BINARY_DIR}/generated/api_html.hpp. The header is only rewritten when the page changed, so an
# unchanged page does not trigger a recompile. A missing tool or a failed generator falls back to the
# committed docs/api.html.
find_program(VALHALLA_BASH bash)
find_program(VALHALLA_NPM npm)
find_program(VALHALLA_PYTHON python3)
if(VALHALLA_BASH AND VALHALLA_NPM AND VALHALLA_PYTHON)
  execute_process(
    COMMAND "${VALHALLA_BASH}" tools/docs/build.sh
    WORKING_DIRECTORY "${SOURCE_DIR}"
    RESULT_VARIABLE result
    OUTPUT_VARIABLE out
    ERROR_VARIABLE out)
  if(NOT result EQUAL 0)
    message(WARNING "API docs generation failed, embedding docs/api.html as committed:\n${out}")
  endif()
else()
  message(STATUS "API docs not regenerated (bash, npm and python3 are required); embedding docs/api.html as is")
endif()

include("${SOURCE_DIR}/tools/cmake/embed_html.cmake")
embed_html(
  INPUT "${SOURCE_DIR}/docs/api.html"
  OUTPUT "${BINARY_DIR}/generated/api_html.hpp"
  VARIABLE kApiHtml
  NAMESPACE api::v2::docs)
