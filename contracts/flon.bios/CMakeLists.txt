add_contract(flon.bios flon.bios ${CMAKE_CURRENT_SOURCE_DIR}/src/flon.bios.cpp)

target_include_directories(flon.bios
   PUBLIC
   ${CMAKE_CURRENT_SOURCE_DIR}/include)

set_target_properties(flon.bios
   PROPERTIES
   RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}")

configure_file( ${CMAKE_CURRENT_SOURCE_DIR}/ricardian/flon.bios.contracts.md.in ${CMAKE_CURRENT_BINARY_DIR}/ricardian/flon.bios.contracts.md @ONLY )

target_compile_options( flon.bios PUBLIC -R${CMAKE_CURRENT_SOURCE_DIR}/ricardian -R${CMAKE_CURRENT_BINARY_DIR}/ricardian )

install(
   FILES
      ${CMAKE_CURRENT_BINARY_DIR}/flon.bios.wasm
      ${CMAKE_CURRENT_BINARY_DIR}/flon.bios.abi
   DESTINATION
      ${CMAKE_INSTALL_PREFIX}/contracts/flon.bios
)