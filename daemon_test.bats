#!/usr/bin/env bats
# vim: filetype=bash:

POSTSRSD="./postsrsd"
LANG=C.UTF-8

start_postsrsd_at()
{
	echo 'tops3cr3t' > "$BATS_TMPDIR/postsrsd.secret"
	local faketime="$1"
	shift
	faketime "${faketime}" ${POSTSRSD} -D -f 10001 -r 10002 -p "$BATS_TMPDIR/postsrsd.pid" -s "$BATS_TMPDIR/postsrsd.secret" -d example.com "$@"
}

stop_postsrsd()
{
	if [ -r $BATS_TMPDIR/postsrsd.pid ] && kill -0 $(< $BATS_TMPDIR/postsrsd.pid) &>/dev/null
	then
		kill -TERM $(< $BATS_TMPDIR/postsrsd.pid)
	fi
	rm -f $BATS_TMPDIR/postsrsd.pid
}

teardown()
{
	stop_postsrsd
}

@test "SRS forward rewrite" {
	start_postsrsd_at "2020-01-01 00:01:00 UTC"
	exec 9<>/dev/tcp/127.0.0.1/10001
	# email address that needs not be rewritten
	echo>&9 "get test@example.com"
	read<&9 line
	[[ "$line" = "200 test@example.com" ]]
	# regular email address rewrite
	echo>&9 "get test@otherdomain.com"
	read<&9 line
	[[ "$line" =~ ^"200 SRS0=vmyz=2W=otherdomain.com=test@example.com" ]]
	# invalid email address without domain part
	echo>&9 "get bla"
	read<&9 line
	[[ "$line" =~ ^"500 No at sign" ]]
	# Test double-rewrite
	echo>&9 "get SRS0=XjO9=2V=otherdomain.com=test@example.com"
	read<&9 line
	[[ "$line" =~ ^"500 Already rewritten" ]]
	# Test rewrite of an SRS0 address from somewhere else
	echo>&9 "get SRS0=opaque+string@otherdomain.com"
	read<&9 line
	[[ "$line" =~ ^"200 SRS1=" ]]
	# Test rewrite of an SRS1 aaddress
	echo>&9 "get SRS1=X=thirddomain.com==opaque+string@otherdomain.com"
	read<&9 line
	[[ "$line" =~ ^"200 SRS1=JIBX=thirddomain.com==opaque+string@example.com" ]]
	# rewrite address with URL encoding
	echo>&9 "get test%40otherdomain.com"
	read<&9 line
	[[ "$line" =~ ^"200 SRS0=vmyz=2W=otherdomain.com=test@example.com" ]]
}

@test "SRS reverse rewrite" {
	start_postsrsd_at "2020-01-01 00:01:00 UTC"
	exec 9<>/dev/tcp/127.0.0.1/10002
	# Test regular SRS reversal
	echo>&9 "get SRS0=XjO9=2V=otherdomain.com=test@example.com"
	read<&9 line
	[[ "$line" =~ ^"200 test@otherdomain.com" ]]
	# Test SRS1 address
	echo>&9 "get SRS1=JIBX=thirddomain.com==opaque+string@example.com"
	read<&9 line
	[[ "$line" =~ ^"200 SRS0=opaque+string@thirddomain.com" ]]
	# Test non-SRS address
	echo>&9 "get test@example.com"
	read<&9 line
	[[ "$line" =~ ^"500 Not an SRS address" ]]
	# Test valid address with timestamp 6 months in the past
	echo>&9 "get SRS0=te87=T7=otherdomain.com=test@example.com"
	read<&9 line
	[[ "$line" =~ ^"500 Time stamp out of date" ]]
	# Test valid address with timestamp 6 months in the future
	echo>&9 "get SRS0=VcIb=7N=otherdomain.com=test@example.com"
	read<&9 line
	[[ "$line" =~ ^"500 Time stamp out of date" ]]
	# Test address with invalid hash
	echo>&9 "get SRS0=FAKE=2V=otherdomain.com=test@example.com"
	read<&9 line
	[[ "$line" =~ ^"500 Hash invalid" ]]
	# Test valid address with munged case #1
	echo>&9 "get srs0=xjo9=2v=otherdomain.com=test@example.com"
	read<&9 line
	[[ "$line" =~ ^"200 test@otherdomain.com" ]]
	# Test valid address with munged case #2
	echo>&9 "get SRS0=XJO9=2V=OTHERDOMAIN.COM=TEST@EXAMPLE.COM"
	read<&9 line
	[[ "$line" =~ ^"200 TEST@OTHERDOMAIN.COM" ]]
	# Test address without hash
	echo>&9 "get SRS0=@example.com"
	read<&9 line
	[[ "$line" =~ ^"500 No hash" ]]
	# Test address without time stamp
	echo>&9 "get SRS0=XjO9@example.com"
	read<&9 line
	[[ "$line" =~ ^"500 No time" ]]
	# Test address without host
	echo>&9 "get SRS0=XjO9=2V@example.com"
	read<&9 line
	[[ "$line" =~ ^"500 No host" ]]
	# Test address without local part
	echo>&9 "get SRS0=XjO9=2V=otherdomain.com@example.com"
	read<&9 line
	[[ "$line" =~ ^"500 No user" ]]
}

@test "SRS always rewrite option" {
	start_postsrsd_at "2020-01-01 00:01:00 UTC" -A
	exec 9<>/dev/tcp/127.0.0.1/10001
	echo>&9 "get test@example.com"
	read<&9 line
	[[ "$line" =~ ^"200 SRS0=" ]]
}

@test "SRS exclude domains" {
	start_postsrsd_at "2020-01-01 00:01:00 UTC" -X"excluded.com .exclude-subdomains.com"
	exec 9<>/dev/tcp/127.0.0.1/10001
	echo>&9 "get test@excluded.com"
	read<&9 line
	[[ "$line" =~ ^"500 Domain excluded" ]]
	echo>&9 "get test@subdomain.of.excluded.com"
	read<&9 line
	[[ "$line" =~ ^"200 SRS0=" ]]
	echo>&9 "get test@exclude-subdomains.com"
	read<&9 line
	[[ "$line" =~ ^"200 SRS0=" ]]
	echo>&9 "get test@subdomain.of.exclude-subdomains.com"
	read<&9 line
	[[ "$line" =~ ^"500 Domain excluded" ]]
}


@test "SRS invalid requests" {
	start_postsrsd_at "2020-01-01 00:01:00 UTC"
	exec 9<>/dev/tcp/127.0.0.1/10001
	echo>&9 "get"
	read<&9 line
	[[ "$line" =~ ^500 ]]
	exec 9<>/dev/tcp/127.0.0.1/10001
	echo>&9 "test@example.com"
	read<&9 line
	[[ "$line" =~ ^500 ]]
	exec 9<>/dev/tcp/127.0.0.1/10001
	echo>&9 "get encoding%error@otherdomain.com"
	read<&9 line
	[[ "$line" =~ ^500 ]]
}
