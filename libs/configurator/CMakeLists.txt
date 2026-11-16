add_library(configurator STATIC
configurator.cpp
envloader/envloader.cpp
envloader/envvars/envvars.cpp)

target_include_directories(
    configurator
    PUBLIC ${CMAKE_CURRENT_SOURCE_DIR} ${PROJECT_SOURCE_DIR}/src)

target_link_libraries(configurator
PUBLIC
fmt::fmt
message)
