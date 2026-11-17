# Regenerate docs/api.html from tools/docs before compiling the server.

# Before compiling the server, regenerate
# docs/ with tools/docs/build.sh
# (needs bash, npm, python3; skipped with a
# note if any is missing, in which case
# the committed docs/api.html is embedded).
# Disable: -DVALHALLA_BUILD_DOCS=OFF.
#
option(VALHALLA_BUILD_DOCS
"Regenerate docs/api.html from tools/docs on every build" ON)
if(VALHALLA_BUILD_DOCS)
  add_custom_target(
    valhalla_docs
    COMMAND ${CMAKE_COMMAND}
    -DSOURCE_DIR=${CMAKE_SOURCE_DIR} -DBINARY_DIR=${CMAKE_BINARY_DIR} -P
            ${CMAKE_SOURCE_DIR}/tools/cmake/build_docs.cmake
    BYPRODUCTS ${CMAKE_BINARY_DIR}/generated/api_html.hpp
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
    COMMENT "Generating API docs (tools/docs/build.sh)
    and embedding docs/api.html"
    VERBATIM)
  # server_core (and so the server and the tests)
  # cannot start compiling until the docs are generated and embedded.
  add_dependencies(server_core valhalla_docs)
endif()
