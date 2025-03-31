cmake_minimum_required(VERSION 3.5)

project(eosio_contracts)

set(VERSION_MAJOR 3)
set(VERSION_MINOR 6)
set(VERSION_PATCH 0)
# set(VERSION_SUFFIX "")

if(VERSION_SUFFIX)
  set(VERSION_FULL "${VERSION_MAJOR}.${VERSION_MINOR}.${VERSION_PATCH}-${VERSION_SUFFIX}")
else()
  set(VERSION_FULL "${VERSION_MAJOR}.${VERSION_MINOR}.${VERSION_PATCH}")
endif()

message(STATUS "Building eos-system-contracts v${VERSION_FULL}")

include(ExternalProject)

find_package(flon.cdt)

option(SYSTEM_CONFIGURABLE_WASM_LIMITS
       "Enables use of the host functions activated by the CONFIGURABLE_WASM_LIMITS protocol feature" ON)

option(SYSTEM_BLOCKCHAIN_PARAMETERS
       "Enables use of the host functions activated by the BLOCKCHAIN_PARAMETERS protocol feature" ON)

option(SYSTEM_ENABLE_FULLON_VERSION_CHECK
      "Enables a configure-time check that the version of fullon's tester library is compatible with this project's unit tests" ON)

option(SYSTEM_ENABLE_CDT_VERSION_CHECK
      "Enables a configure-time check that the version of CDT is compatible with this project's contracts" ON)

option(BUILD_TESTS "Build unit tests" OFF)

ExternalProject_Add(
  contracts_project
  SOURCE_DIR ${CMAKE_SOURCE_DIR}/contracts
  BINARY_DIR ${CMAKE_BINARY_DIR}/contracts
  INSTALL_DIR ${CMAKE_CURRENT_BINARY_DIR}/install
  CMAKE_ARGS -DCMAKE_BUILD_TYPE=Release
             -DCMAKE_TOOLCHAIN_FILE=${CDT_TOOLCHAIN_FILE}
             -DSYSTEM_CONFIGURABLE_WASM_LIMITS=${SYSTEM_CONFIGURABLE_WASM_LIMITS}
             -DSYSTEM_BLOCKCHAIN_PARAMETERS=${SYSTEM_BLOCKCHAIN_PARAMETERS}
             -DCMAKE_INSTALL_PREFIX:PATH=<INSTALL_DIR>
             -DBUILD_TESTS=${BUILD_TESTS}
             -DSYSTEM_ENABLE_CDT_VERSION_CHECK=${SYSTEM_ENABLE_CDT_VERSION_CHECK}
  UPDATE_COMMAND ""
  PATCH_COMMAND ""
  TEST_COMMAND ""
  INSTALL_COMMAND ""
  BUILD_ALWAYS 1)

# Actually install subproject.
install(
  DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}/install/
  DESTINATION "${CMAKE_INSTALL_PREFIX}"
  USE_SOURCE_PERMISSIONS # Remain permissions (rwx) for installed files
)

option(BUILD_TESTS "Build unit tests" OFF)

if(BUILD_TESTS)
  message(STATUS "Building unit tests.")
  add_subdirectory(tests)
else()
  message(STATUS "Unit tests will not be built. To build unit tests, set BUILD_TESTS to ON.")
endif()
