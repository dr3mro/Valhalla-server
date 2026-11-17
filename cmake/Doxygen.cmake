# Doxygen reference: expose the Doxyfile as a build target. The Doxyfile
# resolves every path relative to the repository root (OUTPUT_DIRECTORY =
# docs/doxygen, INPUT = src + the Markdown docs), so the command must run there,
# and that output directory must exist before Doxygen writes its WARN_LOGFILE.
# The generated site is published under docs/ alongside the API docs; Doxygen
# excludes docs/doxygen from INPUT so it never documents its own output.
# Skipped with a status note when doxygen is not installed or when HEAD has no
# git tag (docs are generated only for tagged commits); disable entirely with
# -DVALHALLA_BUILD_DOXYGEN=OFF.
option(VALHALLA_BUILD_DOXYGEN "Generate the Doxygen reference from Doxyfile" ON)
if(VALHALLA_BUILD_DOXYGEN)
  # A tag is "at HEAD" when git tag --points-at HEAD lists one. Using describe
  # here would also match untagged commits that merely descend from a tag.
  execute_process(
    COMMAND git tag --points-at HEAD
    WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
    OUTPUT_VARIABLE _doxygen_head_tags
    OUTPUT_STRIP_TRAILING_WHITESPACE
    ERROR_QUIET
    RESULT_VARIABLE _doxygen_git_result)

  # Notice tags created after the initial configure without a manual rerun.
  foreach(_doxygen_tag_ref IN ITEMS "${CMAKE_SOURCE_DIR}/.git/refs/tags"
                                    "${CMAKE_SOURCE_DIR}/.git/packed-refs")
    if(EXISTS "${_doxygen_tag_ref}")
      set_property(DIRECTORY APPEND
      PROPERTY CMAKE_CONFIGURE_DEPENDS "${_doxygen_tag_ref}")
    endif()
  endforeach()

  if(_doxygen_git_result EQUAL 0 AND NOT "${_doxygen_head_tags}" STREQUAL "")
    find_package(Doxygen QUIET)
    if(DOXYGEN_FOUND)
      add_custom_target(valhalla_doxygen ALL
        COMMAND ${CMAKE_COMMAND} -E make_directory
        ${CMAKE_SOURCE_DIR}/docs/doxygen

        COMMAND ${DOXYGEN_EXECUTABLE} ${CMAKE_SOURCE_DIR}/Doxyfile
        WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
        COMMENT "Generating Doxygen reference in docs/doxygen (tag:
        ${_doxygen_head_tags})"
        VERBATIM)
    else()
      message(STATUS "Doxygen not found; skipping valhalla_doxygen target")
    endif()
  else()
    message(STATUS "HEAD has no git tag; skipping valhalla_doxygen target")
  endif()
  unset(_doxygen_head_tags)
  unset(_doxygen_git_result)
  unset(_doxygen_tag_ref)
endif()
