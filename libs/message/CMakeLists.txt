add_library(message STATIC message.cpp)

target_include_directories(
    message
    PUBLIC ${CMAKE_CURRENT_SOURCE_DIR} ${PROJECT_SOURCE_DIR}/src)

target_link_libraries(message
PUBLIC
fmt::fmt
Drogon::Drogon)
