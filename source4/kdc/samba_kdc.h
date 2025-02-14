/*
   Unix SMB/CIFS implementation.

   KDC structures

   Copyright (C) Andrew Tridgell	2005
   Copyright (C) Andrew Bartlett <abartlet@samba.org> 2005
   Copyright (C) Simo Sorce <idra@samba.org> 2010

   This program is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation; either version 3 of the License, or
   (at your option) any later version.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program.  If not, see <http://www.gnu.org/licenses/>.
*/

#ifndef _SAMBA_KDC_H_
#define _SAMBA_KDC_H_

struct samba_kdc_policy {
	time_t svc_tkt_lifetime;
	time_t usr_tkt_lifetime;
	time_t renewal_lifetime;
};

struct samba_kdc_base_context {
	struct tevent_context *ev_ctx;
	struct loadparm_context *lp_ctx;
	struct imessaging_context *msg_ctx;
};

struct samba_kdc_seq;

struct samba_kdc_db_context {
	struct tevent_context *ev_ctx;
	struct loadparm_context *lp_ctx;
	struct imessaging_context *msg_ctx;
	struct ldb_context *samdb;
	struct samba_kdc_seq *seq_ctx;
	bool rodc;
	unsigned int my_krbtgt_number;
	struct ldb_dn *krbtgt_dn;
	struct samba_kdc_policy policy;
	struct ldb_dn *fx_cookie_dn;
	struct ldb_context *secrets_db;
};

struct samba_kdc_entry {
	struct samba_kdc_db_context *kdc_db_ctx;
	struct ldb_message *msg;
	struct ldb_dn *realm_dn;
	bool is_krbtgt;
	bool is_rodc;
	bool is_trust;
	void *entry_ex;
	uint32_t supported_enctypes;
};

extern struct hdb_method hdb_samba4_interface;

#endif /* _SAMBA_KDC_H_ */
