# Copyright 2026 Timo Röhling <timo@gaussglocke.de>
# SPDX-License-Identifier: FSFAP
#
# Copying and distribution of this file, with or without modification, are
# permitted in any medium without royalty provided the copyright notice and
# this notice are preserved. This file is offered as-is, without any warranty.
#
include(FindPackageHandleStandardArgs)
find_package(PkgConfig QUIET)
if(PkgConfig_FOUND)
    pkg_search_module(PC_SECCOMP QUIET libseccomp)
endif()
find_path(seccomp_INCLUDE_DIR seccomp.h HINTS ${PC_SECCOMP_INCLUDE_DIRS})
find_library(seccomp_LIBRARY seccomp HINTS ${PC_SECCOMP_LIBRARY_DIRS})
find_package_handle_standard_args(
    seccomp
    FOUND_VAR seccomp_FOUND
    REQUIRED_VARS seccomp_INCLUDE_DIR seccomp_LIBRARY
)
if(seccomp_FOUND AND NOT TARGET seccomp::seccomp)
    add_library(seccomp::seccomp UNKNOWN IMPORTED)
    set_target_properties(
        seccomp::seccomp
        PROPERTIES IMPORTED_LOCATION "${seccomp_LIBRARY}"
                   INTERFACE_INCLUDE_DIRECTORIES "${seccomp_INCLUDE_DIR}"
    )
endif()
