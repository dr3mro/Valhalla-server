add_library(logger STATIC logger.cpp)

target_include_directories(
    logger
    PUBLIC ${CMAKE_CURRENT_SOURCE_DIR} ${PROJECT_SOURCE_DIR}/src)

target_link_libraries(logger
PUBLIC
fmt::fmt
Drogon::Drogon)
