/*
 * Copyright (c) 1999 - 2003 Kungliga Tekniska Högskolan
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

#include "gsskrb5_locl.h"

OM_uint32 GSSAPI_CALLCONV
_gsskrb5_export_sec_context(
    OM_uint32 *minor_status,
    gss_ctx_id_t *context_handle,
    gss_buffer_t interprocess_token
    )
{
    krb5_context context;
    const gsskrb5_ctx ctx = (const gsskrb5_ctx) *context_handle;
    krb5_storage *sp;
    krb5_auth_context ac;
    OM_uint32 ret = GSS_S_COMPLETE;
    krb5_data data;
    int flags;
    OM_uint32 minor;
    krb5_error_code kret;

    GSSAPI_KRB5_INIT (&context);

    HEIMDAL_MUTEX_lock(&ctx->ctx_id_mutex);

    if (!(ctx->flags & GSS_C_TRANS_FLAG)) {
	HEIMDAL_MUTEX_unlock(&ctx->ctx_id_mutex);
	*minor_status = 0;
	return GSS_S_UNAVAILABLE;
    }

    sp = krb5_storage_emem ();
    if (sp == NULL) {
	HEIMDAL_MUTEX_unlock(&ctx->ctx_id_mutex);
	*minor_status = ENOMEM;
	return GSS_S_FAILURE;
    }
    ac = ctx->auth_context;

    krb5_storage_set_byteorder(sp, KRB5_STORAGE_BYTEORDER_PACKED);
    krb5_storage_set_flags(sp, KRB5_STORAGE_PRINCIPAL_NO_NAME_TYPE);

    /* flagging included fields */

    flags = 0;
    if (ac->local_address)
	flags |= SC_LOCAL_ADDRESS;
    if (ac->remote_address)
	flags |= SC_REMOTE_ADDRESS;
    if (ac->keyblock)
	flags |= SC_KEYBLOCK;
    if (ac->local_subkey)
	flags |= SC_LOCAL_SUBKEY;
    if (ac->remote_subkey)
	flags |= SC_REMOTE_SUBKEY;
    if (ac->authenticator)
	flags |= SC_AUTHENTICATOR;
    if (ctx->source)
	flags |= SC_SOURCE_NAME;
    if (ctx->target)
	flags |= SC_TARGET_NAME;
    if (ctx->order)
	flags |= SC_ORDER;

    kret = krb5_store_int32 (sp, flags);
    if (kret) {
	*minor_status = kret;
	goto failure;
    }

    /* marshall auth context */

    kret = krb5_store_int32 (sp, ac->flags);
    if (kret) {
	*minor_status = kret;
	goto failure;
    }
    if (ac->local_address) {
	kret = krb5_store_address (sp, *ac->local_address);
	if (kret) {
	    *minor_status = kret;
	    goto failure;
	}
    }
    if (ac->remote_address) {
	kret = krb5_store_address (sp, *ac->remote_address);
	if (kret) {
	    *minor_status = kret;
	    goto failure;
	}
    }
    kret = krb5_store_int16 (sp, ac->local_port);
    if (kret) {
	*minor_status = kret;
	goto failure;
    }
    kret = krb5_store_int16 (sp, ac->remote_port);
    if (kret) {
	*minor_status = kret;
	goto failure;
    }
    if (ac->keyblock) {
	kret = krb5_store_keyblock (sp, *ac->keyblock);
	if (kret) {
	    *minor_status = kret;
	    goto failure;
	}
    }
    if (ac->local_subkey) {
	kret = krb5_store_keyblock (sp, *ac->local_subkey);
	if (kret) {
	    *minor_status = kret;
	    goto failure;
	}
    }
    if (ac->remote_subkey) {
	kret = krb5_store_keyblock (sp, *ac->remote_subkey);
	if (kret) {
	    *minor_status = kret;
	    goto failure;
	}
    }
    kret = krb5_store_int32 (sp, ac->local_seqnumber);
	if (kret) {
	    *minor_status = kret;
	    goto failure;
	}
    kret = krb5_store_int32 (sp, ac->remote_seqnumber);
	if (kret) {
	    *minor_status = kret;
	    goto failure;
	}
    if (ac->authenticator) {
        kret = krb5_store_int64(sp, ac->authenticator->ctime);
        if (kret) {
            *minor_status = kret;
            goto failure;
        }
        kret = krb5_store_int32(sp, ac->authenticator->cusec);
        if (kret) {
            *minor_status = kret;
            goto failure;
        }
    }

    kret = krb5_store_int32 (sp, ac->keytype);
    if (kret) {
	*minor_status = kret;
	goto failure;
    }
    kret = krb5_store_int32 (sp, ac->cksumtype);
    if (kret) {
	*minor_status = kret;
	goto failure;
    }

    /* names */
    if (ctx->source) {
        kret = krb5_store_principal(sp, ctx->source);
	if (kret) {
	    *minor_status = kret;
	    goto failure;
	}
    }

    if (ctx->target) {
        kret = krb5_store_principal(sp, ctx->source);
	if (kret) {
	    *minor_status = kret;
	    goto failure;
	}
    }

    kret = krb5_store_int32 (sp, ctx->flags);
    if (kret) {
	*minor_status = kret;
	goto failure;
    }
    kret = krb5_store_int32 (sp, ctx->more_flags);
    if (kret) {
	*minor_status = kret;
	goto failure;
    }
    kret = krb5_store_int32 (sp, ctx->state);
    if (kret) {
        *minor_status = kret;
        goto failure;
    }
    /*
     * XXX We should put a 64-bit int here, but we don't have a
     * krb5_store_int64() yet.
     */
    kret = krb5_store_int32 (sp, ctx->endtime);
    if (kret) {
	*minor_status = kret;
	goto failure;
    }
    if (ctx->order) {
        kret = _gssapi_msg_order_export(sp, ctx->order);
        if (kret) {
            *minor_status = kret;
            goto failure;
        }
    }

    kret = krb5_storage_to_data (sp, &data);
    krb5_storage_free (sp);
    if (kret) {
	HEIMDAL_MUTEX_unlock(&ctx->ctx_id_mutex);
	*minor_status = kret;
	return GSS_S_FAILURE;
    }
    interprocess_token->length = data.length;
    interprocess_token->value  = data.data;
    HEIMDAL_MUTEX_unlock(&ctx->ctx_id_mutex);
    ret = _gsskrb5_delete_sec_context (minor_status, context_handle,
				       GSS_C_NO_BUFFER);
    if (ret != GSS_S_COMPLETE)
	_gss_secure_release_buffer (&minor, interprocess_token);
    *minor_status = 0;
    return ret;
 failure:
    HEIMDAL_MUTEX_unlock(&ctx->ctx_id_mutex);
    krb5_storage_free (sp);
    return ret;
}
