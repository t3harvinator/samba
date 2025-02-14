#!/bin/sh

outfile="$(dirname $0)/winbindd.stp"

child_funcs="winbindd_dual_ping
winbindd_dual_list_trusted_domains
winbindd_dual_init_connection
winbindd_dual_pam_auth
winbindd_dual_pam_auth_crap
winbindd_dual_pam_logoff
winbindd_dual_pam_chng_pswd_auth_crap
winbindd_dual_pam_chauthtok
_wbint_LookupSid
_wbint_LookupSids
_wbint_LookupName
_wbint_Sids2UnixIDs
_wbint_UnixIDs2Sids
_wbint_AllocateUid
_wbint_AllocateGid
_wbint_GetNssInfo
_wbint_LookupUserAliases
_wbint_LookupUserGroups
_wbint_QuerySequenceNumber
_wbint_LookupGroupMembers
_wbint_QueryGroupList
_wbint_QueryUserRidList
_wbint_DsGetDcName
_wbint_LookupRids
_wbint_CheckMachineAccount
_wbint_ChangeMachineAccount
_wbint_PingDc"

async_funcs="wb_ping
winbindd_lookupsid
winbindd_lookupsids
winbindd_lookupname
winbindd_sids_to_xids
winbindd_xids_to_sids
winbindd_getpwsid
winbindd_getpwnam
winbindd_getpwuid
winbindd_getsidaliases
winbindd_getuserdomgroups
winbindd_getgroups
winbindd_show_sequence
winbindd_getgrgid
winbindd_getgrnam
winbindd_getusersids
winbindd_lookuprids
winbindd_setpwent
winbindd_getpwent
winbindd_endpwent
winbindd_dsgetdcname
winbindd_getdcname
winbindd_setgrent
winbindd_getgrent
winbindd_endgrent
winbindd_list_users
winbindd_list_groups
winbindd_check_machine_acct
winbindd_ping_dc
winbindd_pam_auth
winbindd_pam_logoff
winbindd_pam_chauthtok
winbindd_pam_chng_pswd_auth_crap
winbindd_wins_byip
winbindd_wins_byname
winbindd_allocate_uid
winbindd_allocate_gid
winbindd_change_machine_acct
winbindd_pam_auth_crap"

backend_funcs="query_user_list
enum_dom_groups
enum_local_groups
name_to_sid
sid_to_name
rids_to_names
lookup_usergroups
lookup_useraliases
lookup_groupmem
sequence_number
lockout_policy
password_policy
trusted_domains"

header='#!/usr/bin/stap
#
# Systemtap script to instrument winbindd
#
'"# Generated by examples/systemtap/$(basename $0) on $(date), do not edit
#"'
# Usage:
#
# Instrument all winbindd processes:
# # stap winbindd.stp
#
# Instrument a specific winbindd process:
# # stap -x PID winbindd.stp
#

global dc_running, dc_svctime
global backend_running, backend_svctime
global send_running, recv_running
global start_time, idle_time
global async_svctime, async_runtime

probe begin {
	printf("Collecting data, press ctrl-C to stop... ")
}'

domchild_req_template='
#
# winbind domain child function XXX
#

probe process("winbindd").function("XXX") {
	dc_running[tid(), "XXX"] = gettimeofday_us()
}

probe process("winbindd").function("XXX").return {
	if (!([tid(), "XXX"] in dc_running))
		next

	end = gettimeofday_us()
	begin = dc_running[tid(), "XXX"]
	delete dc_running[tid(), "XXX"]

	duration = end - begin
	dc_svctime["XXX"] <<< duration
}'

backend_req_template='
#
# winbind domain child backend function XXX
#

probe process("winbindd").function("XXX@../source3/winbindd/winbindd_ads.c") {
	backend_running[tid(), "XXX"] = gettimeofday_us()
}

probe process("winbindd").function("XXX@../source3/winbindd/winbindd_ads.c").return {
	if (!([tid(), "XXX"] in backend_running))
		next

	end = gettimeofday_us()
	begin = backend_running[tid(), "XXX"]
	delete backend_running[tid(), "XXX"]

	duration = end - begin
	backend_svctime["XXX"] <<< duration
}'

async_req_template='
#
# winbind async function XXX
#

probe process("winbindd").function("XXX_send") {
	send_running["XXX_send"] = gettimeofday_us()
}

probe process("winbindd").function("XXX_send").return {
	if (!(["XXX_send"] in send_running))
		next

	end = gettimeofday_us()
	start = send_running["XXX_send"]
	delete send_running["XXX_send"]

	start_time["XXX_send", $return] = start
	idle_time["XXX_send", $return] = end
}

probe process("winbindd").function("XXX_recv") {
	if (!(["XXX_send", $req] in start_time))
		next

	recv_running["XXX_recv"] = gettimeofday_us()
}

probe process("winbindd").function("XXX_recv").return {
	if (!(["XXX_recv"] in recv_running))
		next

	recv_end = gettimeofday_us()
	recv_start = recv_running["XXX_recv"]
	delete recv_running["XXX_recv"]
	recv_runtime = recv_end - recv_start

	req = @entry($req)

	send_begin = start_time["XXX_send", req]
	delete start_time["XXX_send", req]
	svctime = recv_end - send_begin

	idle = idle_time["XXX_send", req]
	delete idle_time["XXX_send", req]
	runtime = (idle - send_begin) + recv_runtime

	async_svctime["XXX_send"] <<< svctime
	async_runtime["XXX_send"] <<< runtime
}'

footer='
probe end {
	printf("\n\n")

	printf("Winbind request service time\n")
	printf("============================\n")
	foreach ([name] in async_svctime) {
		printf("%-40s count: %5d, sum: %6d ms (min: %6d us, avg: %6d us, max: %6d us)\n",
		       name,
		       @count(async_svctime[name]),
		       @sum(async_svctime[name]) / 1000,
		       @min(async_svctime[name]),
		       @avg(async_svctime[name]),
		       @max(async_svctime[name]))
	}
	printf("\n")

	printf("Winbind request runtime\n")
	printf("=======================\n")
	foreach ([name] in async_runtime) {
		printf("%-40s count: %5d, sum: %6d ms (min: %6d us, avg: %6d us, max: %6d us)\n",
		       name,
		       @count(async_runtime[name]),
		       @sum(async_runtime[name]) / 1000,
		       @min(async_runtime[name]),
		       @avg(async_runtime[name]),
		       @max(async_runtime[name]))
	}
	printf("\n")

	printf("Winbind domain-child request service time\n")
	printf("=========================================\n")
	foreach ([name] in dc_svctime) {
		printf("%-40s count: %5d, sum: %6d ms (min: %6d us, avg: %6d us, max: %6d us)\n",
		       name,
		       @count(dc_svctime[name]),
		       @sum(dc_svctime[name]) / 1000,
		       @min(dc_svctime[name]),
		       @avg(dc_svctime[name]),
		       @max(dc_svctime[name]))
	}
	printf("\n")

	printf("Winbind domain-child AD-backend service time\n")
	printf("============================================\n")
	foreach ([name] in backend_svctime) {
		printf("%-40s count: %5d, sum: %6d ms (min: %6d us, avg: %6d us, max: %6d us)\n",
		       name,
		       @count(backend_svctime[name]),
		       @sum(backend_svctime[name]) / 1000,
		       @min(backend_svctime[name]),
		       @avg(backend_svctime[name]),
		       @max(backend_svctime[name]))
	}
	printf("\n")

	printf("Winbind request service time distributions (us)\n")
	printf("===============================================\n")
	foreach ([name] in async_svctime) {
		printf("%s:\n", name);
		println(@hist_log(async_svctime[name]))
	}
	printf("\n")

	printf("Winbind request runtime distributions (us)\n")
	printf("==========================================\n")
	foreach ([name] in async_runtime) {
		printf("%s:\n", name);
		println(@hist_log(async_runtime[name]))
	}

	printf("Winbind domain-child request service time distributions (us)\n")
	printf("============================================================\n")
	foreach ([name] in dc_svctime) {
		printf("%s:\n", name);
		println(@hist_log(dc_svctime[name]))
	}

	printf("Winbind domain-child AD-backend service time distributions (us)\n")
	printf("===============================================================\n")
	foreach ([name] in backend_svctime) {
		printf("%s:\n", name);
		println(@hist_log(backend_svctime[name]))
	}
}'

cat <<EOF >$outfile
$header
EOF

printf "$child_funcs\n" | while read func; do
	printf "$domchild_req_template\n" | sed -e s/XXX/$func/g >>$outfile
done

printf "$backend_funcs\n" | while read func; do
	printf "$backend_req_template\n" | sed -e "s|XXX|$func|g" >>$outfile
done

printf "$async_funcs\n" | while read func; do
	printf "$async_req_template\n" | sed -e s/XXX/$func/g >>$outfile
done

cat <<EOF >>$outfile
$footer
EOF
