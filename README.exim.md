PostSRSd integration with Exim
==============================

## SRS Return router

If using a domain solely for SRS return addresses:

    SRS_DOMAIN = srs.your.domain
    
    begin routers
    
    srs_return:
      caseful_local_part
      domains = SRS_DOMAIN
      driver = redirect
      allow_fail
      data = ${if match {$local_part}{\N(?i)^srs[01]=\N} \
    	{${if match \
    	  {${readsocket{inet:localhost:10002}{get ${quote_local_part:$local_part_prefix$local_part}@$domain}{3s}}} \
    	  {\N^200 (.+)\N} \
    	  {$1} \
    	  {:fail: Invalid SRS bounce} \
    	}} \
    	{:fail: Invalid SRS bounce} \
      }
      no_more

If your SRS domain is also used for other addresses:

    SRS_DOMAIN = srs.your.domain
    
    begin routers
    
    srs_return:
      caseful_local_part
      domains = SRS_DOMAIN
      local_part_prefix = srs0= : srs1=
      driver = redirect
      allow_fail
      data = ${if match \
        {${readsocket{inet:localhost:10002}{get ${quote_local_part:$local_part_prefix$local_part}@$domain}{3s}}} \
        {\N^200 (.+)\N} \
        {$1} \
        {:fail: Invalid SRS bounce} \
      }

## Rewriting outgoing mail in the SMTP transport

The following excludes locally submitted mail, or mail submitted by authenticated
users from SRS rewriting. Of course, if the sender address is already in
one of our local domains, there is no need to rewrite the address. You may need
to remove `*@+virtual_domains` from the list if you do not use them.

    begin transports
    
    remote_smtp:
      debug_print = "T: remote_smtp for $local_part@$domain"
      driver = smtp
      return_path = \
        ${if and{\
                  {!match_ip{$sender_host_address}{:@[]}}\
                  {!def:authenticated_id}\
                  {!match_address{$sender_address}{*@+local_domains:*@+virtual_domains:SRS_DOMAIN}}\
                }{${if match \
                  {${readsocket{inet:localhost:10001}{get $sender_address}{3s}}}\
                  {\N^200 (.+)\N}\
                  {$1}\
                  fail}\
                }\
         fail}
    
In your router you're likely to know when you're forwarding an email. When you check a ldap for a 
forwarding address for example. If that's the case you can set this in your router

       address_data = "enable-forward=true"

And add this config in `30_exim4-config_remote_smtp` if you use Debian split config for example

    return_path = \
    ${if and{\
              {bool{${extract{enable-forward}{$address_data}}}}\
              {!match_address{$sender_address}{*@+local_domains:*@+virtual_domains:SRS_DOMAIN}}\
            }{${if match \
              {${readsocket{inet:localhost:10001}{get $sender_address}{3s}}}\
              {\N^200 (.+)\N}\
              {$1}\
              fail}\
            }\
     fail}
