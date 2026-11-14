# Third-party dependencies of linko-core, pinned to exact release tags.
# Upgrades go through a PR plus a note in ADR-0002
# (linko-apple/docs/adr/0002-core-dependencies.md).
#
# EXCLUDE_FROM_ALL keeps everything we do not link against out of the default
# build; SYSTEM keeps the dependency headers from tripping our -Werror flags.
include(FetchContent)

# GoogleTest v1.18.0 - test framework (https://github.com/google/googletest)
set(INSTALL_GTEST OFF CACHE INTERNAL "")
FetchContent_Declare(
  googletest
  GIT_REPOSITORY https://github.com/google/googletest.git
  GIT_TAG v1.18.0
  GIT_SHALLOW TRUE
  EXCLUDE_FROM_ALL
  SYSTEM
)
FetchContent_MakeAvailable(googletest)

# nlohmann/json v3.12.0 - JSON parser (https://github.com/nlohmann/json)
set(JSON_BuildTests OFF CACHE INTERNAL "")
set(JSON_Install OFF CACHE INTERNAL "")
FetchContent_Declare(
  nlohmann_json
  GIT_REPOSITORY https://github.com/nlohmann/json.git
  GIT_TAG v3.12.0
  GIT_SHALLOW TRUE
  EXCLUDE_FROM_ALL
  SYSTEM
)
FetchContent_MakeAvailable(nlohmann_json)
