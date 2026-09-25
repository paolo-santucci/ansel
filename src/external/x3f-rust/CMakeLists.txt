if(CMAKE_CROSSCOMPILING OR NOT CMAKE_SYSTEM_NAME STREQUAL "Linux")
  message(FATAL_ERROR "Experimental Rust X3F integration currently supports native Linux builds only")
endif()

find_program(X3F_CARGO cargo HINTS "$ENV{HOME}/.cargo/bin" REQUIRED)
set(X3F_TARGET_DIR "${CMAKE_CURRENT_BINARY_DIR}/cargo")
set(X3F_ARCHIVE "${X3F_TARGET_DIR}/release/libansel_x3f_ffi.a")
file(GLOB_RECURSE X3F_RUST_SOURCES CONFIGURE_DEPENDS
     "${CMAKE_CURRENT_SOURCE_DIR}/src/*.rs"
     "${CMAKE_CURRENT_SOURCE_DIR}/ffi/src/*.rs")

add_custom_command(
  OUTPUT "${X3F_ARCHIVE}"
  COMMAND "${X3F_CARGO}" build --release --offline --locked
          --manifest-path "${CMAKE_CURRENT_SOURCE_DIR}/Cargo.toml"
          --target-dir "${X3F_TARGET_DIR}" -p ansel-x3f-ffi
  DEPENDS ${X3F_RUST_SOURCES}
          "${CMAKE_CURRENT_SOURCE_DIR}/Cargo.toml"
          "${CMAKE_CURRENT_SOURCE_DIR}/Cargo.lock"
          "${CMAKE_CURRENT_SOURCE_DIR}/ffi/Cargo.toml"
  VERBATIM)
add_custom_target(ansel_x3f_build DEPENDS "${X3F_ARCHIVE}")
add_library(ansel_x3f STATIC IMPORTED GLOBAL)
set_target_properties(ansel_x3f PROPERTIES IMPORTED_LOCATION "${X3F_ARCHIVE}")
add_dependencies(ansel_x3f ansel_x3f_build)
target_link_libraries(ansel_x3f INTERFACE ${CMAKE_DL_LIBS} pthread m)
install(FILES "${CMAKE_CURRENT_SOURCE_DIR}/LICENSE" DESTINATION "share/doc/ansel/x3f-rust")
