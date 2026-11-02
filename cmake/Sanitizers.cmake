# Global sanitizer flags, selected with -DMD_SANITIZER=asan|tsan.
# ASAN and TSAN cannot be combined in one binary, so they are separate builds.
if(MD_SANITIZER STREQUAL "asan")
  add_compile_options(-fsanitize=address,undefined -fno-omit-frame-pointer -fno-sanitize-recover=all)
  add_link_options(-fsanitize=address,undefined)
elseif(MD_SANITIZER STREQUAL "tsan")
  add_compile_options(-fsanitize=thread -fno-omit-frame-pointer)
  add_link_options(-fsanitize=thread)
elseif(NOT MD_SANITIZER STREQUAL "")
  message(FATAL_ERROR "Unknown MD_SANITIZER '${MD_SANITIZER}' (use asan or tsan)")
endif()
