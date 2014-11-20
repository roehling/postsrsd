PostSRSd
========

About
-----

PostSRSd provides the Sender Rewriting Scheme (SRS) via TCP-based 
lookup tables for Postfix. SRS is needed if your mail server acts
as forwarder. 


Sender Rewriting Scheme Crash Course
------------------------------------
Imagine your server receives a mail from alice@example.com
that is to be forwarded. If example.com uses the Sender Policy Framework 
to indicate that all legit mails originate from their server, your 
forwarded mail might be bounced, because you have no permission to send
on behalf of example.com. The solution is that you map the address to
your own domain, e.g. 
SRS0+xxxx=yy=example.com=alice@yourdomain.org (forward SRS). If the
mail is bounced later and a notification arrives, you can extract the
original address from the rewritten one (reverse SRS) and return the
notification to the sender. You might notice that the reverse SRS can
be abused to turn your server into an open relay. For this reason, xxxx
and yy are a cryptographic signature and a time stamp. If the signature
does not match, the address is forged and the mail can be discarded.

Building
--------

PostSRSd requires a POSIX compatible system and CMake to build. 
Optionally, help2man is used to create a manual page.

For convenience, a Makefile fragment is provided which calls CMake with
the recommended command line options. Just run `make`.

Alternatively, you can control many aspects of the build manually:

    mkdir build
    cd build
    cmake .. <options>
    make
    make install

The CMake script defines a number of options in addition to the
standard CMake flags. Use `-D<option>=<value>` to override the defaults.

*   `GENERATE_SRS_SECRET` (default: `ON`). Generate a random secret on install.
*   `USE_APPARMOR` (default: `OFF`): Install an AppArmor profile for the daemon.
*   `INIT_FLAVOR` (default: auto-detect). Select the appriopriate startup 
    script type. Must be one of (`upstart`,`sysv-lsb`,`sysv-redhat`) or `none`.
*   `CHROOT_DIR` (default: `${CMAKE_INSTALL_PREFIX}/lib/postsrsd`). Chroot jail
    for the daemon.
*   `SYSCONF_DIR` (default: `/etc`). Location of system configuration files.
*   `CONFIG_DIR` (default: `${SYSCONF_DIR}/default`). Install destination for
    the postsrsd settings.
*   `DOC_DIR` (default: `${CMAKE_INSTALL_PREFIX}/share/doc/postsrsd`). Install
    destination for documentation files.

Installing
----------

Run `make install` as root to install the daemon and the configuration
files.

Configuration
-------------

The configuration is located in `/etc/default/postsrsd` by default. You must store
at least one secret key in `/etc/postsrsd.secret`. The installer tries to generate
one from `/dev/urandom`. Be careful that no one can guess your secret,
because anyone who knows it can use your mail server as open relay!
Each line of `/etc/postsrsd.secret` is used as secret. The first secret is
used for signing and verification, the others for verification only.

PostSRSd exposes its functionality via two TCP lookup tables. The
recommended Postfix configuration is to add the following fragment to
your main.cf:

    sender_canonical_maps = tcp:127.0.0.1:10001
    sender_canonical_classes = envelope_sender
    recipient_canonical_maps = tcp:127.0.0.1:10002
    recipient_canonical_classes= envelope_recipient

This will transparently rewrite incoming and outgoing envelope addresses.
Run `service postsrsd start` and `postfix reload` as root, or reboot.

Known Issues
------------

- Due to the way PostSRRSd is integrated with Postfix, sender addresses
  will always be rewritten even if the mail is not forwarded at all. This
  is because the canonical maps are read by the cleanup daemon, which
  processes mails at the very beginning before any routing decision is made.

- The Postfix package in CentOS 6 lacks the required support for TCP
  dictionaries. Please upgrade your distribution or build Postfix yourself.

