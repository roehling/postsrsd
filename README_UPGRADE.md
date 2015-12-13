PostSRSd Upgrade Note
=====================

/etc/default/postsrsd
---------------------

The shipped configuration file has changed from previous versions.
However, the installer will not automatically overwrite existing
configuration files to prevent data loss.

Please review the changes after the installation and update your
configuration file accordingly. Note in particular, that most options
may no longer remain commented out, since it was cumbersome to maintain 
all the default values in the various startup scripts.

