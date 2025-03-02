add_contract(pubkey.token pubkey.token ${CMAKE_CURRENT_SOURCE_DIR}/src/pubkey.token.cpp)

target_include_directories(pubkey.token
   PUBLIC
   ${CMAKE_CURRENT_SOURCE_DIR}/include)

set_target_properties(pubkey.token
   PROPERTIES
   RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}")

configure_file( ${CMAKE_CURRENT_SOURCE_DIR}/ricardian/pubkey.token.contracts.md.in ${CMAKE_CURRENT_BINARY_DIR}/ricardian/pubkey.token.contracts.md @ONLY )

target_compile_options( pubkey.token PUBLIC -R${CMAKE_CURRENT_SOURCE_DIR}/ricardian -R${CMAKE_CURRENT_BINARY_DIR}/ricardian )
