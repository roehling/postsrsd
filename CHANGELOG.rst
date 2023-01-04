#########
Changelog
#########

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
* Do not close all file descriptors up to ``_SC_MAX_OPEN``, as this limit
  tends to be absurdly high in Docker containers
* Check for the existence of the ``faketime`` tool before using it in the
  unit tests.


1.11
====

Security
--------

* The subprocess that talks to Postfix could be caused to hang with a very
  long email address (Thanks to Mateusz Jończyk for the report)


1.10
====

Security
--------

* Fixed CVE-2020-35573: PostSRSd could be tricked into consuming a lot of CPU
  time with an SRS address that has a very long time stamp tag

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
