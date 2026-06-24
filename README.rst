..
    PostSRSd - Sender Rewriting Scheme daemon for Postfix
    Copyright 2012-2023 Timo Röhling <timo@gaussglocke.de>
    SPDX-License-Identifier: GPL-3.0-only

========
PostSRSd
========

Sender Rewriting Scheme daemon for Postfix


Overview
--------

The Sender Rewriting Scheme (SRS) is a technique to forward mails from domains
which deploy the Sender Policy Framework (SPF) to prohibit other Mail Transfer
Agents (MTAs) from sending mails on their behalf. With SRS, an MTA can
circumvent SPF restrictions by replacing the envelope sender with a temporary
email address from one of their own domains. This temporary address is bound to
the original sender and only valid for a certain amount of time, which prevents
abuse by spammers.


Installation
------------

Prebuilt packages
~~~~~~~~~~~~~~~~~

If your Linux distribution has a sufficiently recent PostSRSd package, install
it! Unless you need a specific new feature or bugfix from a newer version, it
will be much less of a maintenance burden.

If you are interested in packaging PostSRSd for a Linux distribution, have a
look at the packaging_ notes.

.. _packaging: doc/packaging.rst

Building from source
~~~~~~~~~~~~~~~~~~~~

Fetch the latest source tarball or clone the repository from Github_, unpack it
and run::

    cd path/to/source
    mkdir _build && cd _build
    cmake .. -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX=/usr/local
    make -j
    sudo make install

.. _Github: https://github.com/roehling/postsrsd/releases/latest

PostSRSd has a few external build dependencies:

- CMake_ version 3.24 or newer
- gcc_ or a similar C99 capable C compiler.
- pkgconf_ or pkg-config is optional to improve detection of system settings
- libConfuse_ is required to parse the configuration file.
- sqlite3_ is optional to store envelope senders;
  enable it with ``-DWITH_SQLITE=ON`` as additional argument for ``cmake``.
- hiredis_ is an optional alternative to store envelope senders in Redis;
  enable it with ``-DWITH_REDIS=ON``.
- libseccomp_ and gperf_ are needed if you want to secure untrusted input
  handling with additional sandboxing; enable it with ``-DWITH_SECCOMP=ON``.
- check_ is needed if you want to build and run the unit test suite;
  otherwise disable it with ``-DBUILD_TESTING=OFF``.
- Python_ is needed for the optional blackbox tests.

PostSRSd relies on the FetchContent_ module of CMake for its dependency
resolution. Please refer to its documentation if you wish to tweak the
discovery process.

.. _CMake: https://cmake.org
.. _gcc: https://gcc.gnu.org
.. _pkgconf: http://pkgconf.org
.. _libConfuse: https://github.com/libconfuse/libconfuse
.. _sqlite3: https://sqlite.org
.. _hiredis: https://github.com/redis/hiredis
.. _libseccomp: https://github.com/seccomp/libseccomp
.. _gperf: https://github.com/jwinarske/gperf
.. _check: https://github.com/libcheck/check
.. _FetchContent: https://cmake.org/cmake/help/latest/module/FetchContent.html
.. _Python: https://www.python.org

Configuration
-------------

PostSRSd itself is configured by ``postsrsd.conf`` (see the example_ for a
detailed documentation of all options). PostSRSd will look for this file in
``/usr/local/etc``. The most important configuration options are ``domains``
(or ``domains-file``), so PostSRSd knows about your local domains, and
``secrets-file`` with a secret passphrase for authentication. The other options
often work out of the box. You can also find the example configuration
installed in ``/usr/local/share/postsrsd``. Feel free to use it as base for
your own configuration.

Postfix Setup
~~~~~~~~~~~~~

For integration with Postfix, the recommended mechanism is via the
``canonical`` maps of the ``cleanup`` daemon. Add the following snippet to your
``/etc/postfix/main.cf``::

    sender_canonical_maps = socketmap:unix:srs:forward
    sender_canonical_classes = envelope_sender
    recipient_canonical_maps = socketmap:unix:srs:reverse
    recipient_canonical_classes = envelope_recipient, header_recipient

The ``srs`` part in the lookup table mappings above is the path to the unix
socket relative to ``/var/spool/postfix``; you will have to change this if you
change the ``socketmap`` configuration of PostSRSd. If you prefer a TCP
connection, e.g. ``inet:localhost:10003``, you need to change the mapping to
something like ``socketmap:inet:localhost:10003:forward``.

.. _example: doc/postsrsd.conf

Experimental Milter Support
~~~~~~~~~~~~~~~~~~~~~~~~~~~

PostSRSd 2.x has added optional support for the Milter protocol. You can set
the ``milter`` option in ``postsrsd.conf`` and add the corresponding line to
your ``etc/postfix/main.cf``::

    smtpd_milters = unix:srs_milter

Note that the Milter code is less tested and should be considered experimental
for now and not ready for production. Feel free to report bugs or open pull
requests if you try it out, though. Until PostSRSd 2.1, the milter support was
implemented with the external libMilter library from Sendmail.

Inotify Support
---------------

PostSRSd 2.1 added support for automatic reload if the ``domains-file``
is modified. This feature is disabled by default, but you can opt-in by
enabling the ``domains-file-watch`` option. It comes with the usual caveats
of modern file systems, especially regarding atomic file updates.

The recommended way to update the domains file is by an atomic move, i.e.,
create a ``postsrsd.domains.tmp.XXXXXXX`` in the same directory, populate
it, and rename it to replace the actual domains file. It is also technically
possible to modify the file in-place, but this is unsafe if the system crashes.
The worst way to update is to delete the existing file and create a new one
in-place. This is not atomic either, and the inotify API introduces a race
condition which may let PostSRSd miss the relevant file close event after the
new file has been written completely. PostSRSd tries to recover from this, but
there are no guarantees, so do not file any bugs if it fails.

If a file change is detected, PostSRSd behaves exactly like it would if the
process received a ``SIGHUP`` signal or a ``systemctl reload``.

Migrating from version 1.x
--------------------------

Most configuration options can no longer be configured with command line
arguments, so you will have to set them in ``postsrsd.conf``. PostSRSd 1.x used
shell variables in ``/etc/default/postsrsd``. If you migrate your settings, you
should set

- ``srs-domain`` to the value from ``SRS_DOMAIN``
- ``domains`` to the list of values from ``SRS_EXCLUDE_DOMAINS``
- ``secrets-file`` to the file name from ``SRS_SECRET``
- ``unprivileged-user`` to the user name from ``RUN_AS``
- ``chroot-dir`` to the directory from ``CHROOT``

Be aware that PostSRSd 2.x uses ``socketmap:`` tables, which are NOT compatible
with ``tcp:`` tables. This also means that PostSRSd 2.x requires at least
Postfix 2.10 now, and you need to update your Postfix configuration as detailed
above.

Frequently Asked Questions
--------------------------

* **Can I configure PostSRSd so it will only rewrite the envelope sender if the
  email is not delivered locally?**

  Starting with PostSRSd 2.1, the PostSRSd milter supports this out-of-the-box:
  If all recipients in a mail transaction belong to one of the configured local
  domains, PostSRSd will assume that no forwarding will happen, and skip
  rewriting the envelope sender. If you prefer the socketmap behavior, you can
  enable the ``milter-rewrite-local`` configuration option.

* **I am serving multiple domains with my MTA. Can I configure PostSRSd to
  rewrite addresses to the specific domain for which an email is forwarded?**

  It is much simpler and more robust to have a dedicated SRS (sub-)domain. You
  need to pick a domain for the reverse DNS lookup of your MTA IP address
  anyway, so setup an ``srs`` subdomain there and use it for SRS rewriting.

* **I configured PostSRSd correctly; why are some of my emails still rejected
  with a DMARC failure?**

  Short Answer: Because the originating MTA is misconfigured.

  Long Answer: DMARC has two conditions for an email, but either of them is
  sufficient to pass the DMARC check:

  1. The SMTP envelope sender must have the same domain as the
     ``From:`` address in the mail header.
  2. The email must have a valid DKIM signature from the domain of the
     ``From:`` address.

  The first condition in combination with SPF prevents mail forwarding by
  unauthorized third parties, the second condition in combination with DKIM
  prevents sender address spoofing. Effectively, DMARC only allows mail
  forwarding if the mail is not tampered with.

  By design, SRS must break the first condition, but it will preserve the
  second, if the originating MTA signs all outgoing mails with DKIM.

  Unfortunately, some mail admins forget (or misconfigure) DKIM, which
  effectively breaks forwarding for *everyone*. Try to contact the mail
  administrator for the sending domain and tell them to fix their setup.

* **Can you help me find the problem with my Postfix configuration?**

  No.
