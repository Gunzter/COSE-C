#ifndef _countof
#define _countof(x) (sizeof(x)/sizeof(x[0]))
#endif

#ifdef USE_CBOR_CONTEXT
cn_cbor_context * allocator;
#define CBOR_CONTEXT_PARAM , allocator
#define CBOR_CONTEXT_PARAM_COMMA allocator,
#else
#define CBOR_CONTEXT_PARAM
#define CBOR_CONTEXT_PARAM_COMMA
#endif

//  encrypt.c

int ValidateEnveloped(const cn_cbor * pControl);
int EncryptMessage();
int BuildEncryptMessage(const cn_cbor * pControl);

//  sign.c

int ValidateSigned(const cn_cbor * pControl);
int SignMessage();
int BuildSignedMessage(const cn_cbor * pControl);

// mac_testc

int ValidateMac(const cn_cbor * pControl);
int MacMessage();
int BuildMacMessage(const cn_cbor * pControl);

//  test.c
enum {
	Attributes_MAC_protected=1,
	Attributes_MAC_unprotected,
	Attributes_Recipient_protected,
	Attributes_Recipient_unprotected,
	Attributes_Recipient_unsent,
	Attributes_Enveloped_protected,
	Attributes_Enveloped_unprotected,
	Attributes_Enveloped_unsent,
	Attributes_Sign_protected,
	Attributes_Sign_unprotected,
	Attributes_Sign_unsent,
	Attributes_Signer_protected,
	Attributes_Signer_unprotected,
	Attributes_Signer_unsent,
} whichSet;

extern int CFails;

int MapAlgorithmName(const cn_cbor * p);
cn_cbor * cn_cbor_clone(const cn_cbor * pIn);
byte * GetCBOREncoding(const cn_cbor * pControl, int * pcbEncoded);
bool SetAttributes(HCOSE hHandle, const cn_cbor * pAttributes, int which);
cn_cbor * BuildKey(const cn_cbor * pKeyIn);
