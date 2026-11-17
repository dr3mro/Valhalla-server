# Embed docs/api.html into a generated header at configure time.

# API docs page (docs/api.html) embedded into the binary as a
# constexpr string_view. The header
#  ${CMAKE_BINARY_DIR}/generated/api_html.hpp
#  (included by src/api/v2/routes/doc.hpp) is created now so it
# exists for tooling, and refreshed on every build by
# the valhalla_docs target below.
#
#
include(${CMAKE_SOURCE_DIR}/tools/cmake/embed_html.cmake)
embed_html(
  INPUT docs/api.html
  OUTPUT ${CMAKE_BINARY_DIR}/generated/api_html.hpp
  VARIABLE kApiHtml
  NAMESPACE api::v2::docs)
target_include_directories(server_core PUBLIC ${CMAKE_BINARY_DIR}/generated)
