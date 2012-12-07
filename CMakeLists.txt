cmake_minimum_required(VERSION 2.8)
project(postsrsd C)

find_program(HELP2MAN help2man DOC "path to help2man executable")
find_program(DD dd DOC "path to dd executable")
find_program(BASE64 base64 DOC "path to base64 executable")

add_executable(${PROJECT_NAME} postsrsd.c sha1.c srs2.c)
configure_file(${PROJECT_NAME}.init.in ${PROJECT_NAME}.init @ONLY)
configure_file(postinstall.cmake.in postinstall.cmake @ONLY)

if(HELP2MAN)
add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
	COMMAND ${HELP2MAN} ARGS -s8 -o${PROJECT_NAME}.8 -N -h-h -v-v ./${PROJECT_NAME}
	VERBATIM
)
install(FILES ${CMAKE_CURRENT_BINARY_DIR}/${PROJECT_NAME}.8 DESTINATION "share/man/man8")
endif()

install(TARGETS ${PROJECT_NAME} DESTINATION "sbin")
install(FILES README.md main.cf.ex DESTINATION "share/doc/${PROJECT_NAME}")
install(PROGRAMS ${CMAKE_CURRENT_BINARY_DIR}/${PROJECT_NAME}.init DESTINATION "/etc/init.d" RENAME ${PROJECT_NAME})
install(FILES ${PROJECT_NAME}.default DESTINATION "/etc/default" RENAME ${PROJECT_NAME})
install(SCRIPT "${CMAKE_CURRENT_BINARY_DIR}/postinstall.cmake")

