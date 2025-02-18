add_contract(flon.fees flon.fees ${CMAKE_CURRENT_SOURCE_DIR}/src/flon.fees.cpp)

target_include_directories(flon.fees PUBLIC
   ${CMAKE_CURRENT_SOURCE_DIR}/include
   ${CMAKE_CURRENT_SOURCE_DIR}/../flon.system/include)

set_target_properties(flon.fees
   PROPERTIES
   RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}")

target_compile_options( flon.fees PUBLIC )

install(
   FILES
      ${CMAKE_CURRENT_BINARY_DIR}/flon.fees.wasm
      ${CMAKE_CURRENT_BINARY_DIR}/flon.fees.abi
   DESTINATION
      ${CMAKE_INSTALL_PREFIX}/contracts/flon.fees
)