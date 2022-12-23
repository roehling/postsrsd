..
    PostSRSd - Sender Rewriting Scheme daemon for Postfix
    Copyright 2012-2022 Timo Röhling <timo@gaussglocke.de>
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

If you are looking to package PostSRSd for a Linux distribution, have a look
at the packaging_ notes.

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

PostSRSd has a few external dependencies:

- CMake_ version 3.14 or newer
- gcc_ or a similar C99 capable C compiler.
- libConfuse_ is required to parse the configuration file.
- sqlite3_ is optional to store envelope senders;
  enable it with ``-DWITH_SQLITE=ON`` as additional argument for ``cmake``.
- hiredis_ is an optional alternative to store envelope senders in Redis;
  enable it with ``-DWITH_REDIS=ON``.
- libMilter_ is needed only if you wish to configure PostSRSd as milter;
  enable it with ``-DWITH_MILTER=ON``.
- check_ is needed if you want to build and run the unit test suite;
  otherwise disable it with ``-DBUILD_TESTING=OFF``.

PostSRSd relies on the FetchContent_ module of CMake for its dependency
resolution. Please refer to its documentation if you wish to tweak the
discovery process.

.. _CMake: https://cmake.org
.. _gcc: https://gcc.gnu.org
.. _libConfuse: https://github.com/libconfuse/libconfuse
.. _sqlite3: https://sqlite.org
.. _hiredis: https://github.com/redis/hiredis
.. _libMilter: https://github.com/jons/libmilter
.. _check: https://github.com/libcheck/check
.. _FetchContent: https://cmake.org/cmake/help/latest/module/FetchContent.html


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
``canonical`` lookup table of the ``cleanup`` daemon. Add the following snippet
to your ``/etc/postfix/main.cf``::

    sender_canonical_maps = socketmap:unix:srs:forward
    sender_canonical_classes = envelope_sender
    recipient_canonical_maps = socketmap:unix:srs:reverse
    recipient_canonical_classes = envelope_recipient, header_recipient

Note that ``srs`` is the path to the unix socket relative to
``/var/spool/postfix``, so you will have to change this if you change the
``socketmap`` configuration of PostSRSd. If you prefer a TCP connection, e.g.
``inet:localhost:10003``, you need to use this as map endpoint, e.g.
``socketmap:inet:localhost:10003:forward``.

Also note that the ``socketmap`` mechanism requires at least Postfix 2.10 and
is NOT backwards compatible with the deprecated ``tcp`` mechanism that was used
in PostSRSd 1.x.

.. _example: data/postsrsd.conf.in

Experimental Milter Support
~~~~~~~~~~~~~~~~~~~~~~~~~~~

PostSRSd 2.0 has added optional support for the Milter protocol. If you enabled
it at compile time, you can set the ``milter`` option in ``postsrsd.conf`` and
add the corresponding line to your ``etc/postfix/main.cf``::

    smtpd_milters = unix:srs_milter

Note that the Milter code is less tested and should be considered experimental
for now and not ready for production. Feel free to report bugs or open pull
requests if you try it out, though.

Migrating from version 1.x
--------------------------

Most configuration options can no longer be configured with command line arguments,
so you will have to set them in ``postsrsd.conf``. PostSRSd 1.x used shell variables
in ``/etc/default/postsrsd``. If you migrate your settings, you should set

- ``srs-domain`` to the value from ``SRS_DOMAIN``
- ``domains`` to the list of values from ``SRS_EXCLUDE_DOMAINS``
- ``secrets-file`` to the file name from ``SRS_SECRET``
- ``unprivileged-user`` to the user name from ``RUN_AS``
- ``chroot-dir`` to the directory from ``CHROOT``

As the new ``socketmap`` mechanism is no longer compatible with the old ``tcp``
mechanism, you will have to update your Postfix configuration as detailed above.

Frequently Asked Questions
--------------------------

* Why are some of my emails still rejected with a DMARC failure?
  
  Short Answer: Because the originating MTA is misconfigured.

  Long Answer: DMARC has two conditions for an email, but either of them is
  sufficient to pass the DMARC check:

  1. The SMTP envelope sender must have the same domain as the ``From:``
     address in the mail header.
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
