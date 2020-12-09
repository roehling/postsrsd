---
name: Bug report
about: Create a report to help us improve
title: ''
labels: bug
assignees: ''

---

<!-- IMPORTANT NOTICE
Please do not file a bug report if you merely have issues with configuring PostSRSd correctly. You are welcome to join our Discussions and seek help there instead. -->

**Describe the bug**
A clear and concise description of what the bug is.

**Relevant log entries**
You may anonymize log output as you see fit, but please try and keep as much log information intact as possible. I suggest you replace any sensitive information with a descriptive placeholder, such as:
 - your configured SRS domain with `[SRS_DOMAIN]` and  relevant other domains with `[DOMAIN_1]`, `[DOMAIN_2]`, etc.
 - MX host names by `[DOMAIN_1_MX]`, `[DOMAIN_2_MX]` etc.
 - local parts of email addresses with `[USER_1]` or  `[USER_2]` etc.

```
<!-- Please paste relevant log output from /var/log/mail.log or /var/log/syslog here:-->
Dec  3 15:46:05 [HOSTNAME] postsrsd[16907]: srs_forward: <[USER_1]@[DOMAIN_1]> rewritten as <SRS0=vmyz=2W=[DOMAIN_1]=[USER_1]@[SRS_DOMAIN]>
```

**System Configuration**
 - OS: [e.g. Debian]
 - Mailer daemon: [e.g. Postfix 3.5]
 - Version: [e.g. 1.9]

**More Context**
Is there anything else we should know about your system configuration?
