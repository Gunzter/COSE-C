#include <stdlib.h>
#include <memory.h>
#include <stdio.h>
#include <assert.h>
#include <string.h>

#include "cose.h"
#include "cose_int.h"
#include "configure.h"
#include "crypto.h"

COSE * Mac0Root = NULL;

bool IsValidMac0Handle(HCOSE_MAC0 h)
{
	COSE_Mac0Message * p = (COSE_Mac0Message *)h;
	return _COSE_IsInList(Mac0Root, (COSE *) p);
}

HCOSE_MAC0 COSE_Mac0_Init(CBOR_CONTEXT_COMMA cose_errback * perr)
{
	COSE_Mac0Message * pobj = (COSE_Mac0Message *)COSE_CALLOC(1, sizeof(COSE_Mac0Message), context);
	CHECK_CONDITION(pobj != NULL, COSE_ERR_OUT_OF_MEMORY);

	if (!_COSE_Init(&pobj->m_message, COSE_mac0_object, CBOR_CONTEXT_PARAM_COMMA perr)) {
		goto errorReturn;
	}

	_COSE_InsertInList(&Mac0Root, &pobj->m_message);

	return (HCOSE_MAC0)pobj;

errorReturn:
	if (pobj != NULL) {
		_COSE_Mac0_Release(pobj);
		COSE_FREE(pobj, context);
	}
	return NULL;
}

HCOSE_MAC0 _COSE_Mac0_Init_From_Object(cn_cbor * cbor, COSE_Mac0Message * pIn, CBOR_CONTEXT_COMMA cose_errback * perr)
{
	COSE_Mac0Message * pobj = pIn;
	cn_cbor * pRecipients = NULL;
	// cn_cbor * tmp;
	cose_errback error = { COSE_ERR_NONE };
	if (perr == NULL) perr = &error;

	if (pobj == NULL) pobj = (COSE_Mac0Message *)COSE_CALLOC(1, sizeof(COSE_Mac0Message), context);
	if (pobj == NULL) {
		perr->err = COSE_ERR_OUT_OF_MEMORY;
	errorReturn:
		if (pobj != NULL) {
			_COSE_Mac0_Release(pobj);
			if (pIn == NULL) {
				COSE_FREE(pobj, context);
			}
		}
		return NULL;
	}

	if (!_COSE_Init_From_Object(&pobj->m_message, cbor, CBOR_CONTEXT_PARAM_COMMA perr)) {
		goto errorReturn;
	}

	pRecipients = _COSE_arrayget_int(&pobj->m_message, INDEX_MAC_RECIPIENTS);
	CHECK_CONDITION(pRecipients == NULL, COSE_ERR_INVALID_PARAMETER);

	_COSE_InsertInList(&Mac0Root, &pobj->m_message);

	return(HCOSE_MAC0)pobj;
}

bool COSE_Mac0_Free(HCOSE_MAC0 h)
{
#ifdef USE_CBOR_CONTEXT
	cn_cbor_context context;
#endif
	COSE_Mac0Message * p = (COSE_Mac0Message *)h;

	if (!IsValidMac0Handle(h)) return false;

	if (p->m_message.m_refCount > 1) {
		p->m_message.m_refCount--;
		return true;
	}

	_COSE_RemoveFromList(&Mac0Root, &p->m_message);

#ifdef USE_CBOR_CONTEXT
	context = p->m_message.m_allocContext;
#endif

	_COSE_Mac0_Release(p);

	COSE_FREE(p, &context);

	return true;
}

bool _COSE_Mac0_Release(COSE_Mac0Message * p)
{
	_COSE_Release(&p->m_message);

	return true;
}

bool COSE_Mac0_SetContent(HCOSE_MAC0 cose, const byte * rgbContent, size_t cbContent, cose_errback * perr)
{
	COSE_Mac0Message * p = (COSE_Mac0Message *)cose;
#ifdef USE_CBOR_CONTEXT        
	cn_cbor_context * context = &p->m_message.m_allocContext;
#endif
	cn_cbor * ptmp = NULL;
	cn_cbor_errback cbor_error;

	CHECK_CONDITION(IsValidMac0Handle(cose), COSE_ERR_INVALID_PARAMETER);

	ptmp = cn_cbor_data_create(rgbContent, (int) cbContent, CBOR_CONTEXT_PARAM_COMMA &cbor_error);
	CHECK_CONDITION_CBOR(ptmp != NULL, cbor_error);

	CHECK_CONDITION_CBOR(_COSE_array_replace(&p->m_message, ptmp, INDEX_BODY, CBOR_CONTEXT_PARAM_COMMA &cbor_error),  cbor_error);
	ptmp = NULL;

	return true;

errorReturn:
	if (ptmp != NULL) CN_CBOR_FREE(ptmp, context);
	return false;
}


cn_cbor * COSE_Mac0_map_get_int(HCOSE_MAC0 h, int key, int flags, cose_errback * perror)
{
	if (!IsValidMac0Handle(h)) {
		if (perror != NULL) perror->err = COSE_ERR_INVALID_PARAMETER;
		return NULL;
	}

	return _COSE_map_get_int(&((COSE_Mac0Message *)h)->m_message, key, flags, perror);
}


bool COSE_Mac0_map_put_int(HCOSE_MAC0 h, int key, cn_cbor * value, int flags, cose_errback * perror)
{
	if (!IsValidMac0Handle(h) || (value == NULL)) {
		if (perror != NULL) perror->err = COSE_ERR_INVALID_PARAMETER;
		return false;
	}

	return _COSE_map_put(&((COSE_Mac0Message *)h)->m_message, key, value, flags, perror);
}


bool COSE_Mac0_encrypt(HCOSE_MAC0 h, const byte * pbKey, size_t cbKey, cose_errback * perr)
{
	int alg;
	const cn_cbor * cn_Alg = NULL;
	byte * pbAuthData = NULL;
	size_t cbitKey;
#ifdef USE_CBOR_CONTEXT
	cn_cbor_context * context = NULL;
#endif
	COSE_Mac0Message * pcose = (COSE_Mac0Message *)h;
	bool fRet = false;
	size_t cbAuthData;

	CHECK_CONDITION(IsValidMac0Handle(h), COSE_ERR_INVALID_PARAMETER);

#ifdef USE_CBOR_CONTEXT
	context = &pcose->m_message.m_allocContext;
#endif // USE_CBOR_CONTEXT

	cn_Alg = _COSE_map_get_int(&pcose->m_message, COSE_Header_Algorithm, COSE_BOTH, perr);
	if (cn_Alg == NULL) goto errorReturn;
	CHECK_CONDITION(((cn_Alg->type == CN_CBOR_UINT || cn_Alg->type == CN_CBOR_INT)), COSE_ERR_INVALID_PARAMETER);

	alg = (int) cn_Alg->v.uint;

	//  Get the key size

	switch (alg) {
	case COSE_Algorithm_CBC_MAC_128_64:
	case COSE_Algorithm_CBC_MAC_128_128:
		cbitKey = 128;
		break;

	case COSE_Algorithm_CBC_MAC_256_64:
	case COSE_Algorithm_CBC_MAC_256_128:
	case COSE_Algorithm_HMAC_256_64:
	case COSE_Algorithm_HMAC_256_256:
		cbitKey = 256;
		break;

	case COSE_Algorithm_HMAC_384_384:
		cbitKey = 384;
		break;

	case COSE_Algorithm_HMAC_512_512:
		cbitKey = 512;
		break;

	default:
		FAIL_CONDITION(COSE_ERR_INVALID_PARAMETER);
	}

	//  Build protected headers

	const cn_cbor * cbProtected = _COSE_encode_protected(&pcose->m_message, perr);
	if (cbProtected == NULL) goto errorReturn;

	//  Build authenticated data
	if (!_COSE_Mac_Build_AAD(&pcose->m_message, "MAC0", &pbAuthData, &cbAuthData, CBOR_CONTEXT_PARAM_COMMA perr)) goto errorReturn;

	switch (alg) {
	case COSE_Algorithm_CBC_MAC_128_64:
	case COSE_Algorithm_CBC_MAC_256_64:
		if (!AES_CBC_MAC_Create((COSE_MacMessage *)pcose, 64, pbKey, cbKey, pbAuthData, cbAuthData, perr)) goto errorReturn;
		break;

	case COSE_Algorithm_CBC_MAC_128_128:
	case COSE_Algorithm_CBC_MAC_256_128:
		if (!AES_CBC_MAC_Create((COSE_MacMessage *)pcose, 128, pbKey, cbKey, pbAuthData, cbAuthData, perr)) goto errorReturn;
		break;

	case COSE_Algorithm_HMAC_256_64:
		if (!HMAC_Create((COSE_MacMessage *)pcose, 256, 64, pbKey, cbKey, pbAuthData, cbAuthData, perr)) goto errorReturn;
		break;

	case COSE_Algorithm_HMAC_256_256:
		if (!HMAC_Create((COSE_MacMessage *)pcose, 256, 256, pbKey, cbKey, pbAuthData, cbAuthData, perr)) goto errorReturn;
		break;

	case COSE_Algorithm_HMAC_384_384:
		if (!HMAC_Create((COSE_MacMessage *)pcose, 384, 384, pbKey, cbKey, pbAuthData, cbAuthData, perr)) goto errorReturn;
		break;

	case COSE_Algorithm_HMAC_512_512:
		if (!HMAC_Create((COSE_MacMessage *)pcose, 512, 512, pbKey, cbKey, pbAuthData, cbAuthData, perr)) goto errorReturn;
		break;

	default:
		FAIL_CONDITION(COSE_ERR_INVALID_PARAMETER);
	}

	//  Figure out the clean up

	fRet = true;

errorReturn:
	if (pbAuthData != NULL) COSE_FREE(pbAuthData, context);
	return fRet;
}

bool COSE_Mac0_validate(HCOSE_MAC0 h, const byte * pbKey, size_t cbKey, cose_errback * perr)
{
	COSE_Mac0Message * pcose = (COSE_Mac0Message *)h;
	byte * pbAuthData = NULL;
	int cbitKey = 0;
	bool fRet = false;
	int alg;
	const cn_cbor * cn = NULL;

#ifdef USE_CBOR_CONTEXT
	cn_cbor_context * context = NULL;
#endif
	size_t cbAuthData;

	CHECK_CONDITION(IsValidMac0Handle(h), COSE_ERR_INVALID_PARAMETER);

#ifdef USE_CBOR_CONTEXT
	context = &pcose->m_message.m_allocContext;
#endif

	cn = _COSE_map_get_int(&pcose->m_message, COSE_Header_Algorithm, COSE_BOTH, perr);
	if (cn == NULL) goto errorReturn;

	if (cn->type == CN_CBOR_TEXT) {
			FAIL_CONDITION(COSE_ERR_UNKNOWN_ALGORITHM);
	}
	else {
		CHECK_CONDITION((cn->type == CN_CBOR_UINT || cn->type == CN_CBOR_INT), COSE_ERR_INVALID_PARAMETER);

		alg = (int)cn->v.uint;

		switch (alg) {
		case COSE_Algorithm_CBC_MAC_128_64:
		case COSE_Algorithm_CBC_MAC_128_128:
			cbitKey = 128;
			break;

		case COSE_Algorithm_CBC_MAC_256_64:
		case COSE_Algorithm_CBC_MAC_256_128:
		case COSE_Algorithm_HMAC_256_64:
		case COSE_Algorithm_HMAC_256_256:
			cbitKey = 256;
			break;

		case COSE_Algorithm_HMAC_384_384:
			cbitKey = 384;
			break;

		case COSE_Algorithm_HMAC_512_512:
			cbitKey = 512;
			break;

		default:
			FAIL_CONDITION(COSE_ERR_UNKNOWN_ALGORITHM);
			break;
		}
	}

	//  Build protected headers

	cn_cbor * cnProtected = _COSE_arrayget_int(&pcose->m_message, INDEX_PROTECTED);
	CHECK_CONDITION((cnProtected != NULL) && (cnProtected->type == CN_CBOR_BYTES), COSE_ERR_INVALID_PARAMETER);

	//  Build authenticated data
	if (!_COSE_Mac_Build_AAD(&pcose->m_message, "MAC0", &pbAuthData, &cbAuthData, CBOR_CONTEXT_PARAM_COMMA perr)) goto errorReturn;

	switch (alg) {
	case COSE_Algorithm_HMAC_256_256:
		if (!HMAC_Validate((COSE_MacMessage *)pcose, 256, 256, pbKey, cbKey, pbAuthData, cbAuthData, perr)) goto errorReturn;
		break;

	case COSE_Algorithm_HMAC_256_64:
		if (!HMAC_Validate((COSE_MacMessage *)pcose, 256, 64, pbKey, cbKey, pbAuthData, cbAuthData, perr)) goto errorReturn;
		break;

	case COSE_Algorithm_HMAC_384_384:
		if (!HMAC_Validate((COSE_MacMessage *)pcose, 384, 384, pbKey, cbKey, pbAuthData, cbAuthData, perr)) goto errorReturn;
		break;

	case COSE_Algorithm_HMAC_512_512:
		if (!HMAC_Validate((COSE_MacMessage *)pcose, 512, 512, pbKey, cbKey, pbAuthData, cbAuthData, perr)) goto errorReturn;
		break;

	case COSE_Algorithm_CBC_MAC_128_64:
	case COSE_Algorithm_CBC_MAC_256_64:
		if (!AES_CBC_MAC_Validate((COSE_MacMessage *)pcose, 64, pbKey, cbKey, pbAuthData, cbAuthData, perr)) goto errorReturn;
		break;

	case COSE_Algorithm_CBC_MAC_128_128:
	case COSE_Algorithm_CBC_MAC_256_128:
		if (!AES_CBC_MAC_Validate((COSE_MacMessage *)pcose, 128, pbKey, cbKey, pbAuthData, cbAuthData, perr)) goto errorReturn;
		break;

	default:
		FAIL_CONDITION(COSE_ERR_UNKNOWN_ALGORITHM);
		break;
	}

	fRet = true;

errorReturn:
	if (pbAuthData != NULL) COSE_FREE(pbAuthData, context);

	return fRet;
}
