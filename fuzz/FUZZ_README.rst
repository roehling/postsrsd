..
    PostSRSd - Sender Rewriting Scheme daemon for Postfix
    Copyright 2012-2026 Timo Röhling <timo@gaussglocke.de>
    SPDX-License-Identifier: GPL-3.0-only

======================
PostSRSd Fuzzing Notes
======================

This is a simple fuzzing harness for AFL_ which I used to shake out some bugs::

    cd path/to/source
    mkdir _build && cd _build
    cmake .. -DCMAKE_BUILD_TYPE=RelWithDebInfo -DCMAKE_C_COMPILER=/usr/bin/afl-clang-lto -DBUILD_FUZZING=ON
    cmake --build . --target $FUZZING_TARGET
    cmake --build . --target

Currently implemented fuzzing targets are: ``fuzz_netstring``, ``fuzz_srs``

.. _AFL: https://aflplus.plus/
