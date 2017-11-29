#if HAVE_CONFIG_H
#include <config.h>
#endif

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <cups/raster.h>
#include "lcms2.h"
#include "kmlf.h"

#if WORDS_BIGENDIAN
#  define BMP_ASSIGN_WORD(a,v) a = ((v) >> 8) + ((v) << 8)
#  define BMP_ASSIGN_DWORD(a,v)\
     a = ((v) >> 24) + (((v) >> 8) & 0xff00L) +\
	 (((DWORD)(v) << 8) & 0xff0000L) + ((DWORD)(v) << 24)
#else
#  define BMP_ASSIGN_WORD(a,v) a = (v)
#  define BMP_ASSIGN_DWORD(a,v) a = (v)
#endif

#pragma pack(1)
typedef struct tagBITMAPFILEHEADER { 
	WORD    	bfType; 
	DWORD   	bfSize; 
	WORD    	bfReserved1; 
	WORD    	bfReserved2; 
	DWORD   	bfOffBits; 
} BITMAPFILEHEADER, *PBITMAPFILEHEADER;
#pragma pack()

#pragma pack(1)
typedef struct tagBITMAPINFOHEADER{
	DWORD  	biSize; 
	DWORD   biWidth; 
	DWORD   biHeight; 
	WORD   	biPlanes; 
	WORD   	biBitCount; 
	DWORD  	biCompression; 
	DWORD  	biSizeImage; 
	DWORD   biXPelsPerMeter; 
	DWORD   biYPelsPerMeter; 
	DWORD  	biClrUsed; 
	DWORD  	biClrImportant; 
} BITMAPINFOHEADER, *PBITMAPINFOHEADER; 
#pragma pack()

#pragma pack(1)
typedef struct tagRGBQUAD {
	BYTE    rgbBlue; 
	BYTE    rgbGreen; 
	BYTE    rgbRed; 
	BYTE    rgbReserved; 
} RGBQUAD;
#pragma pack()

#if 0
/* Write a BMP header for separated CMYK output. */
int write_bmp_header(cups_page_header_t *header, FILE *file)
{
	int plane_depth = header->cupsBitsPerColor;
	RGBQUAD palette[256];
	RGBQUAD q;
	int i;
	unsigned long bmp_raster;
	int height = header->cupsHeight;
	int raster;
	int quads = (plane_depth <= 8 ? sizeof(RGBQUAD) << plane_depth : 0);
	int NumPlanes;

	if (CUPS_CSPACE_RGB == header->cupsColorSpace)
	{
		NumPlanes = 3;
	}
	else
	{
		NumPlanes = 1;        /* Black&white graphics */
	}
	raster = header->cupsBytesPerLine/ NumPlanes;

	q.rgbReserved = 0;
	for (i = 0; i < 1 << plane_depth; i++) 
	{
		q.rgbRed = q.rgbGreen = q.rgbBlue =
			255 - i * 255 / ((1 << plane_depth) - 1);
		palette[i] = q;
	}

   /* BMP scan lines are padded to 32 bits. */
	bmp_raster = raster + (-raster & 3);
    
   /* Write the file header. */
   {
		BITMAPFILEHEADER bmpfh;
		
		BMP_ASSIGN_WORD(bmpfh.bfType, "BM");
		BMP_ASSIGN_DWORD(bmpfh.bfSize,
							  sizeof(BITMAPFILEHEADER) +
							  sizeof(BITMAPINFOHEADER) + quads +
							  bmp_raster * height);
		BMP_ASSIGN_WORD(bmpfh.bfReserved1, 0);
		BMP_ASSIGN_WORD(bmpfh.bfReserved2, 0);
		BMP_ASSIGN_DWORD(bmpfh.bfOffBits,
							  sizeof(BITMAPFILEHEADER) +
							  sizeof(BITMAPINFOHEADER) + quads);
		if (fwrite((const char *)&bmpfh, 1, sizeof(BITMAPFILEHEADER), file) != sizeof(BITMAPFILEHEADER))
		{
			fprintf(stderr, "DEBUG: cannot write bmp file header");
		}			
    }

    /* Write the info header. */
    {
		 BITMAPINFOHEADER bih;
		 BMP_ASSIGN_DWORD(bih.biSize, sizeof(BITMAPINFOHEADER));
		 BMP_ASSIGN_DWORD(bih.biWidth, header->cupsWidth);
		 BMP_ASSIGN_DWORD(bih.biHeight, height);
		 BMP_ASSIGN_WORD(bih.biPlanes, 1);
		 BMP_ASSIGN_WORD(bih.biBitCount, plane_depth);
		 BMP_ASSIGN_DWORD(bih.biCompression, 0);
		 BMP_ASSIGN_DWORD(bih.biSizeImage, bmp_raster * height);
		 BMP_ASSIGN_DWORD(bih.biXPelsPerMeter, 0);
		 BMP_ASSIGN_DWORD(bih.biYPelsPerMeter, 0);
		 BMP_ASSIGN_DWORD(bih.biClrUsed, 0);
		 BMP_ASSIGN_DWORD(bih.biClrImportant, 0);
		 if (fwrite((const char *)&bih, 1, sizeof(BITMAPINFOHEADER), file) != sizeof(BITMAPINFOHEADER))
		 {
			 fprintf(stderr, "DEBUG: cannot write bmp info header");
		 }	    
    }

    /* Write the palette. */
	 if (plane_depth <= 8)
		 fwrite(palette, sizeof(RGBQUAD), 1 << plane_depth, file);

    return 0;
}

#endif

int getHalftoneByResolution (PIMAGEHEADER pImage)
{
	BYTE szFile[ 256 ];
   FILE *fh;
	size_t numread;
	PBITMAPFILEHEADER pbmpfh;
	PBITMAPINFOHEADER pbih;
	PHTHEADER pHTtmp;
	int plane, startplane;

	if(pImage->NumPlanes==1)
		startplane=3;
	else
		startplane=0;

	for(plane=startplane; plane <4; plane++) //cmyk 0 1 2 3
	{
		sprintf(szFile, 
			"%s/Halftones/km_ht_%d%d.bmp",
			KM_DATADIR, pImage->xResolution/1200, plane); 
		
		fh = fopen( szFile, "rb");
		if (fh == NULL)
		{
			fprintf(stderr, "ERROR: NULL fh\n");
			return CUPS_FALSE;
		}

		pbmpfh = (PBITMAPFILEHEADER)malloc(sizeof(BITMAPFILEHEADER));

		numread = fread(pbmpfh,1,sizeof(BITMAPFILEHEADER),fh);
		if(numread != sizeof(BITMAPFILEHEADER))
		{
			fprintf(stderr, "ERROR: numread=%d, size=%d\n",numread,sizeof(BITMAPFILEHEADER));
			return CUPS_FALSE;
		}
		BMP_ASSIGN_DWORD(pbmpfh->bfSize,pbmpfh->bfSize);
		BMP_ASSIGN_DWORD(pbmpfh->bfOffBits,pbmpfh->bfOffBits);
		
		pbih = (PBITMAPINFOHEADER)malloc(pbmpfh->bfSize-sizeof(BITMAPFILEHEADER));
		if(pbih==NULL)
		{
			fprintf(stderr, "ERROR: NULL pbih\n");
			return CUPS_FALSE;
		}
		numread = fread(pbih,1,pbmpfh->bfSize - sizeof(BITMAPFILEHEADER),fh);
		if(numread != (pbmpfh->bfSize - sizeof(BITMAPFILEHEADER)))
		{
			fprintf(stderr, "ERROR: wrongsize xxNULL fh\n");
			return CUPS_FALSE;
		}

		BMP_ASSIGN_DWORD(pbih->biWidth,pbih->biWidth);
		BMP_ASSIGN_DWORD(pbih->biHeight,pbih->biHeight);

		pHTtmp = (PHTHEADER)malloc(sizeof(HTHEADER) + pbih->biWidth * pbih->biHeight);

		if(pHTtmp == NULL)
		{
			fprintf(stderr, "ERROR: pbih->width=%d,height=%d\n)",pbih->biWidth,pbih->biHeight);
			fprintf(stderr, "ERROR: size=%d,NULL pHTtmp\n",sizeof(HTHEADER));
			return CUPS_FALSE;
		}
		pHTtmp->wID = plane;// * (objtype+1);
		pHTtmp->cxHT = (WORD)pbih->biWidth;
		pHTtmp->cyHT = (WORD)pbih->biHeight;

		memcpy((BYTE *)(pHTtmp+1), 
				 (BYTE *)pbih + pbmpfh->bfOffBits - sizeof(BITMAPFILEHEADER),
				 pHTtmp->cxHT * pHTtmp->cyHT); 
		//pHTtmp will be freed after using it in Shutdown()
		pImage->pHT[plane] = pHTtmp;
		
		free(pbmpfh);
		pbmpfh = NULL;
		free(pbih);
		pbih = NULL;
		fclose(fh);
	}
   return CUPS_TRUE;
}

