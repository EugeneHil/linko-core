# GoogleTest suites for linko_core.
include(GoogleTest)

# linko_add_test(<name> <sources...>)
#
# Creates a GoogleTest executable <name> from <sources>, links it against
# linko_core and gtest_main, and registers every TEST it contains with CTest.
function(linko_add_test name)
  add_executable(${name} ${ARGN})
  target_link_libraries(${name} PRIVATE linko_core gtest_main)
  gtest_discover_tests(${name})
endfunction()

linko_add_test(smoke test_smoke.cpp)
