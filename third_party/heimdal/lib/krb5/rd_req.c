
/*
 * Copyright (c) 1997 - 2007 Kungliga Tekniska Högskolan
 * (Royal Institute of Technology, Stockholm, Sweden).
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 *
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * 3. Neither the name of the Institute nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE INSTITUTE AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE INSTITUTE OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#include "krb5_locl.h"

static krb5_error_code
decrypt_tkt_enc_part (krb5_context context,
		      krb5_keyblock *key,
		      EncryptedData *enc_part,
		      EncTicketPart *decr_part)
{
    krb5_error_code ret;
    krb5_data plain;
    size_t len;
    krb5_crypto crypto;

    ret = krb5_crypto_init(context, key, 0, &crypto);
    if (ret)
	return ret;
    ret = krb5_decrypt_EncryptedData (context,
				      crypto,
				      KRB5_KU_TICKET,
				      enc_part,
				      &plain);
    krb5_crypto_destroy(context, crypto);
    if (ret)
	return ret;

    ret = decode_EncTicketPart(plain.data, plain.length, decr_part, &len);
    if (ret)
        krb5_set_error_message(context, ret,
			       N_("Failed to decode encrypted "
				  "ticket part", ""));
    krb5_data_free (&plain);
    return ret;
}

static krb5_error_code
decrypt_authenticator (krb5_context context,
		       EncryptionKey *key,
		       EncryptedData *enc_part,
		       Authenticator *authenticator,
		       krb5_key_usage usage)
{
    krb5_error_code ret;
    krb5_data plain;
    size_t len;
    krb5_crypto crypto;

    ret = krb5_crypto_init(context, key, 0, &crypto);
    if (ret)
	return ret;
    ret = krb5_decrypt_EncryptedData (context,
				      crypto,
				      usage /* KRB5_KU_AP_REQ_AUTH */,
				      enc_part,
				      &plain);
    /* for backwards compatibility, also try the old usage */
    if (ret && usage == KRB5_KU_TGS_REQ_AUTH)
	ret = krb5_decrypt_EncryptedData (context,
					  crypto,
					  KRB5_KU_AP_REQ_AUTH,
					  enc_part,
					  &plain);
    krb5_crypto_destroy(context, crypto);
    if (ret)
	return ret;

    ret = decode_Authenticator(plain.data, plain.length,
			       authenticator, &len);
    krb5_data_free (&plain);
    return ret;
}

KRB5_LIB_FUNCTION krb5_error_code KRB5_LIB_CALL
krb5_decode_ap_req(krb5_context context,
		   const krb5_data *inbuf,
		   krb5_ap_req *ap_req)
{
    krb5_error_code ret;
    size_t len;
    ret = decode_AP_REQ(inbuf->data, inbuf->length, ap_req, &len);
    if (ret)
	return ret;
    if (ap_req->pvno != 5){
	free_AP_REQ(ap_req);
	krb5_clear_error_message (context);
	return KRB5KRB_AP_ERR_BADVERSION;
    }
    if (ap_req->msg_type != krb_ap_req){
	free_AP_REQ(ap_req);
	krb5_clear_error_message (context);
	return KRB5KRB_AP_ERR_MSG_TYPE;
    }
    if (ap_req->ticket.tkt_vno != 5){
	free_AP_REQ(ap_req);
	krb5_clear_error_message (context);
	return KRB5KRB_AP_ERR_BADVERSION;
    }
    return 0;
}

static krb5_error_code
check_transited(krb5_context context, Ticket *ticket, EncTicketPart *enc)
{
    char **realms;
    unsigned int num_realms, n;
    krb5_error_code ret;

    /*
     * Windows 2000 and 2003 uses this inside their TGT so it's normaly
     * not seen by others, however, samba4 joined with a Windows AD as
     * a Domain Controller gets exposed to this.
     */
    if(enc->transited.tr_type == 0 && enc->transited.contents.length == 0)
	return 0;

    if(enc->transited.tr_type != domain_X500_Compress)
	return KRB5KDC_ERR_TRTYPE_NOSUPP;

    if(enc->transited.contents.length == 0)
	return 0;

    ret = krb5_domain_x500_decode(context, enc->transited.contents,
				  &realms, &num_realms,
				  enc->crealm,
				  ticket->realm);
    if(ret)
	return ret;
    ret = krb5_check_transited(context, enc->crealm,
			       ticket->realm,
			       realms, num_realms, NULL);
    for (n = 0; n < num_realms; n++)
	free(realms[n]);
    free(realms);
    return ret;
}

static krb5_error_code
find_etypelist(krb5_context context,
	       krb5_auth_context auth_context,
	       EtypeList *etypes)
{
    krb5_error_code ret;
    krb5_data data;
  
    ret = _krb5_get_ad(context, auth_context->authenticator->authorization_data, NULL, KRB5_AUTHDATA_GSS_API_ETYPE_NEGOTIATION, &data);
    if (ret)
  	return 0;
    
    ret = decode_EtypeList(data.data, data.length, etypes, NULL);
    krb5_data_free(&data);
    if (ret)
  	krb5_clear_error_message(context);
    
    return ret;
}

KRB5_LIB_FUNCTION krb5_error_code KRB5_LIB_CALL
krb5_decrypt_ticket(krb5_context context,
		    Ticket *ticket,
		    krb5_keyblock *key,
		    EncTicketPart *out,
		    krb5_flags flags)
{
    EncTicketPart t;
    krb5_error_code ret;
    ret = decrypt_tkt_enc_part (context, key, &ticket->enc_part, &t);
    if (ret)
	return ret;

    {
	krb5_timestamp now;
	time_t start = t.authtime;

	krb5_timeofday (context, &now);
	if(t.starttime)
	    start = *t.starttime;
	if(start - now > context->max_skew
	   || (t.flags.invalid
	       && !(flags & KRB5_VERIFY_AP_REQ_IGNORE_INVALID))) {
	    free_EncTicketPart(&t);
	    krb5_clear_error_message (context);
	    return KRB5KRB_AP_ERR_TKT_NYV;
	}
	if(now - t.endtime > context->max_skew) {
	    free_EncTicketPart(&t);
	    krb5_clear_error_message (context);
	    return KRB5KRB_AP_ERR_TKT_EXPIRED;
	}

	if(!t.flags.transited_policy_checked) {
	    ret = check_transited(context, ticket, &t);
	    if(ret) {
		free_EncTicketPart(&t);
		return ret;
	    }
	}
    }

    if(out)
	*out = t;
    else
	free_EncTicketPart(&t);
    return 0;
}

KRB5_LIB_FUNCTION krb5_error_code KRB5_LIB_CALL
krb5_verify_authenticator_checksum(krb5_context context,
				   krb5_auth_context ac,
				   void *data,
				   size_t len)
{
    krb5_error_code ret;
    krb5_keyblock *key = NULL;
    krb5_authenticator authenticator;
    krb5_crypto crypto;

    ret = krb5_auth_con_getauthenticator(context, ac, &authenticator);
    if (ret)
	return ret;
    if (authenticator->cksum == NULL) {
	ret = -17;
        goto out;
    }
    ret = krb5_auth_con_getkey(context, ac, &key);
    if (ret)
        goto out;
    ret = krb5_crypto_init(context, key, 0, &crypto);
    if (ret)
	goto out;

    _krb5_crypto_set_flags(context, crypto, KRB5_CRYPTO_FLAG_ALLOW_UNKEYED_CHECKSUM);
    ret = krb5_verify_checksum(context, crypto,
                               KRB5_KU_AP_REQ_AUTH_CKSUM,
                               data, len, authenticator->cksum);
    krb5_crypto_destroy(context, crypto);
out:
    krb5_free_authenticator(context, &authenticator);
    krb5_free_keyblock(context, key);
    return ret;
}


KRB5_LIB_FUNCTION krb5_error_code KRB5_LIB_CALL
krb5_verify_ap_req(krb5_context context,
		   krb5_auth_context *auth_context,
		   krb5_ap_req *ap_req,
		   krb5_const_principal server,
		   krb5_keyblock *keyblock,
		   krb5_flags flags,
		   krb5_flags *ap_req_options,
		   krb5_ticket **ticket)
{
    return krb5_verify_ap_req2 (context,
				auth_context,
				ap_req,
				server,
				keyblock,
				flags,
				ap_req_options,
				ticket,
				KRB5_KU_AP_REQ_AUTH);
}

KRB5_LIB_FUNCTION krb5_error_code KRB5_LIB_CALL
krb5_verify_ap_req2(krb5_context context,
		    krb5_auth_context *auth_context,
		    krb5_ap_req *ap_req,
		    krb5_const_principal server,
		    krb5_keyblock *keyblock,
		    krb5_flags flags,
		    krb5_flags *ap_req_options,
		    krb5_ticket **ticket,
		    krb5_key_usage usage)
{
    krb5_ticket *t;
    krb5_auth_context ac;
    krb5_error_code ret;
    EtypeList etypes;
    int badaddr = 0;

    memset(&etypes, 0, sizeof(etypes));

    if (ticket)
	*ticket = NULL;

    if (auth_context && *auth_context) {
	ac = *auth_context;
    } else {
	ret = krb5_auth_con_init (context, &ac);
	if (ret)
	    return ret;
    }

    t = calloc(1, sizeof(*t));
    if (t == NULL) {
	ret = krb5_enomem(context);
	goto out;
    }

    if (ap_req->ap_options.use_session_key && ac->keyblock){
	ret = krb5_decrypt_ticket(context, &ap_req->ticket,
				  ac->keyblock,
				  &t->ticket,
				  flags);
	krb5_free_keyblock(context, ac->keyblock);
	ac->keyblock = NULL;
    }else
	ret = krb5_decrypt_ticket(context, &ap_req->ticket,
				  keyblock,
				  &t->ticket,
				  flags);

    if(ret)
	goto out;

    ret = _krb5_principalname2krb5_principal(context,
					     &t->server,
					     ap_req->ticket.sname,
					     ap_req->ticket.realm);
    if (ret) goto out;
    ret = _krb5_principalname2krb5_principal(context,
					     &t->client,
					     t->ticket.cname,
					     t->ticket.crealm);
    if (ret) goto out;

    ret = decrypt_authenticator (context,
				 &t->ticket.key,
				 &ap_req->authenticator,
				 ac->authenticator,
				 usage);
    if (ret)
	goto out;

    {
	krb5_principal p1, p2;
	krb5_boolean res;

	_krb5_principalname2krb5_principal(context,
					   &p1,
					   ac->authenticator->cname,
					   ac->authenticator->crealm);
	_krb5_principalname2krb5_principal(context,
					   &p2,
					   t->ticket.cname,
					   t->ticket.crealm);
	res = krb5_principal_compare (context, p1, p2);
	krb5_free_principal (context, p1);
	krb5_free_principal (context, p2);
	if (!res) {
	    ret = KRB5KRB_AP_ERR_BADMATCH;
	    krb5_clear_error_message (context);
	    goto out;
	}
    }

    /* check addresses */

    if (t->ticket.caddr
	&& ac->remote_address
	&& !krb5_address_search (context,
				 ac->remote_address,
				 t->ticket.caddr)) {
        /*
         * Hack alert.  If KRB5_VERIFY_AP_REQ_IGNORE_ADDRS and the client's
         * address didn't check out then we'll return KRB5KRB_AP_ERR_BADADDR
         * even on success, and we'll let the caller figure it out because
         * `*ticket != NULL' or `*auth_context != NULL'.
         */
        if ((flags & KRB5_VERIFY_AP_REQ_IGNORE_ADDRS)) {
            badaddr = 1;
        } else {
            ret = KRB5KRB_AP_ERR_BADADDR;
            krb5_clear_error_message(context);
            goto out;
        }
    }

    /* check timestamp in authenticator */
    {
	krb5_timestamp now;

	krb5_timeofday (context, &now);

	if (krb5_time_abs(ac->authenticator->ctime, now) > context->max_skew) {
	    ret = KRB5KRB_AP_ERR_SKEW;
	    krb5_clear_error_message (context);
	    goto out;
	}
    }

    if (ac->authenticator->seq_number)
	krb5_auth_con_setremoteseqnumber(context, ac,
					 *ac->authenticator->seq_number);

    /* XXX - Xor sequence numbers */

    if (ac->authenticator->subkey) {
	ret = krb5_auth_con_setremotesubkey(context, ac,
					    ac->authenticator->subkey);
	if (ret)
	    goto out;
    }

    ret = find_etypelist(context, ac, &etypes);
    if (ret)
	goto out;

    ac->keytype = ETYPE_NULL;

    if (etypes.val) {
	size_t i;

	for (i = 0; i < etypes.len; i++) {
	    if (krb5_enctype_valid(context, etypes.val[i]) == 0) {
		ac->keytype = etypes.val[i];
		break;
	    }
	}
    }

    /* save key */
    ret = krb5_copy_keyblock(context, &t->ticket.key, &ac->keyblock);
    if (ret) goto out;

    if (ap_req_options) {
	*ap_req_options = 0;
	if (ac->keytype != (krb5_enctype)ETYPE_NULL)
	    *ap_req_options |= AP_OPTS_USE_SUBKEY;
	if (ap_req->ap_options.use_session_key)
	    *ap_req_options |= AP_OPTS_USE_SESSION_KEY;
	if (ap_req->ap_options.mutual_required)
	    *ap_req_options |= AP_OPTS_MUTUAL_REQUIRED;
    }

    if(ticket)
	*ticket = t;
    else
	krb5_free_ticket (context, t);
    if (auth_context) {
	if (*auth_context == NULL)
	    *auth_context = ac;
    } else
	krb5_auth_con_free (context, ac);
    free_EtypeList(&etypes);

    if (badaddr) {
        krb5_clear_error_message(context);
        return KRB5KRB_AP_ERR_BADADDR;
    }
    return 0;
 out:
    free_EtypeList(&etypes);
    if (t)
	krb5_free_ticket (context, t);
    if (auth_context == NULL || *auth_context == NULL)
	krb5_auth_con_free (context, ac);
    return ret;
}

/*
 *
 */

struct krb5_rd_req_in_ctx_data {
    krb5_keytab keytab;
    krb5_keyblock *keyblock;
    krb5_boolean check_pac;
};

struct krb5_rd_req_out_ctx_data {
    krb5_keyblock *keyblock;
    krb5_flags ap_req_options;
    krb5_ticket *ticket;
    krb5_principal server;
};

/**
 * Allocate a krb5_rd_req_in_ctx as an input parameter to
 * krb5_rd_req_ctx(). The caller should free the context with
 * krb5_rd_req_in_ctx_free() when done with the context.
 *
 * @param context Keberos 5 context.
 * @param ctx in ctx to krb5_rd_req_ctx().
 *
 * @return Kerberos 5 error code, see krb5_get_error_message().
 *
 * @ingroup krb5_auth
 */

KRB5_LIB_FUNCTION krb5_error_code KRB5_LIB_CALL
krb5_rd_req_in_ctx_alloc(krb5_context context, krb5_rd_req_in_ctx *ctx)
{
    *ctx = calloc(1, sizeof(**ctx));
    if (*ctx == NULL)
	return krb5_enomem(context);
    (*ctx)->check_pac = (context->flags & KRB5_CTX_F_CHECK_PAC) ? 1 : 0;
    return 0;
}

/**
 * Set the keytab that krb5_rd_req_ctx() will use.
 *
 * @param context Keberos 5 context.
 * @param in in ctx to krb5_rd_req_ctx().
 * @param keytab keytab that krb5_rd_req_ctx() will use, only copy the
 *        pointer, so the caller must free they keytab after
 *        krb5_rd_req_in_ctx_free() is called.
 *
 * @return Kerberos 5 error code, see krb5_get_error_message().
 *
 * @ingroup krb5_auth
 */

KRB5_LIB_FUNCTION krb5_error_code KRB5_LIB_CALL
krb5_rd_req_in_set_keytab(krb5_context context,
			  krb5_rd_req_in_ctx in,
			  krb5_keytab keytab)
{
    in->keytab = keytab;
    return 0;
}

/**
 * Set if krb5_rq_red() is going to check the Windows PAC or not
 *
 * @param context Keberos 5 context.
 * @param in krb5_rd_req_in_ctx to check the option on.
 * @param flag flag to select if to check the pac (TRUE) or not (FALSE).
 *
 * @return Kerberos 5 error code, see krb5_get_error_message().
 *
 * @ingroup krb5_auth
 */

KRB5_LIB_FUNCTION krb5_error_code KRB5_LIB_CALL
krb5_rd_req_in_set_pac_check(krb5_context context,
			     krb5_rd_req_in_ctx in,
			     krb5_boolean flag)
{
    in->check_pac = flag;
    return 0;
}


KRB5_LIB_FUNCTION krb5_error_code KRB5_LIB_CALL
krb5_rd_req_in_set_keyblock(krb5_context context,
			    krb5_rd_req_in_ctx in,
			    krb5_keyblock *keyblock)
{
    in->keyblock = keyblock; /* XXX should make copy */
    return 0;
}

KRB5_LIB_FUNCTION krb5_error_code KRB5_LIB_CALL
krb5_rd_req_out_get_ap_req_options(krb5_context context,
				   krb5_rd_req_out_ctx out,
				   krb5_flags *ap_req_options)
{
    *ap_req_options = out->ap_req_options;
    return 0;
}

KRB5_LIB_FUNCTION krb5_error_code KRB5_LIB_CALL
krb5_rd_req_out_get_ticket(krb5_context context,
			    krb5_rd_req_out_ctx out,
			    krb5_ticket **ticket)
{
    return krb5_copy_ticket(context, out->ticket, ticket);
}

KRB5_LIB_FUNCTION krb5_error_code KRB5_LIB_CALL
krb5_rd_req_out_get_keyblock(krb5_context context,
			    krb5_rd_req_out_ctx out,
			    krb5_keyblock **keyblock)
{
    return krb5_copy_keyblock(context, out->keyblock, keyblock);
}

/**
 * Get the principal that was used in the request from the
 * client. Might not match whats in the ticket if krb5_rd_req_ctx()
 * searched in the keytab for a matching key.
 *
 * @param context a Kerberos 5 context.
 * @param out a krb5_rd_req_out_ctx from krb5_rd_req_ctx().
 * @param principal return principal, free with krb5_free_principal().
 *
 * @ingroup krb5_auth
 */

KRB5_LIB_FUNCTION krb5_error_code KRB5_LIB_CALL
krb5_rd_req_out_get_server(krb5_context context,
			    krb5_rd_req_out_ctx out,
			    krb5_principal *principal)
{
    return krb5_copy_principal(context, out->server, principal);
}

KRB5_LIB_FUNCTION void KRB5_LIB_CALL
krb5_rd_req_in_ctx_free(krb5_context context, krb5_rd_req_in_ctx ctx)
{
    free(ctx);
}

/**
 * Free the krb5_rd_req_out_ctx.
 *
 * @param context Keberos 5 context.
 * @param ctx krb5_rd_req_out_ctx context to free.
 *
 * @ingroup krb5_auth
 */

KRB5_LIB_FUNCTION void KRB5_LIB_CALL
krb5_rd_req_out_ctx_free(krb5_context context, krb5_rd_req_out_ctx ctx)
{
    if (ctx->ticket)
	krb5_free_ticket(context, ctx->ticket);
    if (ctx->keyblock)
	krb5_free_keyblock(context, ctx->keyblock);
    if (ctx->server)
	krb5_free_principal(context, ctx->server);
    free(ctx);
}

/**
 * Process an AP_REQ message.
 *
 * @param context        Kerberos 5 context.
 * @param auth_context   authentication context of the peer.
 * @param inbuf          the AP_REQ message, obtained for example with krb5_read_message().
 * @param server         server principal.
 * @param keytab         server keytab.
 * @param ap_req_options set to the AP_REQ options. See the AP_OPTS_* defines.
 * @param ticket         on success, set to the authenticated client credentials.
 *                       Must be deallocated with krb5_free_ticket(). If not
 *                       interested, pass a NULL value.
 *
 * @return 0 to indicate success. Otherwise a Kerberos error code is
 *         returned, see krb5_get_error_message().
 */
KRB5_LIB_FUNCTION krb5_error_code KRB5_LIB_CALL
krb5_rd_req(krb5_context context,
	    krb5_auth_context *auth_context,
	    const krb5_data *inbuf,
	    krb5_const_principal server,
	    krb5_keytab keytab,
	    krb5_flags *ap_req_options,
	    krb5_ticket **ticket)
{
    krb5_error_code ret;
    krb5_rd_req_in_ctx in;
    krb5_rd_req_out_ctx out;

    ret = krb5_rd_req_in_ctx_alloc(context, &in);
    if (ret)
	return ret;

    ret = krb5_rd_req_in_set_keytab(context, in, keytab);
    if (ret) {
	krb5_rd_req_in_ctx_free(context, in);
	return ret;
    }

    ret = krb5_rd_req_ctx(context, auth_context, inbuf, server, in, &out);
    krb5_rd_req_in_ctx_free(context, in);
    if (ret)
	return ret;

    if (ap_req_options)
	*ap_req_options = out->ap_req_options;
    if (ticket) {
	ret = krb5_copy_ticket(context, out->ticket, ticket);
	if (ret)
	    goto out;
    }

out:
    krb5_rd_req_out_ctx_free(context, out);
    return ret;
}

/*
 *
 */

KRB5_LIB_FUNCTION krb5_error_code KRB5_LIB_CALL
krb5_rd_req_with_keyblock(krb5_context context,
			  krb5_auth_context *auth_context,
			  const krb5_data *inbuf,
			  krb5_const_principal server,
			  krb5_keyblock *keyblock,
			  krb5_flags *ap_req_options,
			  krb5_ticket **ticket)
{
    krb5_error_code ret;
    krb5_rd_req_in_ctx in;
    krb5_rd_req_out_ctx out;

    ret = krb5_rd_req_in_ctx_alloc(context, &in);
    if (ret)
	return ret;

    ret = krb5_rd_req_in_set_keyblock(context, in, keyblock);
    if (ret) {
	krb5_rd_req_in_ctx_free(context, in);
	return ret;
    }

    ret = krb5_rd_req_ctx(context, auth_context, inbuf, server, in, &out);
    krb5_rd_req_in_ctx_free(context, in);
    if (ret)
	return ret;

    if (ap_req_options)
	*ap_req_options = out->ap_req_options;
    if (ticket) {
	ret = krb5_copy_ticket(context, out->ticket, ticket);
	if (ret)
	    goto out;
    }

out:
    krb5_rd_req_out_ctx_free(context, out);
    return ret;
}

/*
 *
 */

static krb5_error_code
get_key_from_keytab(krb5_context context,
		    krb5_ap_req *ap_req,
		    krb5_const_principal server,
		    krb5_keytab keytab,
		    krb5_keyblock **out_key)
{
    krb5_keytab_entry entry;
    krb5_error_code ret;
    int kvno;
    krb5_keytab real_keytab;

    if(keytab == NULL)
	krb5_kt_default(context, &real_keytab);
    else
	real_keytab = keytab;

    if (ap_req->ticket.enc_part.kvno)
	kvno = *ap_req->ticket.enc_part.kvno;
    else
	kvno = 0;

    ret = krb5_kt_get_entry (context,
			     real_keytab,
			     server,
			     kvno,
			     ap_req->ticket.enc_part.etype,
			     &entry);
    if(ret)
	goto out;
    ret = krb5_copy_keyblock(context, &entry.keyblock, out_key);
    krb5_kt_free_entry (context, &entry);
out:
    if(keytab == NULL)
	krb5_kt_close(context, real_keytab);

    return ret;
}

/**
 * The core server function that verify application authentication
 * requests from clients.
 *
 * @param context Keberos 5 context.
 * @param auth_context the authentication context, can be NULL, then
 *        default values for the authentication context will used.
 * @param inbuf the (AP-REQ) authentication buffer
 *
 * @param server the server to authenticate to. If NULL the function
 *        will try to find any available credential in the keytab
 *        that will verify the reply. The function will prefer the
 *        server specified in the AP-REQ, but if
 *        there is no mach, it will try all keytab entries for a
 *        match. This has serious performance issues for large keytabs.
 *
 * @param inctx control the behavior of the function, if NULL, the
 *        default behavior is used.
 * @param outctx the return outctx, free with krb5_rd_req_out_ctx_free().
 * @return Kerberos 5 error code, see krb5_get_error_message().
 *
 * @ingroup krb5_auth
 */

KRB5_LIB_FUNCTION krb5_error_code KRB5_LIB_CALL
krb5_rd_req_ctx(krb5_context context,
		krb5_auth_context *auth_context,
		const krb5_data *inbuf,
		krb5_const_principal server,
		krb5_rd_req_in_ctx inctx,
		krb5_rd_req_out_ctx *outctx)
{
    krb5_error_code ret;
    krb5_ap_req ap_req;
    krb5_rd_req_out_ctx o = NULL;
    krb5_keytab id = NULL, keytab = NULL;
    krb5_principal service = NULL;

    *outctx = NULL;

    o = calloc(1, sizeof(*o));
    if (o == NULL)
	return krb5_enomem(context);

    if (*auth_context == NULL) {
	ret = krb5_auth_con_init(context, auth_context);
	if (ret)
	    goto out;
    }

    ret = krb5_decode_ap_req(context, inbuf, &ap_req);
    if(ret)
	goto out;

    /* Save the principal that was in the request */
    ret = _krb5_principalname2krb5_principal(context,
					     &o->server,
					     ap_req.ticket.sname,
					     ap_req.ticket.realm);
    if (ret)
	goto out;

    if (ap_req.ap_options.use_session_key &&
	(*auth_context)->keyblock == NULL) {
	ret = KRB5KRB_AP_ERR_NOKEY;
	krb5_set_error_message(context, ret,
			       N_("krb5_rd_req: user to user auth "
				  "without session key given", ""));
	goto out;
    }

    if (inctx && inctx->keytab)
	id = inctx->keytab;

    if((*auth_context)->keyblock){
	ret = krb5_copy_keyblock(context,
				 (*auth_context)->keyblock,
				 &o->keyblock);
	if (ret)
	    goto out;
    } else if(inctx && inctx->keyblock){
	ret = krb5_copy_keyblock(context,
				 inctx->keyblock,
				 &o->keyblock);
	if (ret)
	    goto out;
    } else {

	if(id == NULL) {
	    krb5_kt_default(context, &keytab);
	    id = keytab;
	}
	if (id == NULL)
	    goto out;

	if (server == NULL) {
	    ret = _krb5_principalname2krb5_principal(context,
						     &service,
						     ap_req.ticket.sname,
						     ap_req.ticket.realm);
	    if (ret)
		goto out;
	    server = service;
	}

	ret = get_key_from_keytab(context,
				  &ap_req,
				  server,
				  id,
				  &o->keyblock);
	if (ret) {
	    /* If caller specified a server, fail. */
	    if (service == NULL && (context->flags & KRB5_CTX_F_RD_REQ_IGNORE) == 0)
		goto out;
	    /* Otherwise, fall back to iterating over the keytab. This
	     * have serious performace issues for larger keytab.
	     */
	    o->keyblock = NULL;
	}
    }

    if (o->keyblock) {
	/*
	 * We got an exact keymatch, use that.
	 */

	ret = krb5_verify_ap_req2(context,
				  auth_context,
				  &ap_req,
				  server,
				  o->keyblock,
				  0,
				  &o->ap_req_options,
				  &o->ticket,
				  KRB5_KU_AP_REQ_AUTH);

	if (ret)
	    goto out;

    } else {
	/*
	 * Interate over keytab to find a key that can decrypt the request.
	 */

	krb5_keytab_entry entry;
	krb5_kt_cursor cursor;
	int done = 0, kvno = 0;

	memset(&cursor, 0, sizeof(cursor));

	if (ap_req.ticket.enc_part.kvno)
	    kvno = *ap_req.ticket.enc_part.kvno;

	ret = krb5_kt_start_seq_get(context, id, &cursor);
	if (ret)
	    goto out;

	done = 0;
	while (!done) {
	    krb5_principal p;

	    ret = krb5_kt_next_entry(context, id, &entry, &cursor);
	    if (ret) {
		_krb5_kt_principal_not_found(context, ret, id, o->server,
					     ap_req.ticket.enc_part.etype,
					     kvno);
		break;
	    }

	    if (entry.keyblock.keytype != ap_req.ticket.enc_part.etype) {
		krb5_kt_free_entry (context, &entry);
		continue;
	    }

	    ret = krb5_verify_ap_req2(context,
				      auth_context,
				      &ap_req,
				      server,
				      &entry.keyblock,
				      0,
				      &o->ap_req_options,
				      &o->ticket,
				      KRB5_KU_AP_REQ_AUTH);
	    if (ret) {
		krb5_kt_free_entry (context, &entry);
		continue;
	    }

	    /*
	     * Found a match, save the keyblock for PAC processing,
	     * and update the service principal in the ticket to match
	     * whatever is in the keytab.
	     */

	    ret = krb5_copy_keyblock(context,
				     &entry.keyblock,
				     &o->keyblock);
	    if (ret) {
		krb5_kt_free_entry (context, &entry);
		break;
	    }

	    ret = krb5_copy_principal(context, entry.principal, &p);
	    if (ret) {
		krb5_kt_free_entry (context, &entry);
		break;
	    }
	    krb5_free_principal(context, o->ticket->server);
	    o->ticket->server = p;

	    krb5_kt_free_entry (context, &entry);

	    done = 1;
	}
	krb5_kt_end_seq_get (context, id, &cursor);
        if (ret)
            goto out;
    }

    /* If there is a PAC, verify its server signature */
    if (inctx == NULL || inctx->check_pac) {
	krb5_pac pac;
	krb5_data data;

	ret = krb5_ticket_get_authorization_data_type(context,
						      o->ticket,
						      KRB5_AUTHDATA_WIN2K_PAC,
						      &data);
	if (ret == 0) {
	    ret = krb5_pac_parse(context, data.data, data.length, &pac);
	    krb5_data_free(&data);
	    if (ret)
		goto out;

	    ret = krb5_pac_verify(context,
				  pac,
				  o->ticket->ticket.authtime,
				  o->ticket->client,
				  o->keyblock,
				  NULL);
	    if (ret == 0 && (context->flags & KRB5_CTX_F_REPORT_CANONICAL_CLIENT_NAME)) {
		krb5_error_code ret2;
		krb5_principal canon_name;

		ret2 = _krb5_pac_get_canon_principal(context, pac, &canon_name);
		if (ret2 == 0) {
		    krb5_free_principal(context, o->ticket->client);
		    o->ticket->client = canon_name;
		} else if (ret2 != ENOENT)
		    ret = ret2;
	    }
	    krb5_pac_free(context, pac);
	    if (ret)
		goto out;
	} else
	  ret = 0;
    }
out:

    if (ret || outctx == NULL) {
	krb5_rd_req_out_ctx_free(context, o);
    } else
	*outctx = o;

    free_AP_REQ(&ap_req);

    if (service)
	krb5_free_principal(context, service);

    if (keytab)
	krb5_kt_close(context, keytab);

    return ret;
}
