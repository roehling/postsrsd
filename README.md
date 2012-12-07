About
=====
PostSRSd provides the Sender Rewriting Scheme (SRS) via TCP-based 
lookup tables for Postfix. SRS is needed if your mail server acts
as forwarder. 

Imagine your server receives a mail from alice@example.com
that is to be forwarded. If example.com uses the Sender Policy Framework 
to indicate that all legit mails originate from their server, your 
forwarded mail might be bounced, because you have no permission to send
on behalf of example.com. The solution is that you map the address to
your own domain, e.g. 
SRS0+xxxx=yy=example.com=alice@yourdomain.org (forward SRS). If the
mail is bounced later and a notification arrives, you can extract the
original address from the rewritten one (revere SRS) and return the
notification to the sender. You might notice that the reverse SRS can
be abused to turn your server into an open relay. For this reason, xxxx
and yy are a cryptographic signature and a time stamp. If the signature
does not match, the address is forged and the mail can be discarded.

Building
========
PostSRSd requires a POSIX compatible system and CMake to build. 
Optionally, help2man is used to create a manual page.

For convenience, a Makefile fragment is provided which calls CMake with
the recommended command line options. Just run `make`.

Installing
==========
Run `make install` as root to install the daemon and the configuration
files.

Configuration
=============
The configuration is located in `/etc/default/postsrsd`. You must store 
a secret key in `/etc/postsrsd.secret`. The installer tries to generate 
one from `/dev/urandom`. Be careful that no one can guess your secret,
because anyone who knows it can use your mail server as open relay!

PostSRSd exposes its functionality via two TCP lookup tables. The
recommended Postfix configuration is to add the following fragment to
your main.cf:

    sender_canonical_maps = tcp:127.0.0.1:10001
    sender_canonical_classes = envelope_sender
    recipient_canonical_maps = tcp:127.0.0.1:10002
    recipient_canonical_maps = envelope_recipient

This will transparently rewrite incoming and outgoing envelope addresses.
Run `service postsrsd start` and `postfix reload` as root, or reboot.

