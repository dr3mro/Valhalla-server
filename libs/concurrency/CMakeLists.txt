add_library(concurrency STATIC boundedexecutor.cpp)

target_include_directories(
    concurrency
    PUBLIC ${CMAKE_CURRENT_SOURCE_DIR} ${CMAKE_SOURCE_DIR}/src)

target_link_libraries(concurrency PUBLIC fmt::fmt Trantor::Trantor)
