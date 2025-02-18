add_contract(flon.bpay flon.bpay ${CMAKE_CURRENT_SOURCE_DIR}/src/flon.bpay.cpp)

target_include_directories(flon.bpay PUBLIC
   ${CMAKE_CURRENT_SOURCE_DIR}/include
   ${CMAKE_CURRENT_SOURCE_DIR}/../flon.system/include
   ${CMAKE_CURRENT_SOURCE_DIR}/../flon.token/include)

set_target_properties(flon.bpay
   PROPERTIES
   RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}")

configure_file( ${CMAKE_CURRENT_SOURCE_DIR}/ricardian/flon.bpay.contracts.md.in ${CMAKE_CURRENT_BINARY_DIR}/ricardian/flon.bpay.contracts.md @ONLY )

target_compile_options( flon.bpay PUBLIC -R${CMAKE_CURRENT_SOURCE_DIR}/ricardian -R${CMAKE_CURRENT_BINARY_DIR}/ricardian )

install(
   FILES
      ${CMAKE_CURRENT_BINARY_DIR}/flon.bpay.wasm
      ${CMAKE_CURRENT_BINARY_DIR}/flon.bpay.abi
   DESTINATION
      ${CMAKE_INSTALL_PREFIX}/contracts/flon.bpay
)