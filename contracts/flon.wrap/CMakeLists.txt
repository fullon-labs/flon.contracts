add_contract(flon.wrap flon.wrap ${CMAKE_CURRENT_SOURCE_DIR}/src/flon.wrap.cpp)

target_include_directories(flon.wrap
   PUBLIC
   ${CMAKE_CURRENT_SOURCE_DIR}/include)

set_target_properties(flon.wrap
   PROPERTIES
   RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}")

configure_file( ${CMAKE_CURRENT_SOURCE_DIR}/ricardian/flon.wrap.contracts.md.in ${CMAKE_CURRENT_BINARY_DIR}/ricardian/flon.wrap.contracts.md @ONLY )

target_compile_options( flon.wrap PUBLIC -R${CMAKE_CURRENT_SOURCE_DIR}/ricardian -R${CMAKE_CURRENT_BINARY_DIR}/ricardian )

install(
   FILES
      ${CMAKE_CURRENT_BINARY_DIR}/flon.wrap.wasm
      ${CMAKE_CURRENT_BINARY_DIR}/flon.wrap.abi
   DESTINATION
      ${CMAKE_INSTALL_PREFIX}/contracts/flon.wrap
)