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
mails. This is Evil and prevents any mail forwarding.


Migrating from version 1.x
--------------------------


Building from source
--------------------


Configuration
-------------
