..
    PostSRSd - Sender Rewriting Scheme daemon for Postfix
    Copyright 2012-2023 Timo Röhling <timo@gaussglocke.de>
    SPDX-License-Identifier: GPL-3.0-only

========================
PostSRSd Packaging Notes
========================

Introduction
------------

Thank you for taking an interest in PostSRSd and for all the work you put in to
make my software available to a broader audience! I have tried to make PostSRSd
easy to package, and this document is intended to document a few advanced CMake
features you can use to adapt PostSRSd for your distribution. Feel free to open
an issue if you think that PostSRSd can be improved.


Third-Party Dependencies
------------------------

PostSRSd has gained a few external dependencies with its 2.0 rewrite, and it
uses the CMake FetchContent_ module to manage those. By default, PostSRSd will
look for installed system libraries and fall back to downloading the sources of
its dependencies at configure time, building them, and linking them statically
into the executable.

For self-contained builds, you may want to pass the additional CMake options
``-DFETCHCONTENT_FULLY_DISCONNECTED -DFETCHCONTENT_TRY_FIND_PACKAGE_MODE=ALWAYS``
to make sure that no attempts to download anything are made.

.. _FetchContent: https://cmake.org/cmake/help/latest/module/FetchContent.html


Tweaking the Default Configuration
----------------------------------

PostSRSd mostly relies on the GNUInstallDirs_ module to discover the correct
paths for data files. Additionally, PostSRSd has a few custom CMake options you
can set with ``-D<name>=<value>`` and tweak the default settings for your
users:

- ``POSTSRSD_USER``: the unprivileged user as which PostSRSd is supposed to
  run. Defaults to ``nobody``. Note that this value can always be overridden in
  ``postsrsd.conf`` by the user.

- ``POSTSRSD_CONFIGDIR``: the location where PostSRSd should store configuration
  files. The default is ``${CMAKE_INSTALL_FULL_SYSCONFDIR}``.

- ``POSTSRSD_DATADIR``: the location where PostSRSd should store runtime files
  such as the SQLite database for envelope senders (if PostSRSd is configured
  to use that). The default is ``${CMAKE_INSTALL_LOCALSTATEDIR}/lib/postsrsd``.
  Note that this value can always be overridden in ``postsrsd.conf`` by the
  user.

- ``POSTSRSD_CHROOTDIR``: the location where PostSRSd is supposed to jail
  itself. Defaults to ``${POSTSRSD_DATADIR}``. Note that this value can always
  be overridden in ``postsrsd.conf`` by the user.

- ``GENERATE_SRS_SECRET``: If set to ``ON`` (the default), PostSRSd will create
  ``postsrsd.secret`` if it does not exist. This is helpful to make a source
  installation secure by default, but less so for a distributed binary package.

- ``INSTALL_SYSTEMD_SERVICE``: If set to ``ON`` (the default), a postsrsd.service
  unit will be installed to allow starting PostSRSd via systemd. You can disable
  this if your distribution uses a different init system.

- ``INSTALL_SYSTEMD_SYSUSERS``: If set to ``ON``, a sysusers.d config snippet
  will be installed which instructs systemd to create the ``POSTSRSD_USER`` system
  user if it not exists. This is disabled by default.

- ``SYSTEMD_UNITDIR``: the intended install destination for the
  ``postsrsd.service`` file. The default should be fine for most systems, but
  you can override it if the auto-detected location is wrong.

- ``SYSTEMD_SYSUSERSDIR``: the intended install destination for the
  sysusers.d configuration file. The default should be fine for most systems, but
  you can override it if the auto-detected location is wrong.

- ``DEVELOPER_BUILD``: this makes the compiler treat all warnings as errors and
  enable as much of them as possible. While certainly useful for me as upstream
  developer (and for the Github CI), you should keep this option disabled. It
  will not do much for you except likely break your package build each time a
  new compiler version is released.

- ``TESTS_WITH_ASAN``: If set to ``ON``, the unit tests will be linked against
  the AddressSanitizer and LeakSanitizer libraries to catch common memory handling
  bugs. Enabling this option does not affect the PostSRSd binary, it will only
  be used for the test suite. It is disabled by default.

- ``TESTS_WITH_REDIS``: If set to ``ON``, the test suite will assume that a
  Redis instance is available at ``localhost:6397``, and run additional
  database related tests. This is disabled by default and has no effect unless
  ``WITH_REDIS=ON`` is also set.

.. _GNUInstallDirs: https://cmake.org/cmake/help/latest/module/GNUInstallDirs.html
