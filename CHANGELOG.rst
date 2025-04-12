..
    PostSRSd - Sender Rewriting Scheme daemon for Postfix
    Copyright 2012-2022 Timo Röhling <timo@gaussglocke.de>
    SPDX-License-Identifier: GPL-3.0-only

#########
Changelog
#########

2.0.11
======

Fixed
-----

* Run `autoreconf` to prevent confuse build failures with newer
  autoconf/automake releases
* Fix build failures with libcheck if libsubunit ist installed
  (`#161 <https://github.com/roehling/postsrsd/issues/161`_)
* Do not fail silently if `WITH_MILTER=ON` is set but libmilter
  is not available

2.0.10
======

Changed
-------

* Allow comments in domains-file
  (`#181 <https://github.com/roehling/postsrsd/issues/181>`_)

Added
-----

* Support for building against system libmilter library

2.0.9
=====

Fixed
-----

* Fixed build with system libraries
  (`#176 <https://github.com/roehling/postsrsd/issues/176>`_)

Changed
-------

* Create sockets non-blocking and with close-on-exec enabled

2.0.8
=====

Fixed
-----

* Fixed socket creation for Milter
* Fixed Milter issue with IPv6 clients
  (`#156 <https://github.com/roehling/postsrsd/issues/156>`_)

Added
-----

* Support for system user management with ``sysusers.d``
* Better customization of the PostSRSd build with
  ``POSTSRSD_CONFIGDIR`` and ``INSTALL_SYSTEMD_SERVICE``

Changed
-------

* Improved documentation of the PostSRSd example configuration

Contributions
-------------

* Richard Hansen (`#155 <https://github.com/roehling/postsrsd/pull/155>`_,
  `#157 <https://github.com/roehling/postsrsd/pull/157>`_)

2.0.7
=====

Fixed
-----

* the parser callback for the ``original-envelope`` option used the
  wrong return type, which could prevent the ``database`` mode from
  activating
* PostSRSd is confirmed to build and run on FreeBSD now

2.0.6
=====

Added
-----

* New configuration option ``debug`` to increase log verbosity.

Changed
-------

* Reduced default log verbosity: PostSRSd no longer prints
  messages for mail addresses which need no rewrite
  (`#149 <https://github.com/roehling/postsrsd/issues/149>`_)

2.0.5
=====

Fixed
-----

* Do not try to set Keep-Alive on Redis unix sockets
  (`#146 <https://github.com/roehling/postsrsd/issues/146>`_)

2.0.4
=====

Fixed
-----

* Worked around EXCLUDE_FROM_ALL bug in CMake 3.20.x and older
* Fixed a few compiler warnings in the test suite

Added
-----

* Added support for musl as libc alternative
* Added support for CPack to generate installable packages
* Added new CLI option -h to print a summary of CLI options

Changed
-------

* The test suite no longer requires ``faketime`` as dependency
* Improved error logging


2.0.3
=====

Fixed
-----

* Close socketmap connection in main process to prevent resource
  exhaustion (`#141 <https://github.com/roehling/postsrsd/issues/141>`_)
* Explicitly set 0666 permissions on socketmap unix socket
  (`#141 <https://github.com/roehling/postsrsd/issues/141>`_)

2.0.2
=====

Fixed
-----

* Improved detection logic for systemd system unit directory
  (`#132 <https://github.com/roehling/postsrsd/issues/132>`_)
* Drop supplementary groups when relinquishing root privileges
  (`#133 <https://github.com/roehling/postsrsd/issues/133>`_)


2.0.1
=====

Fixed
-----

* Fixed improper linking against the pthread library on systems
  where pthread is separate from libc
  (`#130 <https://github.com/roehling/postsrsd/issues/130>`_)


2.0.0
=====

Added
-----

* Added proper configuration file format
* Added support for unix sockets
* Added new rewrite mode with database backend
* Added experimental milter support

Changed
-------

* PostSRSd uses ``socketmap`` tables instead of ``tcp`` tables now

Removed
-------

* Removed AppArmor and SELinux profiles
* Removed support for all init systems except systemd
  (Pull requests for needed init systems are welcome)


1.12
====

Fixed
-----

* Explicitly clear ``O_NONBLOCK`` to avoid inherited non-blocking sockets
  on some operating systems
  (`#117 <https://github.com/roehling/postsrsd/pull/117>`_)
* Do not close all file descriptors up to ``_SC_MAX_OPEN``, as this limit
  tends to be absurdly high in Docker containers
  (`#122 <https://github.com/roehling/postsrsd/issues/122>`_)
* Check for the existence of the ``faketime`` tool before using it in the
  unit tests.


1.11
====

Security
--------

* The subprocess that talks to Postfix could be caused to hang with a very
  long email address
  (`077be98d <https://github.com/roehling/postsrsd/commit/077be98d8c8a9847e4ae0c7dc09e7474cbe27db2>`_)

1.10
====

Security
--------

* Fixed CVE-2020-35573: PostSRSd could be tricked into consuming a lot of CPU
  time with an SRS address that has a very long time stamp tag
  (`4733fb11 <https://github.com/roehling/postsrsd/commit/4733fb11f6bec6524bb8518c5e1a699288c26bac>`_)

Fixed
-----

* Fixed a bug where PostSRSd would occasionally create invalid SRS addresses
  if the used secret is extremely long


1.9
===

Hotfix release

Added
-----

* Added test that systemd service file is working properly

Fixed
-----

* Fixed systemd service file


1.8
===

Added
-----

* Added "Always Rewrite" option
  (`#97 <https://github.com/roehling/postsrsd/pull/97>`_)
* Added blackbox testing for PostSRSd daemon

Changed
-------

* Improved syslog messages

Fixed
-----

* Fixed AppArmor and SELinux profiles


1.7
===

Changed
-------

* Improved systemd auto detection
* Drop group privileges as well as user privileges
* Merged Debian adaptations (Thanks to Oxan van Leeuwen)

Removed
-------

* CMake 2.x support


1.6
===

Added
-----

* Somewhat usable unit tests

Fixed
-----

* Fixed Big Endian issue with SHA-1 implementation
  (`#90 <https://github.com/roehling/postsrsd/pull/90>`_)

1.5
===

Added
-----

* Add configuration options for listening network interface

Changed
-------

* Close all open file descriptors on startup

Fixed
-----

* Fixed SELinux policy
* Fixed handling of excluded domains in systemd startup file


1.4
===

Added
-----

* Added dual stack support

Fixed
-----

* Make startup scripts more robust in case of configuration errors
* Improved BSD compatibility


1.3
===

Added
-----

* Make SRS separator configurable
* Added support for even more init systems


1.2
===

Added
-----

* Added support for more init systems

Changed
-------

* Listen to 127.0.0.1 by default

Fixed
-----

* Load correct timezone for logging


1.1
===

Fixed
-----

* Fixed various issues with the CMake script
* Fixed command line parsing bug


1.0
===
* First stable release
