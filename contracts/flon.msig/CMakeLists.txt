add_contract(flon.msig flon.msig ${CMAKE_CURRENT_SOURCE_DIR}/src/flon.msig.cpp)

target_include_directories(flon.msig
   PUBLIC
   ${CMAKE_CURRENT_SOURCE_DIR}/include)

set_target_properties(flon.msig
   PROPERTIES
   RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}")

configure_file( ${CMAKE_CURRENT_SOURCE_DIR}/ricardian/flon.msig.contracts.md.in ${CMAKE_CURRENT_BINARY_DIR}/ricardian/flon.msig.contracts.md @ONLY )

target_compile_options( flon.msig PUBLIC -R${CMAKE_CURRENT_SOURCE_DIR}/ricardian -R${CMAKE_CURRENT_BINARY_DIR}/ricardian )

install(
   FILES
      ${CMAKE_CURRENT_BINARY_DIR}/flon.msig.wasm
      ${CMAKE_CURRENT_BINARY_DIR}/flon.msig.abi
   DESTINATION
      ${CMAKE_INSTALL_PREFIX}/contracts/flon.msig
)