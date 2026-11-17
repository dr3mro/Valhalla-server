# Enable AddressSanitizer only for Debug build type on Linux for now.
# Applied globally so the server binary, server_core and the unit tests all
# get instrumented (ASan/UBSan/LSan enforced on test runs on Linux Debug).
# NOTE: kept at its original position in the root build; see the split plan
# for the separate fix that moves these flags before target creation.
if(CMAKE_BUILD_TYPE STREQUAL "Debug")
  if(LINUX)
    message("Enabling AddressSanitizer for Debug builds on Linux")
    add_compile_options(-fsanitize=address -fsanitize=undefined -fsanitize=leak
                        -fno-sanitize=thread -fno-omit-frame-pointer)
    add_link_options(-fsanitize=address -fsanitize=undefined -fsanitize=leak
                     -fno-sanitize=thread -fno-omit-frame-pointer)
  endif()
endif()
