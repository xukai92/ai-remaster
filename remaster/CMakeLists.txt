cmake_minimum_required(VERSION 3.16)
project(remaster_core LANGUAGES CXX)

add_library(remaster-core Image.cpp TextureIdentity.cpp FakeBackend.cpp JobScheduler.cpp
  PngArtifactCache.cpp)
target_compile_features(remaster-core PUBLIC cxx_std_17)
target_include_directories(remaster-core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/..)
find_package(Threads REQUIRED)
find_package(PNG REQUIRED)
target_link_libraries(remaster-core PUBLIC Threads::Threads PNG::PNG)

include(CTest)
if(BUILD_TESTING)
  add_executable(remaster-core-tests tests/CoreTests.cpp)
  target_link_libraries(remaster-core-tests PRIVATE remaster-core)
  add_test(NAME remaster-core-tests COMMAND remaster-core-tests)
  add_executable(remaster-cache-tests tests/CacheTests.cpp)
  target_link_libraries(remaster-cache-tests PRIVATE remaster-core)
  add_test(NAME remaster-cache-unit COMMAND remaster-cache-tests unit)
  add_test(NAME remaster-cache-process COMMAND ${CMAKE_COMMAND}
    -DTEST_EXEC=$<TARGET_FILE:remaster-cache-tests>
    -DROOT=${CMAKE_CURRENT_BINARY_DIR}/cache-process-fixture
    -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/RunCacheProcess.cmake)
endif()
