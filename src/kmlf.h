#ifndef KMLF_H
#define KMLF_H
#include <limits.h>


#if 0
typedef unsigned short WORD;
typedef unsigned char BYTE;

#if (UINT_MAX==0xFFFFFFFF)
typedef unsigned int DWORD;
#else
typedef unsigned long DWORD;
#endif
#endif

#pragma pack(1)
typedef struct _HT_HEADER{
	WORD wID;
	WORD cxHT;
	WORD cyHT;
	WORD wFree;
}HTHEADER, *PHTHEADER;
#pragma pack()

#pragma pack(1)
typedef struct _IMAGE_HEADER{
	DWORD InitCM_HT;
	DWORD iColorSpace;
	WORD xResolution;
	WORD NumPlanes;
	void *pHT[4];
}IMAGEHEADER, *PIMAGEHEADER;
#pragma pack()

//Media Type code
#define PLAIN_PAPER		0
#define TRANSPARENCY		4
#define GLOSSY				28
#define THICK_STOCK		20
#define ENVELOPE			22
#define LETTERHEAD		23
#define POSTCARD			25
#define LABELS				26
#define RECYCLED_PAPER	27

//Paper Source Code
#define TRAY_AUTOSELECT	7
#define TRAY_MANUALFEED	2
#define TRAY_1				1
#define TRAY_2				4
#define TRAY_3				101

#endif
