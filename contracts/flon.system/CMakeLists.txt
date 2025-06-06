add_contract(
  flon.system
  flon.system
  ${CMAKE_CURRENT_SOURCE_DIR}/src/flon.system.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/src/delegate_bandwidth.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/src/finalizer_key.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/src/name_bidding.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/src/native.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/src/producer_pay.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/src/voting.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/src/limit_auth_changes.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/src/block_info.cpp)

if(SYSTEM_CONFIGURABLE_WASM_LIMITS)
  target_compile_definitions(flon.system PUBLIC SYSTEM_CONFIGURABLE_WASM_LIMITS)
endif()

if(SYSTEM_BLOCKCHAIN_PARAMETERS)
  target_compile_definitions(flon.system PUBLIC SYSTEM_BLOCKCHAIN_PARAMETERS)
endif()

target_include_directories(flon.system PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include
                                               ${CMAKE_CURRENT_SOURCE_DIR}/../flon.token/include
                                               ${CMAKE_CURRENT_SOURCE_DIR}/../flon.reward/include)

set_target_properties(flon.system PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}")

configure_file(${CMAKE_CURRENT_SOURCE_DIR}/ricardian/flon.system.contracts.md.in
               ${CMAKE_CURRENT_BINARY_DIR}/ricardian/flon.system.contracts.md @ONLY)

target_compile_options(flon.system PUBLIC -R${CMAKE_CURRENT_SOURCE_DIR}/ricardian
                                           -R${CMAKE_CURRENT_BINARY_DIR}/ricardian)

install(
  FILES
     ${CMAKE_CURRENT_BINARY_DIR}/flon.system.wasm
     ${CMAKE_CURRENT_BINARY_DIR}/flon.system.abi
  DESTINATION
     ${CMAKE_INSTALL_PREFIX}/contracts/flon.system
)
