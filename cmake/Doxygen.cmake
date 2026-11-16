# Doxygen reference: expose the existing Doxyfile as a build target. The Doxyfile
# resolves every path relative to the repository root (OUTPUT_DIRECTORY =
# docs/doxygen, INPUT = src + the Markdown docs), so the command must run there,
# and that output directory must exist before Doxygen writes its WARN_LOGFILE.
# The generated site is published under docs/ alongside the API docs; Doxygen
# excludes docs/doxygen from INPUT so it never documents its own output.
# Skipped with a status note when doxygen is not installed; disable entirely
# with -DVALHALLA_BUILD_DOXYGEN=OFF.
option(VALHALLA_BUILD_DOXYGEN "Generate the Doxygen reference from Doxyfile" ON)
if(VALHALLA_BUILD_DOXYGEN)
  find_package(Doxygen QUIET)
  if(DOXYGEN_FOUND)
    add_custom_target(valhalla_doxygen ALL
      COMMAND ${CMAKE_COMMAND} -E make_directory ${CMAKE_SOURCE_DIR}/docs/doxygen
      COMMAND ${DOXYGEN_EXECUTABLE} ${CMAKE_SOURCE_DIR}/Doxyfile
      WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
      COMMENT "Generating Doxygen reference in docs/doxygen"
      VERBATIM)
  else()
    message(STATUS "Doxygen not found; skipping valhalla_doxygen target")
  endif()
endif()
