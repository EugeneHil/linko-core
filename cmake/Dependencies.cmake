# Third-party dependencies of linko-core, pinned to exact release tarballs
# (URL + SHA256). Upgrades go through a PR plus a note in ADR-0002
# (linko-apple/docs/adr/0002-core-dependencies.md).
#
# Release tarballs with URL_HASH instead of GIT_REPOSITORY/GIT_TAG: no git
# invocation at configure time and the strongest pinning - CMake refuses the
# download if a single byte differs from the recorded SHA256.
#
# EXCLUDE_FROM_ALL keeps everything we do not link against out of the default
# build; SYSTEM keeps the dependency headers from tripping our -Werror flags.
include(FetchContent)

# GoogleTest v1.18.0 - test framework (https://github.com/google/googletest)
set(INSTALL_GTEST OFF CACHE INTERNAL "")
FetchContent_Declare(
  googletest
  URL https://github.com/google/googletest/archive/refs/tags/v1.18.0.tar.gz
  URL_HASH SHA256=6e3191c1455468b3fc35a417fb565c1c5071aee1b7e7f85e30cf48a98d37d8b5
  EXCLUDE_FROM_ALL
  SYSTEM
)
FetchContent_MakeAvailable(googletest)

# nlohmann/json v3.12.0 - JSON parser (https://github.com/nlohmann/json)
set(JSON_BuildTests OFF CACHE INTERNAL "")
set(JSON_Install OFF CACHE INTERNAL "")
FetchContent_Declare(
  nlohmann_json
  URL https://github.com/nlohmann/json/archive/refs/tags/v3.12.0.tar.gz
  URL_HASH SHA256=4b92eb0c06d10683f7447ce9406cb97cd4b453be18d7279320f7b2f025c10187
  EXCLUDE_FROM_ALL
  SYSTEM
)
FetchContent_MakeAvailable(nlohmann_json)
