# Local libraries and final link dependencies for server_core.

# Link libraries (PUBLIC: static lib deps are propagated to consumers)
add_subdirectory(${CMAKE_SOURCE_DIR}/libs)
list(APPEND PACKAGE_LIBS concurrency configurator logger message )
target_link_libraries(server_core PUBLIC ${PACKAGE_LIBS})
