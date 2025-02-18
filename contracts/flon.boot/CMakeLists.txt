add_contract(flon.boot flon.boot ${CMAKE_CURRENT_SOURCE_DIR}/src/flon.boot.cpp)

target_include_directories(flon.boot
   PUBLIC
   ${CMAKE_CURRENT_SOURCE_DIR}/include)

set_target_properties(flon.boot
   PROPERTIES
   RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}")

configure_file( ${CMAKE_CURRENT_SOURCE_DIR}/ricardian/flon.boot.contracts.md.in ${CMAKE_CURRENT_BINARY_DIR}/ricardian/flon.boot.contracts.md @ONLY )

target_compile_options( flon.boot PUBLIC -R${CMAKE_CURRENT_SOURCE_DIR}/ricardian -R${CMAKE_CURRENT_BINARY_DIR}/ricardian )

install(
   FILES
      ${CMAKE_CURRENT_BINARY_DIR}/flon.boot.wasm
      ${CMAKE_CURRENT_BINARY_DIR}/flon.boot.abi
   DESTINATION
      ${CMAKE_INSTALL_PREFIX}/contracts/flon.boot
)