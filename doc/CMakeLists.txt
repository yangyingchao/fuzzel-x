cmake_minimum_required(VERSION 3.13)

find_program(SCDOC_EXECUTABLE scdoc)
find_program(GZIP_EXECUTABLE gzip)

add_custom_command(
  OUTPUT f00sel.1.gz
  COMMENT "Generating man page f00sel.1.gz"
  COMMAND ${SCDOC_EXECUTABLE} < ${CMAKE_CURRENT_SOURCE_DIR}/f00sel.scdoc |
          ${GZIP_EXECUTABLE} > f00sel.1.gz
  VERBATIM
  MAIN_DEPENDENCY ${CMAKE_CURRENT_SOURCE_DIR}/f00sel.scdoc
  )

add_custom_target(man-pages ALL SOURCES f00sel.scdoc DEPENDS f00sel.1.gz)
install(FILES ${CMAKE_CURRENT_BINARY_DIR}/f00sel.1.gz DESTINATION share/man/man1)
