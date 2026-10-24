set(contract_name dao.treasury)
add_contract(${contract_name} ${contract_name} ${CMAKE_CURRENT_SOURCE_DIR}/src/${contract_name}.cpp)

target_include_directories(${contract_name} PUBLIC
   ${CMAKE_CURRENT_SOURCE_DIR}/include
   ${CMAKE_CURRENT_SOURCE_DIR}/../flon.token/include)
set_target_properties(${contract_name} PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}")

install(FILES
   ${CMAKE_CURRENT_BINARY_DIR}/${contract_name}.wasm
   ${CMAKE_CURRENT_BINARY_DIR}/${contract_name}.abi
   DESTINATION ${CMAKE_INSTALL_PREFIX}/contracts/${contract_name})
