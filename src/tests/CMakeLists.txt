set(NAME FpsAimForgeTests)

file(GLOB_RECURSE TEST_SOURCES
  "aim_db_test.cc"
  "bundle_functional_test.cc"
  "geometry_test.cc"
  "name_util_test.cc"
  "playlist_manager_test.cc"
  "guide_manager_test.cc"
  "scenario_manager_test.cc"
  "textures_test.cc"
  "times_test.cc"
  "util_test.cc"
  "collections_test.cc"
  "proto_util_test.cc"
)

# Add the test executable
add_executable(${NAME} ${TEST_SOURCES})

# Link your test executable to your library and GTest
target_link_libraries(
    ${NAME}
    GTest::gmock_main
    GTest::gmock
    protobuf-matchers
    aim
)

if(MSVC)
    # Disables warnings from protobuf lib
    target_compile_options(${NAME} PRIVATE /wd4091)
endif()

include(GoogleTest)
gtest_discover_tests(${NAME})
