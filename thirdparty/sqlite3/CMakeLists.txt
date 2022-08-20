# Copyright (c) 2012-2022 Timo Röhling <timo@gaussglocke.de>
#
# Copying and distribution of this file, with or without modification, are
# permitted in any medium without royalty provided the copyright notice and
# this notice are preserved. This file is offered as-is, without any warranty.
#
find_package(Threads REQUIRED)

add_library(sqlite3
    sqlite3.c
)
target_include_directories(sqlite3 PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(sqlite3 PUBLIC Threads::Threads ${CMAKE_DL_LIBS})

add_library(sqlite3::sqlite3 ALIAS sqlite3)
