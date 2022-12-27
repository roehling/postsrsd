# Copyright 2022 Timo Röhling <timo@gaussglocke.de>
# SPDX-License-Identifier: FSFAP
#
# Copying and distribution of this file, with or without modification, are
# permitted in any medium without royalty provided the copyright notice and
# this notice are preserved. This file is offered as-is, without any warranty.
#
include(FindPackageHandleStandardArgs)
find_package(Check CONFIG QUIET)
if(TARGET Check::check)
    find_package_handle_standard_args(Check CONFIG_MODE)
else()
    find_package(PkgConfig QUIET)
    if(PkgConfig_FOUND)
        pkg_search_module(PC_CHECK QUIET check)
        pkg_search_module(PC_SUBUNIT QUIET libsubunit)
    endif()
    find_path(Check_INCLUDE_DIR check.h HINTS ${PC_CHECK_INCLUDE_DIRS})
    find_library(Check_LIBRARY NAMES check_pic check HINTS ${PC_CHECK_LIBRARY_DIRS})
    find_path(Check_subunit_LIBRARY subunit HINTS ${PC_SUBUNIT_LIBRARY_DIRS})
    find_library(Check_m_LIBRARY m)
    find_library(Check_rt_LIBRARY rt)
    find_package(Threads REQUIRED)
    find_package_handle_standard_args(Check
        FOUND_VAR Check_FOUND
        REQUIRED_VARS
            Check_INCLUDE_DIR
            Check_LIBRARY
    )
    if(Check_FOUND AND NOT TARGET Check::check)
        set(Check_DEPS "Threads::Threads")
        if(Check_subunit_LIBRARY)
            list(APPEND Check_DEPS "${Check_subunit_LIBRARY}")
        endif()
        if(Check_m_LIBRARY)
            list(APPEND Check_DEPS "${Check_m_LIBRARY}")
        endif()
        if(Check_rt_LIBRARY)
            list(APPEND Check_DEPS "${Check_rt_LIBRARY}")
        endif()
        add_library(Check::check UNKNOWN IMPORTED)
        set_target_properties(Check::check PROPERTIES
            IMPORTED_LOCATION "${Check_LIBRARY}"
            INTERFACE_INCLUDE_DIRECTORIES "${Check_INCLUDE_DIR}"
            INTERFACE_LINK_LIBRARIES "${Check_DEPS}"
        )
    endif()
endif()
