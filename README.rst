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


Building from source
~~~~~~~~~~~~~~~~~~~~

Fetch the latest source tarball or clone the repository from Github_,
unpack it and run::

    cd path/to/source
    mkdir _build && cd _build
    cmake .. -DCMAKE_BUILD_TYPE=Release
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


Migrating from version 1.x
--------------------------


The Big Picture: Certificate of origin for mails
------------------------------------------------

SPF, DKIM, and DMARC are three different, complementary standards to ensure
mail authenticity:

- SPF binds the sender address to an authorized originating MTA.
- DKIM binds the mail contents to an authorized originating MTA.
- DMARC combines SPF and DKIM into a certificate of origin

DMARC has two conditions for an email, but either of them is sufficient to
pass the DMARC check:

1. The SMTP envelope sender must have the same domain as the ``From:`` address
   in the mail header.
2. The email must have a valid DKIM signature from the domain of the
   ``From:`` address. 

The first condition in combination with SPF prevents mail forwarding by
unauthorized third parties, the second condition in combination with DKIM
prevents sender address spoofing. Effectively, DMARC only allows mail
forwarding if the mail is not tampered with.

By design, SRS must break the first condition, but it will preserve the
second, if the originating MTA signs all outgoing mails with DKIM.

DMARC has a somewhat bad reputation, which is only partially deserved.
It is true that DMARC breaks many traditional mailing lists, which forward
mails *and* modify them, e.g., by adding a Subject prefix or a mailing list
footer. However, many DMARC failures actually come from misconfigured domains,
which enforce a strict DMARC reject policy, but fail to DKIM-sign all outgoing
mails. This breaks email forwarding for good; please don't do it, the Internet
is broken enough as it is.
