//  encrypt.c

#define _CRT_SECURE_NO_WARNINGS

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <cose.h>
#include <cn-cbor/cn-cbor.h>

#include "json.h"
#include "test.h"
#include "context.h"


int ValidateEnveloped(const cn_cbor * pControl)
{
	int cbEncoded;
	byte * pbEncoded = GetCBOREncoding(pControl, &cbEncoded);
	const cn_cbor * pInput = cn_cbor_mapget_string(pControl, "input");
	const cn_cbor * pFail;
	const cn_cbor * pEnveloped;
	const cn_cbor * pRecipients;
	HCOSE_ENCRYPT hEnc;
	int type;
	int iRecipient;
	bool fFail = false;
	bool fFailBody = false;

#ifdef USE_CBOR_CONTEXT
        allocator = CreateContext();
#endif 

	pFail = cn_cbor_mapget_string(pControl, "fail");
	if ((pFail != NULL) && (pFail->type == CN_CBOR_TRUE)) {
		fFailBody = true;
	}

	if ((pInput == NULL) || (pInput->type != CN_CBOR_MAP)) exit(1);
	pEnveloped = cn_cbor_mapget_string(pInput, "enveloped");
	if ((pEnveloped == NULL) || (pEnveloped->type != CN_CBOR_MAP)) exit(1);

	pRecipients = cn_cbor_mapget_string(pEnveloped, "recipients");
	if ((pRecipients == NULL) || (pRecipients->type != CN_CBOR_ARRAY)) exit(1);

	pRecipients = pRecipients->first_child;
	for (iRecipient = 0; pRecipients != NULL; iRecipient++, pRecipients = pRecipients->next) {

		hEnc = (HCOSE_ENCRYPT)COSE_Decode(pbEncoded, cbEncoded, &type, COSE_enveloped_object, CBOR_CONTEXT_PARAM_COMMA NULL);
		if (hEnc == NULL) exit(1);


		cn_cbor * pkey = BuildKey(cn_cbor_mapget_string(pRecipients, "key"));
		if (pkey == NULL) {
			fFail = true;
			continue;
		}

		HCOSE_RECIPIENT hRecip = COSE_Encrypt_GetRecipient(hEnc, iRecipient, NULL);
		if (hEnc == NULL) {
			fFail = true;
			continue;
		}

		if (!COSE_Recipient_SetKey(hRecip, pkey, NULL)) {
			fFail = true;
			continue;
		}

		pFail = cn_cbor_mapget_string(pRecipients, "fail");
		if (COSE_Encrypt_decrypt(hEnc, hRecip, NULL)) {
			if ((pFail != NULL) && (pFail->type != CN_CBOR_TRUE)) fFail = true;
		}
		else {
			if ((pFail == NULL) || (pFail->type == CN_CBOR_FALSE)) fFail = true;
		}

		// COSE_Encrypt_Free(hEnc);
	}

	if (fFailBody) {
		if (!fFail) fFail = true;
		else fFail = false;
	}

#ifdef USE_CBOR_CONTEXT
        FreeContext(allocator);
        allocator = NULL;
#endif

	if (fFail) CFails += 1;
	return 0;
}

int BuildEncryptMessage(const cn_cbor * pControl)
{
	int iRecipient;

	//
	//  We don't run this for all control sequences - skip those marked fail.
	//

	const cn_cbor * pFail = cn_cbor_mapget_string(pControl, "fail");
	if ((pFail != NULL) && (pFail->type == CN_CBOR_TRUE)) return 0;

	HCOSE_ENCRYPT hEncObj = COSE_Encrypt_Init(CBOR_CONTEXT_PARAM_COMMA NULL);

	const cn_cbor * pInputs = cn_cbor_mapget_string(pControl, "input");
	if (pInputs == NULL) exit(1);
	const cn_cbor * pEnveloped = cn_cbor_mapget_string(pInputs, "enveloped");
	if (pEnveloped == NULL) exit(1);

	const cn_cbor * pContent = cn_cbor_mapget_string(pInputs, "plaintext");
	if (!COSE_Encrypt_SetContent(hEncObj, pContent->v.bytes, pContent->length, NULL)) goto returnError;

	if (!SetAttributes((HCOSE)hEncObj, cn_cbor_mapget_string(pEnveloped, "protected"), Attributes_Enveloped_protected)) goto returnError;
	if (!SetAttributes((HCOSE)hEncObj, cn_cbor_mapget_string(pEnveloped, "unprotected"), Attributes_Enveloped_unprotected)) goto returnError;
	if (!SetAttributes((HCOSE)hEncObj, cn_cbor_mapget_string(pEnveloped, "unsent"), Attributes_Enveloped_unsent)) goto returnError;

	const cn_cbor * pAlg = COSE_Encrypt_map_get_int(hEncObj, 1, COSE_BOTH, NULL);

	const cn_cbor * pRecipients = cn_cbor_mapget_string(pEnveloped, "recipients");
	if ((pRecipients == NULL) || (pRecipients->type != CN_CBOR_ARRAY)) exit(1);

	pRecipients = pRecipients->first_child;
	for (iRecipient = 0; pRecipients != NULL; iRecipient++, pRecipients = pRecipients->next) {
		cn_cbor * pkey = BuildKey(cn_cbor_mapget_string(pRecipients, "key"));
		if (pkey == NULL) exit(1);

		HCOSE_RECIPIENT hRecip = COSE_Recipient_Init(CBOR_CONTEXT_PARAM_COMMA NULL);
		if (hRecip == NULL) exit(1);

		if (!SetAttributes((HCOSE)hRecip, cn_cbor_mapget_string(pRecipients, "protected"), Attributes_Recipient_protected)) goto returnError;
		if (!SetAttributes((HCOSE)hRecip, cn_cbor_mapget_string(pRecipients, "unprotected"), Attributes_Recipient_unprotected)) goto returnError;
		if (!SetAttributes((HCOSE)hRecip, cn_cbor_mapget_string(pRecipients, "unsent"), Attributes_Recipient_unsent)) goto returnError;

		if (!COSE_Recipient_SetKey(hRecip, pkey, NULL)) exit(1);

		if (!COSE_Encrypt_AddRecipient(hEncObj, hRecip, NULL)) exit(1);
	}

	if (!COSE_Encrypt_encrypt(hEncObj, NULL)) exit(1);

	size_t cb = COSE_Encode((HCOSE)hEncObj, NULL, 0, 0) + 1;
	byte * rgb = (byte *)malloc(cb);
	cb = COSE_Encode((HCOSE)hEncObj, rgb, 0, cb);

	return 0;

returnError:
	CFails += 1;
	return 1;
}

int EncryptMessage()
{
	HCOSE_ENCRYPT hEncObj = COSE_Encrypt_Init(CBOR_CONTEXT_PARAM_COMMA NULL);
	byte rgbSecret[128 / 8] = { 'a', 'b', 'c' };
	int cbSecret = 128/8;
	byte  rgbKid[6] = { 'a', 'b', 'c', 'd', 'e', 'f' };
	int cbKid = 6;
	size_t cb;
	byte * rgb;
	char * sz = "This is the content to be used";


	COSE_Encrypt_map_put(hEncObj, COSE_Header_Algorithm, cn_cbor_int_create(COSE_Algorithm_AES_CCM_16_64_128, CBOR_CONTEXT_PARAM_COMMA NULL), COSE_PROTECT_ONLY, NULL);
	COSE_Encrypt_SetContent(hEncObj, (byte *) sz, strlen(sz), NULL);
	COSE_Encrypt_map_put(hEncObj, COSE_Header_IV, cn_cbor_data_create(rgbKid, cbKid, CBOR_CONTEXT_PARAM_COMMA NULL), COSE_UNPROTECT_ONLY, NULL);

	COSE_Encrypt_add_shared_secret(hEncObj, COSE_Algorithm_Direct, rgbSecret, cbSecret, rgbKid, cbKid, NULL);

	COSE_Encrypt_encrypt(hEncObj, NULL);

	cb = COSE_Encode((HCOSE)hEncObj, NULL, 0, 0) +1;
	rgb = (byte *)malloc(cb);
	cb = COSE_Encode((HCOSE)hEncObj, rgb, 0, cb);


	FILE * fp = fopen("test.cbor", "wb");
	fwrite(rgb, cb, 1, fp);
	fclose(fp);

#if 0
	char * szX;
	int cbPrint = 0;
	cn_cbor * cbor = COSE_get_cbor((HCOSE) hEncObj);
	cbPrint = cn_cbor_printer_write(NULL, 0, cbor, "  ", "\r\n");
	szX = malloc(cbPrint);
	cn_cbor_printer_write(szX, cbPrint, cbor, "  ", "\r\n");
	fprintf(stdout, "%s", szX);
	fprintf(stdout, "\r\n");
#endif

	COSE_Encrypt_Free(hEncObj);

	/* */

	int typ;
	hEncObj = (HCOSE_ENCRYPT) COSE_Decode(rgb, (int) cb, &typ, COSE_enveloped_object, CBOR_CONTEXT_PARAM_COMMA NULL);
	
	int iRecipient = 0;
	do {
		HCOSE_RECIPIENT hRecip;

		hRecip = COSE_Encrypt_GetRecipient(hEncObj, iRecipient, NULL);
		if (hRecip == NULL) break;

		COSE_Recipient_SetKey_secret(hRecip, rgbSecret, cbSecret, NULL);

		COSE_Encrypt_decrypt(hEncObj, hRecip, NULL);

		iRecipient += 1;

	} while (true);

	return 1;
}
