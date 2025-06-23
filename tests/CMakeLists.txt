cmake_minimum_required(VERSION 3.5)

set(CHAIN_CORE_VERSION_MIN "0.5")
set(CHAIN_CORE_VERSION_SOFT_MAX "1.0")
# set(CHAIN_CORE_VERSION_HARD_MAX "")

find_package(flon-core REQUIRED)

# Check the version of fullon
if(SYSTEM_ENABLE_CHAIN_CORE_VERSION_CHECK)
  set(VERSION_MATCH_ERROR_MSG "")
  chain_core_check_version(VERSION_OUTPUT "${CHAIN_CORE_VERSION}" "${CHAIN_CORE_VERSION_MIN}" "${CHAIN_CORE_VERSION_SOFT_MAX}"
                      "${CHAIN_CORE_VERSION_HARD_MAX}" VERSION_MATCH_ERROR_MSG)
  if(VERSION_OUTPUT STREQUAL "MATCH")
    message(STATUS "Using chain core version ${CHAIN_CORE_VERSION}")
  elseif(VERSION_OUTPUT STREQUAL "WARN")
    message(
      WARNING
        "Using chain core version ${CHAIN_CORE_VERSION} even though it exceeds the maximum supported version of ${CHAIN_CORE_VERSION_SOFT_MAX}; continuing with configuration, however build may fail.\nIt is recommended to use chain core version ${CHAIN_CORE_VERSION_SOFT_MAX}.x"
    )
  else() # INVALID OR MISMATCH
    message(
      FATAL_ERROR
        "Found chain core version ${CHAIN_CORE_VERSION} but it does not satisfy version requirements: ${VERSION_MATCH_ERROR_MSG}\nPlease use chain core version ${CHAIN_CORE_VERSION_SOFT_MAX}.x"
    )
  endif(VERSION_OUTPUT STREQUAL "MATCH")
endif()

configure_file(${CMAKE_CURRENT_SOURCE_DIR}/contracts.hpp.in ${CMAKE_CURRENT_BINARY_DIR}/contracts.hpp)

include_directories(${CMAKE_CURRENT_BINARY_DIR})
# UNIT TESTING ###
include(CTest) # eliminates DartConfiguration.tcl errors at test runtime
enable_testing()
# build unit test executable
file(GLOB UNIT_TESTS "*.cpp" "*.hpp") # find all unit test suites
add_chain_core_test_executable(unit_test ${UNIT_TESTS}) # build unit tests as one executable
# mark test suites for execution
foreach(TEST_SUITE ${UNIT_TESTS}) # create an independent target for each test suite
  execute_process(
    COMMAND
      bash -c
      "grep -E 'BOOST_AUTO_TEST_SUITE\\s*[(]' ${TEST_SUITE} | grep -vE '//.*BOOST_AUTO_TEST_SUITE\\s*[(]' | cut -d ')' -f 1 | cut -d '(' -f 2"
      OUTPUT_VARIABLE SUITE_NAMES
    OUTPUT_STRIP_TRAILING_WHITESPACE) # get all the test suite names from each *.cpp file
  string(REPLACE "\n" ";" SUITE_NAMES "${SUITE_NAMES}")
  foreach(SUITE_NAME IN LISTS SUITE_NAMES)
    if(NOT "" STREQUAL "${SUITE_NAME}") # ignore empty lines
      execute_process(
        COMMAND bash -c "echo ${SUITE_NAME} | sed -e 's/s$//' | sed -e 's/_test$//'"
        OUTPUT_VARIABLE TRIMMED_SUITE_NAME
        OUTPUT_STRIP_TRAILING_WHITESPACE) # trim "_test" or "_tests" from the end of ${SUITE_NAME}
      # to run unit_test with all log from blockchain displayed, put "--verbose" after "--", i.e. "unit_test -- --verbose"
      add_test(NAME ${TRIMMED_SUITE_NAME}_unit_test COMMAND unit_test --run_test=${SUITE_NAME} --report_level=detailed
                                                            --color_output)
    endif()
  endforeach(SUITE_NAME)
endforeach(TEST_SUITE)
