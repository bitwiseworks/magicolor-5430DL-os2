/*
 *  rastertokmlf -- Konica Minolta LavaFlow Stream (PCL emulater) filter for CUPS
 *  Sean Zhan @ KONICA MINOLTA SYSTEMS LAB INC
 *
 *  This filter is modified from CUPS 1.15 sample filter rastertohp.c
 *  and uses Jbig compression library from Markus Kuhn
 *  http://www.cl.cam.ac.uk/~mgk25/download/jbigkit-1.5.tar.gz
 *  and uses littleCMS 1.14 from www.littlecms.com
 */
/*
 * "$Id: rastertohp.c,v 1.19 2002/03/01 19:53:36 mike Exp $"
 *
 *   Hewlett-Packard Page Control Language filter for the Common UNIX
 *   Printing System (CUPS).
 *
 *   Copyright 1993-2002 by Easy Software Products.
 *
 *   These coded instructions, statements, and computer programs are the
 *   property of Easy Software Products and are protected by Federal
 *   copyright law.  Distribution and use rights are outlined in the file
 *   "LICENSE.txt" which should have been included with this file.  If this
 *   file is missing or damaged please contact Easy Software Products
 *   at:
 *
 *       Attn: CUPS Licensing Information
 *       Easy Software Products
 *       44141 Airport View Drive, Suite 204
 *       Hollywood, Maryland 20636-3111 USA
 *
 *       Voice: (301) 373-9603
 *       EMail: cups-info@cups.org
 *         WWW: http://www.cups.org
 *
 *   This file is subject to the Apple OS-Developed Software exception.
 *
 * Contents:
 *
 *   Setup()        - Prepare the printer for printing.
 *   StartPage()    - Start a page of graphics.
 *   EndPage()      - Finish a page of graphics.
 *   Shutdown()     - Shutdown the printer.
 *   CancelJob()    - Cancel the current job...
 *   CompressData() - Compress a line of graphics.
 *   OutputLine()   - Output a line of graphics.
 *   main()         - Main entry and processing of driver.
 */

#if HAVE_CONFIG_H
#include <config.h>
#endif
/*
 * Include necessary headers...
 */

#include <cups/cups.h>
//#include <cups/string.h>
#include <cups/language.h>
#include <cups/raster.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <signal.h>
#include <sys/utsname.h>
#include "jbig.h"
#include "lcms.h"
#include "kmlf.h"

/*
 * Globals...
 */

unsigned char *Planes[4],     /* Output buffers */
   *OutBuffer;                /* Buffer for output data */  
unsigned char *pLineBuf=NULL;
unsigned char *pbCMYKline=NULL;
int   NumPlanes,     /* Number of color planes */
   ColorBits,        /* Number of bits per color */
   Feed,             /* Number of lines to skip */
   Duplex,           /* Current duplex mode */
   Page;             /* Current page number */
int top_offset,bottom_offset,left_offset,right_offset;
int xIgnore, yIgnore;
int WidthInBytes;
unsigned printarea_x, printarea_y; //printable area width and height
cups_bool_t fWriteJBigHeader;
unsigned lSize;

cmsHPROFILE hInProfile, hOutProfile;
cmsHTRANSFORM hTransform;

PIMAGEHEADER pImage=NULL;
#define SIGNATURE_CMHT 0x434D4854
#define MIN_OFFSET   96         //4 mm from each edge
#define OUTBUFSIZE   0x10000   //64K for USB
#define UEL_STRING ("\033%-12345X")
#define PJL_ENTER_KMLF_STRING ("@PJL ENTER LANGUAGE=LAVAFLOW\012")
#define PJL_START_JOB           ("@PJL JOB NAME=\"%s\"\012")
#define PJL_START_TIMESTAMP     ("@PJL JOB TIMESTAMP=\"%02d/%02d/%d\"\012")
#define PJL_START_OSINFO        ("@PJL JOB OSINFO=\"%s/%s\"\012")
//color plane non-white dots and white dots count.
#define STR_LAVAFLOW_CMD_DOTS			"\033*x%ld%c"
#define STR_LAVAFLOW_CMD_WHITEDOTS	"\033*x%ld%c"
#define MAX_PACKET_SIZE	64
#define JBIG_ALIGNBYTES 16

#define WRITERASTERTOBMP 0 //1
#define CUPS_FLIP  //cups will flip the image for duplex
#if WRITERASTERTOBMP
FILE *rasterplanefile[4];
#endif
static int	holdcount = 0;		/* Number of time "hold" was called */
#if defined(HAVE_SIGACTION) && !defined(HAVE_SIGSET)
static sigset_t	holdmask;		/* Old POSIX signal mask */
#endif /* HAVE_SIGACTION && !HAVE_SIGSET */

FILE *fp;
unsigned long dwDotCount[4];
unsigned long dwWhiteDots[4];
char symDot[4]={'K','C','M','Y'};
char symDotW[4]={'W','Z','V','U'};
cups_bool_t usb_flag=CUPS_FALSE;
int usb_size;

/*
 * Prototypes...
 */

int  Setup(void);
int  StartPage(ppd_file_t *ppd, cups_page_header_t *header);
void  EndPage(void);
void  Shutdown(void);
void  CancelJob(int sig);

static void OutputBie(unsigned char *start, size_t len, void *file);
void  OutputPlane(unsigned char *pbPlane, unsigned leftsize);
int getHalftoneByResolution (PIMAGEHEADER pImage);
#ifndef CUPS_FLIP
int RotateImage(
	unsigned char *pBuffer,		// image buffer to be rotated
	unsigned long cbData			// size of data in bytes
);
#endif
int CountDots(int p, unsigned char *pBuf, unsigned long dwLen);
static void myfprintf(FILE *stream, const char *format, ...);
#if WRITERASTERTOBMP
int write_bmp_header(cups_page_header_t *header, FILE *file);
#endif

/*
 * 'Setup()' - Prepare the printer for printing.
 */

int
   Setup(void)
{
   const char    *device_uri;    /* The device for the printer... */
   cups_lang_t  *language;  /* Language information */

	//reset colormatching flag
	pImage = (PIMAGEHEADER)malloc(sizeof(IMAGEHEADER));
   pImage->InitCM_HT = 0;
   // if ((device_uri = getenv("DEVICE_URI")) != NULL &&
   //   strncmp(device_uri, "usb:", 4) == 0 && Model >= EPSON_ICOLOR)

   device_uri = getenv("DEVICE_URI");

   language = cupsLangDefault();  
   fprintf(stderr, "DEBUG: language is %s\n",language->language);
   fprintf(stderr, "DEBUG: lang is %s\n",getenv("LANG"));
   cupsLangFree(language);

   if (device_uri)
   {
      fprintf(stderr, "DEBUG: deviceuri is %s\n",device_uri);
		if(!strncmp(device_uri, "usb:", 4) ||!strncmp(device_uri, "hal:", 4))
		{
			fprintf(stderr, "DEBUG: usb_flag is true\n");
			usb_flag = CUPS_TRUE;
		}
   }
   else
   {
      fprintf(stderr, "ERROR: deviceuri is null\n");
      return(-1);
   }

   return 0;
}


/*
 * 'StartPage()' - Start a page of graphics.
 */

int
   StartPage(ppd_file_t         *ppd,    /* I - PPD file */
             cups_page_header_t *header) /* I - Page header */
{
   int plane;        /* Looping var */
   int iMinX, iMinY;
   int iBindingX, iBindingY;
   int iPageWidth,iPageHeight;
   int iPlaneSize;
   cups_bool_t bCustomSize = CUPS_FALSE;
	int rc=0;

   //int printarea_x, printarea_y;
   //ppd_size_t *size;

#if defined(HAVE_SIGACTION) && !defined(HAVE_SIGSET)
   struct sigaction action;    /* Actions for POSIX signals */
#endif /* HAVE_SIGACTION && !HAVE_SIGSET */


   /*
    * Register a signal handler to eject the current page if the
    * job is cancelled.
    */

#ifdef HAVE_SIGSET /* Use System V signals over POSIX to avoid bugs */
   sigset(SIGTERM, CancelJob);
#elif defined(HAVE_SIGACTION)
   memset(&action, 0, sizeof(action));

   sigemptyset(&action.sa_mask);
   action.sa_handler = CancelJob;
   sigaction(SIGTERM, &action, NULL);
#else
   signal(SIGTERM, CancelJob);
#endif /* HAVE_SIGSET */

   /*
    * Setup printer/job attributes...
    */

   Duplex = header->Duplex + header->Tumble;
   ColorBits = header->cupsBitsPerColor;

   if (Page==1)
   {
      if (header->Duplex)
         myfprintf(fp, "\033&l%dS",     /* Set duplex mode */
                Duplex);
      myfprintf(fp, "\033&l%dX", header->NumCopies); /* Set number copies */
		myfprintf(fp, "\033&u%dD", header->HWResolution[0]); //set unit of measurement
   }

   //calculation margin information
   fprintf(stderr, "DEBUG: header->Margins[0]=%d, header->Margins[1]=%d\n",header->Margins[0],header->Margins[1]);
   fprintf(stderr, "DEBUG: header->ImagingBoundingBox[0]=%d, header->ImagingBoundingBox[1]=%d\n",header->ImagingBoundingBox[0],header->ImagingBoundingBox[1]);
   iMinX = MIN_OFFSET*(header->HWResolution[0]/600);
   iBindingX = (header->HWResolution[0]*header->Margins[0])/72 ;
   if (iMinX < iBindingX)
      xIgnore = 0;
   else
      xIgnore = iMinX-iBindingX;

   iMinY = MIN_OFFSET*(header->HWResolution[1]/600);
   iBindingY = (header->HWResolution[1]*header->Margins[1])/72 ;
   if (iMinY < iBindingY)
      yIgnore = 0;
   else
      yIgnore = iMinY - iBindingY;
   fprintf(stderr, "DEBUG: xIgnore=%d, yIgnore=%d\n",xIgnore,yIgnore);
   left_offset = right_offset = xIgnore ? iMinX : iBindingX; 
   top_offset = bottom_offset = yIgnore ? iMinY : iBindingY;
   fprintf(stderr, "DEBUG: left_offset=%d, top_offset=%d\n",left_offset,top_offset);

   //set page information
   /*
    * Set the media type, position, and size...
    */

   // fprintf(fp, "\033&l6D\033&k12H");   /* Set 6 LPI, 10 CPI */
   myfprintf(fp, "\033&l0O");       /* Set portrait orientation */
   iPageWidth = header->PageSize[0];
   iPageHeight = header->PageSize[1];
   //fprintf(stderr, "ERROR: iPageWidth=%d, iPageHeight=%d\n",iPageWidth,iPageHeight);

   switch (iPageHeight)
   {
   case 420 : /* Japanese Postcard */
      if (283==iPageWidth)
      {
         myfprintf(fp, "\033&l71A");      /* Set page size */
      }
      else
         bCustomSize = CUPS_TRUE;
      break;

   case 459 : /* Envelope C6 */
      if (323==iPageWidth)
      {
         myfprintf(fp, "\033&l92A");      /* Set page size */
      }
      else
         bCustomSize = CUPS_TRUE;
      break;

   case 524 : /* Kai 32 */
      if (369==iPageWidth)
      {
         myfprintf(fp, "\033&l820A");      /* Set page size */
      }
      else
         bCustomSize = CUPS_TRUE;
      break;

   case 540 : /* Monarch Envelope */
      if (279==iPageWidth)
      {
         myfprintf(fp, "\033&l80A");      /* Set page size */
      }
      else
         bCustomSize = CUPS_TRUE;
      break;
   
	case 567 : /* Double Postcard */
      if (420==iPageWidth)
      {
         myfprintf(fp, "\033&l72A");      /* Set page size */
      }
      else
         bCustomSize = CUPS_TRUE;
      break;

   case 581 : /* Envelope Chou #4 */
      if (255==iPageWidth)
      {
         myfprintf(fp, "\033&l111A");      /* Set page size */
      }
      else
         bCustomSize = CUPS_TRUE;
      break;

   case 595 : /* A5 */
      if (420==iPageWidth)
      {
         myfprintf(fp, "\033&l25A");      /* Set page size */
      }
      else
         bCustomSize = CUPS_TRUE;
      break;

   case 612 : /* Statement */
      if (396==iPageWidth)
      {
         myfprintf(fp, "\033&l15A");      /* Set page size */
      }
      else
         bCustomSize = CUPS_TRUE;
      break;

   case 624 : /* DL Envelope */
      if (312==iPageWidth)
      {
         myfprintf(fp, "\033&l90A");      /* Set page size */
      }
      else
         bCustomSize = CUPS_TRUE;
      break;

   case 649 : /* C5 Envelope */
      if (459==iPageWidth)
      {
         myfprintf(fp, "\033&l91A");      /* Set page size */
      }
      else
         bCustomSize = CUPS_TRUE;
      break;

   case 666 : 
      if (340==iPageWidth) /* Envelope Chou #3 */
      {
         myfprintf(fp, "\033&l110A");      /* Set page size */
      }
      else if (298==iPageWidth) /* Envelope You #4 */
      {
         myfprintf(fp, "\033&l831A");      /* Set page size */
      }
      else
         bCustomSize = CUPS_TRUE;
      break;

   case 684 : /* COM-10 Envelope */
      if (297==iPageWidth)
      {
         myfprintf(fp, "\033&l81A");      /* Set page size */
      }
      else
         bCustomSize = CUPS_TRUE;
      break;

   case 709 : /* B5(ISO) not Envelope */
      if (499==iPageWidth)
      {
         myfprintf(fp, "\033&l65A");     /* Set page size */
      }
      else
         bCustomSize = CUPS_TRUE;
      break;

   case 720 : /* UK Quarto */
      if (576==iPageWidth)
      {
         myfprintf(fp, "\033&l812A");      /* Set page size */
      }
      else
         bCustomSize = CUPS_TRUE;
      break;

   case 729 : /* B5 (JIS) */
      if (516==iPageWidth)
      {
         myfprintf(fp, "\033&l45A");      /* Set page size */
      }
      else
         bCustomSize = CUPS_TRUE;
      break;

   case 737 : /* Kai 16 */
      if (524==iPageWidth)
      {
         myfprintf(fp, "\033&l819A");      /* Set page size */
      }
      else
         bCustomSize = CUPS_TRUE;
      break;

   case 756 : 
      if (522==iPageWidth) /* Executive */
      {
         myfprintf(fp, "\033&l1A");      /* Set page size */
      }
      else if (576==iPageWidth) /* Government Letter */
      {
         myfprintf(fp, "\033&l7A");      /* Set page size */
      }
      else
         bCustomSize = CUPS_TRUE;
      break;

   case 765 : /* 16K */
      if (553==iPageWidth)
      {
         myfprintf(fp, "\033&l832A");      /*  Set page size */
      }
      else
         bCustomSize = CUPS_TRUE;
      break;
   
	case 792 : /* Letter */
      if (612==iPageWidth)
      {
         myfprintf(fp, "\033&l2A");     /* Set page size */
      }
      else
         bCustomSize = CUPS_TRUE;
      break;

   case 842 : /* A4 */
      if (595==iPageWidth)
      {
         myfprintf(fp, "\033&l26A");      /* Set page size */
      }
      else
         bCustomSize = CUPS_TRUE;
      break;

   case 914 : /*LetterPlus */
      if (612==iPageWidth)
      {
         myfprintf(fp, "\033&l817A");      /* Set page size */
      }
      else
         bCustomSize = CUPS_TRUE;
      break;

   case 935 : /* Folio */
      if (595==iPageWidth)
      {
         myfprintf(fp, "\033&l9A");      /* Set page size */
      }
      else
         bCustomSize = CUPS_TRUE;
      break;

   case 936 : 
      if (612==iPageWidth) /* Government Legal */
      {
         myfprintf(fp, "\033&l10A");      /* Set page size */
      }
      else if (576==iPageWidth) /* Foolscap */
      {
         myfprintf(fp, "\033&l818A");      /* Set page size */
      }
      else
         bCustomSize = CUPS_TRUE;
      break;

   case 1008 : /* Legal */
      if (612==iPageWidth)
      {
         myfprintf(fp, "\033&l3A");     /* Set page size */
      }
      else
         bCustomSize = CUPS_TRUE;
      break;

   default:
      bCustomSize = CUPS_TRUE;
      break;
   }

   if (bCustomSize)
   {
	   fprintf(stderr, "DEBUG: Paper Source is %d\n",header->MediaPosition);

		//Tray 2/Tray 3 don't support custom paper size and Mac doesn't check the constrains.
		if(TRAY_2 == header->MediaPosition || TRAY_3 == header->MediaPosition)
		{
			fprintf(stderr, "ERROR: Tray 2/3 doesn't support Custom Paper Size!\n");
			rc = 1;
			return rc;
		}
		if(iPageWidth < ppd->custom_min[0])
		{
			fprintf(stderr, "ERROR: Wrong Custom Paper Size Width: %d pt!\n",iPageWidth);
			rc = 1;
			return rc;
		}
		if(header->Duplex &&
			(iPageWidth < 516 || iPageHeight < 729))
		{
			fprintf(stderr, 
					  "ERROR: Wrong Custom Paper Size for Duplex Option: Width=%d pt, Height=%d pt!\n",
					  iPageWidth, iPageHeight);
			rc = 1;
			return rc;
		}
      myfprintf(fp, "\033&l101A");      /* Set page size */
      myfprintf(fp, "\033&f%dg%dF", iPageWidth*10, iPageHeight*10);
   }

   //myfprintf(fp, "\033&l%dP",       /* Set page length */
   //       header->PageSize[1] / 12);
   myfprintf(fp, "\033&l0E");       /* Set top margin to 0 */

   myfprintf(fp, "\033&l%dH",     /* Set media position */
				 header->MediaPosition);
	
   fprintf(stderr, "DEBUG: Media Type is %d\n",header->cupsMediaType);
   myfprintf(fp, "\033&l%dM",     /* Set media type */
			 header->cupsMediaType);

   /*
    * Set graphics mode...
    */
   /* cupsWidth and cupsHeight give different value in linux and mac*/

   WidthInBytes = (header->PageSize[0]*header->HWResolution[0]/72 
                   - left_offset -right_offset + 7) >> 3;
   //WidthInBytes = (header->cupsWidth - 2* xIgnore + 7)/8;
   if(WidthInBytes%2)  //5430DL need 16 divisible
	   WidthInBytes +=1;
   printarea_x = WidthInBytes << 3;
   printarea_y = header->PageSize[1]*header->HWResolution[1]/72
                 - top_offset -bottom_offset ;

   /*
   * Figure out the number of color planes...
   */
	if (CUPS_CSPACE_RGB == header->cupsColorSpace || 
		 CUPS_CSPACE_CMYK == header->cupsColorSpace )
   {
      NumPlanes = 4;
   }
   else
   {
      NumPlanes = 1;        /* Black&white graphics */
   }

   /*
    * Set size and position of graphics...
    */

   myfprintf(fp, "\033*r%dS", printarea_x ); /* Set width */
   myfprintf(fp, "\033*r%dT", printarea_y); /* Set height */

   //myfprintf(fp, "\033&a0H");       /* Set horizontal position */
   myfprintf(fp, "\033&l0U");
   myfprintf(fp, "\033&l0Z");
   myfprintf(fp, "\033*p%dX",left_offset);
   myfprintf(fp, "\033*p%dY", (header->HWResolution[0]/header->HWResolution[1])*top_offset);

   //if (ppd)
   // myfprintf(fp, "\033&a%.0fV",      /* Set vertical position */
   //       10.0 * (ppd->sizes[0].length - ppd->sizes[0].top));
   //else
   // myfprintf(fp, "\033&a0V");        /* Set top-of-page */

   if (header->cupsCompression)
      myfprintf(fp, "\033*b%dM",       /* Set compression */
					 header->cupsCompression);

   /*
    * Allocate memory ...
    */
   iPlaneSize = WidthInBytes * printarea_y;
   Planes[0] = malloc( iPlaneSize * NumPlanes );
   memset(Planes[0],0, iPlaneSize * NumPlanes );

   //for (plane = 0; plane < NumPlanes; plane ++)
   //  Planes[plane] = Planes[0] + plane * iPlaneSize;
   if (4==NumPlanes)
   {
      Planes[1] = Planes[0] + iPlaneSize;
      Planes[2] = Planes[0] + (iPlaneSize<<1);
      Planes[3] = Planes[0] + (iPlaneSize<<1) + iPlaneSize;
   }
   fprintf(stderr, "DEBUG: NumPlanes=%d, WidthInBytes=%d, cupsWidth=%d, cupsHeight=%d, bottom_offset=%d,ColorBits=%d\n",NumPlanes,WidthInBytes,header->cupsWidth,header->cupsHeight,bottom_offset,ColorBits);
   
	if (CUPS_CSPACE_K == header->cupsColorSpace)
	{
		pLineBuf = malloc(header->cupsWidth);
		memset(pLineBuf,0,header->cupsWidth);
	}
	if (CUPS_CSPACE_RGB == header->cupsColorSpace)
	{
		pLineBuf = malloc(header->cupsWidth*3);
		memset(pLineBuf,0, header->cupsWidth*3);
		pbCMYKline = malloc(header->cupsWidth*4);
		memset(pbCMYKline,0,(header->cupsWidth*4));
	}
	if (CUPS_CSPACE_CMYK == header->cupsColorSpace)
	{
		pbCMYKline = malloc(header->cupsWidth*4);
		memset(pbCMYKline,0,(header->cupsWidth*4));
	}
	//clean dotcount
	for(plane=0;plane<4;plane++)
	{
		dwDotCount[plane] = 0;
		dwWhiteDots[plane] = 0;
	}

   OutBuffer = malloc(OUTBUFSIZE);
#if WRITERASTERTOBMP
	for(plane=0;plane<NumPlanes; plane ++)
	{
		char filename[IPP_MAX_NAME];
		sprintf(filename, "/tmp/raster_page%d_%d.bmp", Page, plane);
		rasterplanefile[plane]=fopen(filename, "w");
		write_bmp_header(header, rasterplanefile[plane]);
	}

#endif
	return rc;
}


/*
 * 'EndPage()' - Finish a page of graphics.
 */

void
   EndPage(void)
{
#if WRITERASTERTOBMP
	int plane;
#endif
#if defined(HAVE_SIGACTION) && !defined(HAVE_SIGSET)
   struct sigaction action;  /* Actions for POSIX signals */
#endif /* HAVE_SIGACTION && !HAVE_SIGSET */

   /*
    * Eject the current page...
    */

   myfprintf(fp, "\033*rC");     /* End  GFX */

   //if (!(Duplex && (Page & 1)))
   //myfprintf(fp, "\033&l0H");    /* Eject current page */
   fputc(0x0C, fp);	/*Eject current page with FF*/
   if(usb_flag)
      usb_size +=1;

   fflush(fp);
#if WRITERASTERTOBMP
	for(plane=0;plane<NumPlanes; plane ++)
	{
		fclose(rasterplanefile[plane]);
	}
#endif

   /*
    * Unregister the signal handler...
    */

#ifdef HAVE_SIGSET /* Use System V signals over POSIX to avoid bugs */
   sigset(SIGTERM, SIG_IGN);
#elif defined(HAVE_SIGACTION)
   memset(&action, 0, sizeof(action));

   sigemptyset(&action.sa_mask);
   action.sa_handler = SIG_IGN;
   sigaction(SIGTERM, &action, NULL);
#else
   signal(SIGTERM, SIG_IGN);
#endif /* HAVE_SIGSET */

   /*
    * Free memory...
    */

   free(Planes[0]);
	Planes[0] = NULL;

   if (OutBuffer)
	{
      free(OutBuffer);
		OutBuffer = NULL;
	}
	if (pLineBuf)
	{
		free(pLineBuf);
		pLineBuf = NULL;
	}
	if (pbCMYKline)
	{
		free(pbCMYKline);
		pbCMYKline = NULL;
	}

}


/*
 * 'Shutdown()' - Shutdown the printer.
 */

void
   Shutdown(void)
{
	//int err = 0;
	int i;
	int startplane;

	if(pImage)
	{
		if( (SIGNATURE_CMHT == pImage->InitCM_HT) &&
			 (4 == pImage->NumPlanes) && 
			 (CUPS_CSPACE_RGB == pImage->iColorSpace))
		{
			cmsDeleteTransform(hTransform);
			cmsCloseProfile(hInProfile);
			cmsCloseProfile(hOutProfile);
		}

		if(pImage->NumPlanes==1)
			startplane=3;
		else
			startplane=0;


		for(i=startplane;i<4;i++)
		{
			if(pImage->pHT[i])
			{
				free(pImage->pHT[i]);
				pImage->pHT[i] = NULL;
			}
		}
		free(pImage);
		pImage = NULL;
	}

   /*
    * Send a PCL reset sequence.
    */

   fputc(0x1b, fp);
   fputc('E', fp);
	myfprintf(fp, "%s", UEL_STRING);
	if(usb_flag)
	{
		usb_size +=2;
		if(usb_size%MAX_PACKET_SIZE == 0)
		{
			fprintf(fp, "%s", UEL_STRING);
			fprintf(stderr, "DEBUG: 64 divisible\n");
		}
	}

}


/*
 * 'CancelJob()' - Cancel the current job...
 */

void
   CancelJob(int sig)      /* I - Signal */
{
   //int i;        /* Looping var */

   (void)sig;

   /*
    * Send out lots of NUL bytes to clear out any pending raster data...
    */

   //for (i = 0; i < 600; i ++)
   //   putchar(0);

	fprintf(stderr, "DEBUG: Cancel Job");
   /*
    * End the current page and exit...
    */

   EndPage();
   Shutdown();
	if(fp!=stdout&&fp)
		fclose(fp);


   exit(0);
}

/*
 * 'HoldSignals()' - Hold child and termination signals.
 */

void
HoldSignals(void)
{
#if defined(HAVE_SIGACTION) && !defined(HAVE_SIGSET)
  sigset_t		newmask;	/* New POSIX signal mask */
#endif /* HAVE_SIGACTION && !HAVE_SIGSET */


  holdcount ++;
  if (holdcount > 1)
    return;

#ifdef HAVE_SIGSET
  sighold(SIGTERM);
#elif defined(HAVE_SIGACTION)
  sigemptyset(&newmask);
  sigaddset(&newmask, SIGTERM);
  sigprocmask(SIG_BLOCK, &newmask, &holdmask);
#endif /* HAVE_SIGSET */
}

/*
 * 'ReleaseSignals()' - Release signals for delivery.
 */

void
ReleaseSignals(void)
{
  holdcount --;
  if (holdcount > 0)
    return;

#ifdef HAVE_SIGSET
  sigrelse(SIGTERM);
#elif defined(HAVE_SIGACTION)
  sigprocmask(SIG_SETMASK, &holdmask, NULL);
#endif /* HAVE_SIGSET */
}



static void OutputBie(unsigned char *start, size_t len, void *file)
{
   size_t leftsize, offset;
   if (fWriteJBigHeader&&len==20)
   {
		HoldSignals();
      fprintf(stderr, "DEBUG: Write Jbig Header\n");
      myfprintf(file,"\033*b%d%c", (int)len, 'V');
      fwrite(start, 1, len, (FILE *) file);
		if(usb_flag)
			usb_size += len%MAX_PACKET_SIZE;
      fWriteJBigHeader = CUPS_FALSE;
   }
   else
   {
      leftsize = len;
      offset=0;
      while (leftsize)
      {
         if (lSize > OUTBUFSIZE - leftsize)
         {
            memcpy(OutBuffer + lSize, start+offset, OUTBUFSIZE-lSize);
            leftsize -= OUTBUFSIZE-lSize;
            offset += OUTBUFSIZE -lSize;
            fprintf(stderr, "DEBUG: Write Jbig V Block Header\n");
            myfprintf(file,"\033*b%d%c", OUTBUFSIZE, 'V');
            fwrite(OutBuffer, 1, OUTBUFSIZE, (FILE *)file);
				if(usb_flag)
					usb_size += OUTBUFSIZE%MAX_PACKET_SIZE;
            lSize = 0;
         }
         else
         {
            memcpy(OutBuffer + lSize, start+offset, leftsize);
            lSize += leftsize;
            leftsize= 0;
         }
      }
   }
   return;
}

void OutputPlane(unsigned char *pbPlane, unsigned leftsize)
{
   unsigned datasize = 0;
   unsigned paddedbytes;
   cups_bool_t NeedWriteW = CUPS_FALSE;
   if (leftsize==0)
      NeedWriteW = CUPS_TRUE;
   while (leftsize)
   {
      if (leftsize < OUTBUFSIZE)
      {
         datasize = leftsize;
         leftsize = 0;
      }
      else
      {
         datasize = OUTBUFSIZE;
         leftsize -= OUTBUFSIZE;
      } 

      memcpy(OutBuffer, pbPlane, datasize);
      pbPlane += datasize;

      paddedbytes = JBIG_ALIGNBYTES - datasize % JBIG_ALIGNBYTES;
      myfprintf(fp, "\033*b%d%c", (datasize + paddedbytes), (datasize < OUTBUFSIZE)? 'W':'V');
      if ((datasize == OUTBUFSIZE) && (leftsize == 0))
         NeedWriteW = CUPS_TRUE;
      if (paddedbytes)
         memset(OutBuffer + datasize, 0, paddedbytes);
      fwrite(OutBuffer, 1, (datasize+ paddedbytes), fp);
		if(usb_flag)
			usb_size += (datasize+ paddedbytes)%MAX_PACKET_SIZE;
		if(datasize<OUTBUFSIZE)	
		{
			fprintf(stderr, "DEBUG: Write Jbig W Block\n");
			ReleaseSignals();
		}
   }

   if (NeedWriteW)
	{
		fprintf(stderr, "DEBUG: Write 0 W  W \n");
      myfprintf(fp, "\033*b%d%c", 0, 'W');
		ReleaseSignals();
	}
}

void WritePJL_OSINFO_TIMESTAMP(void)
{
	struct utsname uts;
	char subtype[25];
	struct tm *tmp;
	time_t now;

	time(&now);
	tmp=localtime(&now);
	myfprintf(fp, PJL_START_TIMESTAMP, tmp->tm_mon+1,tmp->tm_mday,tmp->tm_year+1900);

	uname(&uts);
	if(!strncmp(uts.sysname,"Linux",5)) 
	{
		FILE *fpTmp ;

		if (fpTmp = fopen("/etc/fedora-release", "r")) 
		{  
			sprintf(subtype, "%s", "RedHat"); 
			fclose(fpTmp);
		}
		else if(fpTmp = fopen("/etc/SuSE-release", "r")) 
		{
			sprintf(subtype, "%s", "SuSe");
			fclose(fpTmp);
		}
		else if(fpTmp = fopen("/etc/debian_version", "r")) 
		{  
			sprintf(subtype, "%s", "Debian");
			fclose(fpTmp);
		}
		else if(fpTmp = fopen("/etc/mandrake-release", "r")) 
		{
			sprintf(subtype, "%s", "mandrake");
			fclose(fpTmp);
		}
		else if(fpTmp = fopen("/etc/gentoo-release", "r")) 
		{
			sprintf(subtype, "%s", "gentoo");
			fclose(fpTmp);
		}
		else if(fpTmp = fopen("/etc/slackware-version", "r")) 
		{
			sprintf(subtype, "%s", "slackware");
			fclose(fpTmp);
		}
		else if (fpTmp = fopen("/etc/readhat-release", "r")) 
		{  
			sprintf(subtype, "%s", "RedHat"); 
			fclose(fpTmp);
		}
		else
		{
			sprintf(subtype, "%s", "Other");
		}
	}
	else if(!strncmp(uts.sysname,"FreeBSD",6))
	{
		sprintf(subtype, "%s", "");
	}

	fprintf(stderr, "DEBUG: OS is %s, subtype is %s\n", uts.sysname, subtype);
	myfprintf(fp, PJL_START_OSINFO, uts.sysname, subtype);
}

/*
 * 'main()' - Main entry and processing of driver.
 */

int                     /* O - Exit status */
   main(int  argc,      /* I - Number of command-line arguments */
        char *argv[])   /* I - Command-line arguments */
{
   int     fd;         /* File descriptor */
   cups_raster_t   *ras; /* Raster stream for printing */
   cups_page_header_t  header; /* Page header from file */
   //unsigned y;          /* Current line */
   ppd_file_t    *ppd; /* PPD file */

   unsigned char *pbOut[4]={0};
   int plane;
   cups_bool_t bColor = CUPS_FALSE;
	cups_bool_t bNotBlank = CUPS_FALSE;

   int ret;//, err=0;
   //jbig compression
   struct jbg_enc_state se;
   int mx=0, my=0;
   int options = JBG_TPDON | JBG_TPBON | JBG_DPON | JBG_LRLTWO | JBG_DELAY_AT;
   int order = JBG_ILEAVE | JBG_SMID;
	DWORD dwHori, dwVert,  dwPixelCount;
  	unsigned char *pbCHT, *pbMHT,*pbYHT,*pbKHT,*pbCHTstart,*pbMHTstart,*pbYHTstart,*pbKHTstart, *pbCMYK;
 	unsigned char *pbDstCyan, *pbDstMagenta, *pbDstYellow, *pbDstBlack;
	unsigned char bCyan, bMagenta,bYellow, bBlack;
	
	DWORD x, y, iShift;//, dwWidthInByte;
	unsigned char bTmpCyan, bTmpMagenta, bTmpYellow, bTmpBlack, bit;
	//int err = 0;

   /*
    * Make sure status messages are not buffered...
    */

   setbuf(stderr, NULL);

   /*
    * Check command-line...
    */

   if (argc < 6 || argc > 7)
   {
      /*
       * We don't have the correct number of arguments; write an error message
       * and return.
       */

      fputs("ERROR: rastertokmlf job-id user title copies options [file]\n", stderr);
      return(1);
   }

   /*
    * Open the page stream...
    */

   if (argc == 7)
   {
      if ((fd = open(argv[6], O_RDONLY)) == -1)
      {
         perror("ERROR: Unable to open raster file - ");
         sleep(1);
         return(1);
      }
   }
   else
      fd = 0;

   ras = cupsRasterOpen(fd, CUPS_RASTER_READ);

   /*
    * Initialize the print device...
    */

   ppd = ppdOpenFile(getenv("PPD"));

   fprintf(stderr, "DEBUG: ppd language is %s\n",ppd->lang_version);

   ret=Setup();
   if (ret==-1)
   {
      //fputs("INFO: Wrong Printer Found!\n", stderr);
      fputs("INFO: Printer Not Found!\n", stderr);
      //fprintf(stderr, "ERROR: Printer Not Found!\n");
#ifdef __linux
      sleep(30);
#endif
      return(1);
   }

   /*
    * Process pages as needed...
    */

   Page = 0;

   while (cupsRasterReadHeader(ras, &header))
   {
      /*
       * Write a status message with the page number and number of copies.
       */

      Page ++;
      bColor = CUPS_FALSE; //assumed b/w page

      fprintf(stderr, "PAGE: %d %d\n", Page, header.NumCopies);

		if(Page==1)
		{
			fp=stdout;

			myfprintf(fp, "%s",UEL_STRING);
			myfprintf(fp, PJL_START_JOB, argv[3]);

			WritePJL_OSINFO_TIMESTAMP();

		   /*
			 * Send a PCL reset sequence.
			 */
			myfprintf(fp, "%s", UEL_STRING);
			myfprintf(fp, "%s", PJL_ENTER_KMLF_STRING);
		
			fputc(0x1b, fp);
			fputc('E', fp);
			if(usb_flag)
				usb_size +=2;
		}

      /*
       * Start the page...
       */
		fprintf(stderr, "DEBUG: Paper Source is %d\n",header.MediaPosition);
		fprintf(stderr, "DEBUG: Color Space is %d\n",header.cupsColorSpace);
		//mac os x send mediaposition=0 if user doesn't touch the paper source
		if(!header.MediaPosition)
			header.MediaPosition = TRAY_AUTOSELECT; //7 is printer auto select
		//check page dimension because MacOSX doesn't check contraints
		if(TRAY_AUTOSELECT == header.MediaPosition)
		{
			if((header.PageSize[0] > ppd->custom_max[0]) ||
				(header.PageSize[0] < 255) || //ppd->custom_min[0]) ||
				(header.PageSize[1] > 1008) || 
				(header.PageSize[1] < ppd->custom_min[1]))
			{
				fprintf(stderr, "ERROR: Wrong Paper Size for Printer Auto Select: Width=%d pt, Height=%d pt!\n",
						  header.PageSize[0], header.PageSize[1] );
#ifdef __linux
				sleep(30);
#endif
				return(1);
			}
		}

		else if(TRAY_1 == header.MediaPosition) //Tray 1, assuming only Tray support custom paper size
		{
			if((header.PageSize[0] > ppd->custom_max[0]) ||
				(header.PageSize[0] < 255) ||//ppd->custom_min[0]) || //255 is for Chou#4
				(header.PageSize[1] > ppd->custom_max[1]) || 
				(header.PageSize[1] < ppd->custom_min[1]))
			{
				fprintf(stderr, "ERROR: Wrong Paper Size for Tray 1: Width=%d pt, Height=%d pt!\n",
						  header.PageSize[0], header.PageSize[1] );
#ifdef __linux
				sleep(30);
#endif
				return(1);
			}
		}
		else if(TRAY_2 == header.MediaPosition || TRAY_3 == header.MediaPosition)
		{
			if((header.PageSize[0] > ppd->custom_max[0]) ||
				(header.PageSize[0] < ppd->custom_min[0]) ||
				(header.PageSize[1] > 1008) || 
				(header.PageSize[1] < 729))
			{
				fprintf(stderr, "ERROR: Wrong Paper Size for Tray 2/3: Width=%d pt, Height=%d pt!\n",
						  header.PageSize[0], header.PageSize[1] );
#ifdef __linux
				sleep(30);
#endif
				return(1);
			}
		}
		else
		{
			fprintf(stderr, "ERROR: Wrong Tray: %d!\n",header.MediaPosition );
#ifdef __linux
				sleep(30);
#endif
			return(1);
		}

		pImage->xResolution = header.HWResolution[0];
		if(!pImage->InitCM_HT)
		{
			if(CUPS_CSPACE_RGB == header.cupsColorSpace)
			{
				int err=0;
				char szFile2[ 256];//,szFile1[256];

				pImage->NumPlanes = 4;
				pImage->iColorSpace = CUPS_CSPACE_RGB;

				/* Set profile data */
				//sprintf(szFile1, "%s/Profiles/sRGB.icm", KM_DATADIR); 
				//hInProfile=cmsOpenProfileFromFile(szFile1,"r");
				hInProfile=cmsCreate_sRGBProfile();
				sprintf(szFile2, "%s/Profiles/km_%d.icm", 
						  KM_DATADIR, pImage->xResolution/1200); 
				hOutProfile = cmsOpenProfileFromFile(szFile2,"r");
				hTransform = cmsCreateTransform(hInProfile,TYPE_RGB_8,hOutProfile,TYPE_CMYK_8,header.cupsRowCount,0);
				if(err)
				{
					fprintf(stderr, "ERROR: Loading Colormatching Profiles Error.\n");
					return(1);
				}
			}
			else if(CUPS_CSPACE_CMYK == header.cupsColorSpace)
			{
				pImage->NumPlanes = 4;
				pImage->iColorSpace = CUPS_CSPACE_CMYK;
			}
			else if(CUPS_CSPACE_K == header.cupsColorSpace)
			{
				pImage->NumPlanes = 1;
				pImage->iColorSpace = CUPS_CSPACE_K;
			}
			//now load halftone data
			if(header.HWResolution[0]>=600)
			{
				if(getHalftoneByResolution(pImage)==CUPS_FALSE)
				{
					fprintf(stderr, "ERROR: Loading Halftones Error.\n");
					if(4 == pImage->NumPlanes && 
						CUPS_CSPACE_RGB == header.cupsColorSpace )
					{
						cmsDeleteTransform(hTransform);
						cmsCloseProfile(hInProfile);
						cmsCloseProfile(hOutProfile);
					}
					return(1);
				}
			}
			else
			{
				fprintf(stderr, "ERROR: Resolution %d is not support.\n",header.HWResolution[0]);
				if(4 == pImage->NumPlanes &&
					CUPS_CSPACE_RGB == header.cupsColorSpace)
				{
					cmsDeleteTransform(hTransform);
					cmsCloseProfile(hInProfile);
					cmsCloseProfile(hOutProfile);
				}
				return(1);
			}
			
			pImage->InitCM_HT = SIGNATURE_CMHT;
		}

      ret = StartPage(ppd, &header);
		if(ret==1)
		{
#ifdef __linux
				sleep(30);
#endif
				return(1);
		}

      pbOut[0] = Planes[0];
      if (4==NumPlanes)
      {
         pbOut[1] = Planes[1];
         pbOut[2] = Planes[2];
         pbOut[3] = Planes[3];
      }

		dwHori 	= header.cupsWidth;
		dwVert 	= header.cupsHeight;
      //dwWidthInByte = (dwHori + 7)/8;
		
		PHTHEADER pCHThdr = (PHTHEADER)pImage->pHT[0];
		PHTHEADER pMHThdr = (PHTHEADER)pImage->pHT[1];
		PHTHEADER pYHThdr = (PHTHEADER)pImage->pHT[2];
		PHTHEADER pKHThdr = (PHTHEADER)pImage->pHT[3];

		//pbSrc = pSrcBitmap;
#ifdef CUPS_FLIP
		if (Duplex ==1 && ((Page+1) & 1)) //only for longedge
		{
			//plane order YMCK
			pbDstBlack 	= Planes[0] + (printarea_y +yIgnore-1) * WidthInBytes;
			pbDstCyan	= Planes[1] + (printarea_y +yIgnore-1) * WidthInBytes;
			pbDstMagenta= Planes[2] + (printarea_y +yIgnore-1) * WidthInBytes;
			pbDstYellow = Planes[3] + (printarea_y +yIgnore-1) * WidthInBytes;
		}
		else
#endif
		{
			//plane order YMCK
			pbDstBlack 	= Planes[0];
			pbDstCyan	= Planes[1];
			pbDstMagenta= Planes[2];
			pbDstYellow = Planes[3];
		}
		
		/*
       * Loop for each line on the page...
       */

      for (y = 0; y < dwVert; y ++)
      {

         /*
          * Let the user know how far we have progressed...
          */

         if ((y & 127) == 0)
            fprintf(stderr, "INFO: Printing page %d, %d%% complete...\n", Page, (int)(y * 100 / dwVert));

         /*
          * Read a line of graphics...
          */

			if(CUPS_CSPACE_CMYK == header.cupsColorSpace)
			{
				if (cupsRasterReadPixels(ras, pbCMYKline, header.cupsBytesPerLine) < 1)
					break;
			}
			else
			{
				if (cupsRasterReadPixels(ras, pLineBuf, header.cupsBytesPerLine) < 1)
					break;
			}
#if WRITERASTERTOBMP
			for(plane = 0; plane < NumPlanes; plane++)
			{
				int raster = header.cupsBytesPerLine/NumPlanes;
				fwrite((const char *)pLineBuf + plane*raster, 1, (raster + (-raster & 3)), rasterplanefile[plane]);
			}
#endif
         /* ignore lines that are above the upper margin */
         if (y < (unsigned)yIgnore)
         {
            continue;
         }

         /* ignore lines that are below the bottom margin */
         if (y >= (printarea_y+yIgnore))
         {
            continue;
         }

			dwPixelCount = 0;

			if(CUPS_CSPACE_RGB == header.cupsColorSpace)
			{
				if(pLineBuf[0]!=255 || 
					memcmp(pLineBuf, pLineBuf + 1, dwHori*3 - 1))
				{
					bNotBlank = CUPS_TRUE;
				}
				else
				{
					bNotBlank = CUPS_FALSE;
				}
			}
			else if(CUPS_CSPACE_CMYK == header.cupsColorSpace)
			{
				if(pbCMYKline[0] || 
					memcmp(pbCMYKline, pbCMYKline + 1, dwHori*4 - 1))
				{
					bNotBlank = CUPS_TRUE;
				}
				else
				{
					bNotBlank = CUPS_FALSE;
				}
			}
			else if(CUPS_CSPACE_K == header.cupsColorSpace)
			{
				if(pLineBuf[0] || 
					memcmp(pLineBuf, pLineBuf + 1, dwHori - 1))
				{
					bNotBlank = CUPS_TRUE;
				}
				else
				{
					bNotBlank = CUPS_FALSE;
				}
			}

			if( (CUPS_CSPACE_RGB == header.cupsColorSpace ||
				  CUPS_CSPACE_CMYK == header.cupsColorSpace) &&
				 (8 == header.cupsBitsPerColor) )
			{
				//skip the white lines in non-blank band. 
				//This doesn't seem to help much
				if(bNotBlank)
				{
					pbCHTstart = ((BYTE *)(pCHThdr+1) + (y % pCHThdr->cyHT)*pCHThdr->cxHT);
					pbMHTstart = ((BYTE *)(pMHThdr+1) + (y % pMHThdr->cyHT)*pMHThdr->cxHT);
					pbYHTstart = ((BYTE *)(pYHThdr+1) + (y % pYHThdr->cyHT)*pYHThdr->cxHT);
					pbKHTstart = ((BYTE *)(pKHThdr+1) + (y % pKHThdr->cyHT)*pKHThdr->cxHT);
	
					pbCHT = pbCHTstart;
					pbMHT = pbMHTstart;
					pbYHT = pbYHTstart;
					pbKHT = pbKHTstart;
		
					if(CUPS_CSPACE_RGB == header.cupsColorSpace)
					{
						//text & graph color matching
						cmsDoTransform(hTransform, pLineBuf, pbCMYKline,dwHori);
					}
	
					pbCMYK = pbCMYKline;
	
					for(x=0; x < WidthInBytes; x++)
					{
						bTmpCyan=bTmpMagenta=bTmpYellow=bTmpBlack=0;
						for(iShift=0; iShift<8; iShift++)
						{
							bit = 0x80>>iShift;

							bCyan		= *pbCMYK++;
							bMagenta	= *pbCMYK++;
							bYellow	= *pbCMYK++;
							bBlack	= *pbCMYK++;
							
							if(bCyan || bMagenta || bYellow || bBlack)
							{
	
								if(bCyan>=*pbCHT)
									bTmpCyan 	|= bit;
								
								if(bMagenta>=*pbMHT)
									bTmpMagenta |= bit;
								
								if(bYellow>=*pbYHT)
									bTmpYellow 	|= bit;
								
								if(bBlack>=*pbKHT)
									bTmpBlack 	|= bit;
								
							}
	
							pbCHT++;
							pbMHT++;
							pbYHT++;
							pbKHT++;
							dwPixelCount++;
							if(dwPixelCount>=dwHori)
								break;
						}

						if(!bColor)
						{
							if(bTmpCyan || bTmpMagenta || bTmpYellow)
								bColor = CUPS_TRUE;
						}

						*pbDstCyan++ 		= bTmpCyan;
						*pbDstMagenta++ 	= bTmpMagenta;
						*pbDstYellow++ 	= bTmpYellow;
						*pbDstBlack++ 		= bTmpBlack;
	
						if(pbCHT >= (pbCHTstart + pCHThdr->cxHT))
						{
							pbCHT -= pCHThdr->cxHT;
						}
						if(pbMHT >= (pbMHTstart + pMHThdr->cxHT))
						{
							pbMHT -= pMHThdr->cxHT;
						}
						if(pbYHT >= (pbYHTstart + pYHThdr->cxHT))
						{
							pbYHT -= pYHThdr->cxHT;
						}
						if(pbKHT >= (pbKHTstart + pKHThdr->cxHT))
						{
							pbKHT -= pKHThdr->cxHT;
						}
					}
#ifdef CUPS_FLIP
					if (Duplex ==1 && ((Page+1) & 1)) //only for longedge
					{
						pbDstCyan		-= WidthInBytes<<1;
						pbDstMagenta	-= WidthInBytes<<1;
						pbDstYellow		-= WidthInBytes<<1;
						pbDstBlack		-= WidthInBytes<<1;
					}
#endif
				}
				else
				{
#ifdef CUPS_FLIP
					if (Duplex ==1 && ((Page+1) & 1)) //only for longedge
					{
						pbDstCyan		-= WidthInBytes;
						pbDstMagenta	-= WidthInBytes;
						pbDstYellow		-= WidthInBytes;
						pbDstBlack		-= WidthInBytes;
					}
					else
#endif
					{
						pbDstCyan		+= WidthInBytes;
						pbDstMagenta	+= WidthInBytes;
						pbDstYellow		+= WidthInBytes;
						pbDstBlack		+= WidthInBytes;
					}

				}
			}
			else if( (CUPS_CSPACE_K == header.cupsColorSpace) && 
						(8 == header.cupsBitsPerColor) )						
			{
				//skip the white lines in non-blank band. 
				if(bNotBlank)
				{
					pbKHTstart = ((BYTE *)(pKHThdr+1) + (y % pKHThdr->cyHT)*pKHThdr->cxHT);
					pbKHT = pbKHTstart;
		
					pbCMYK = pLineBuf;
					for(x=0; x < WidthInBytes; x++)
					{
						bTmpBlack=0;
						for(iShift=0; iShift<8; iShift++)
						{
							bit = 0x80>>iShift;
							bBlack	= *pbCMYK++;
							if(bBlack)
							{
								if(bBlack>=*pbKHT)
								{
									bTmpBlack 	|= bit;
								}
							}

							pbKHT++;
							dwPixelCount++;
							if(dwPixelCount>=dwHori)
								break;
						}
							
						*pbDstBlack++ 	= bTmpBlack;
	
						if(pbKHT >= (pbKHTstart + pKHThdr->cxHT))
						{
							pbKHT -= pKHThdr->cxHT;
						}
					}
#ifdef CUPS_FLIP
					if (Duplex ==1 && ((Page+1) & 1)) //only for longedge
					{
						pbDstBlack		-= WidthInBytes<<1;
					}
#endif
				}
				else
				{
#ifdef CUPS_FLIP
					if (Duplex ==1 && ((Page+1) & 1)) //only for longedge
					{
						pbDstBlack		-= WidthInBytes;
					}
					else
#endif
					{
						pbDstBlack	+= WidthInBytes;
					}
				}
				
			}
      }

      /*
      * Figure out the number of color planes...
      */
      //if cmy planes don't have bits on, the bColor is 0
      if (bColor && 
			 (CUPS_CSPACE_RGB == header.cupsColorSpace ||
			  CUPS_CSPACE_CMYK == header.cupsColorSpace ))
      //if (((bColor <<4) | header.cupsColorSpace) == ((CUPS_TRUE <<4) | CUPS_CSPACE_RGB))
      {
         NumPlanes = 4;
         myfprintf(fp,"\033*r-1004U");
      }
      else
      {
         NumPlanes = 1;        /* Black&white graphics */
         myfprintf(fp,"\033*r1U");
      }

#ifndef CUPS_FLIP
		if (Duplex ==1 && ((Page+1) & 1)) //only for longedge
		{
			for ( plane = 0; plane < NumPlanes; plane++)
			{
       		ret = RotateImage(Planes[plane],
										WidthInBytes * printarea_y);
			}
		}
#endif

		//count dot
		for( plane=0; plane < NumPlanes; plane++)
		{
			CountDots(plane, Planes[plane], WidthInBytes * printarea_y);
		}
      /*
      * Send 26-byte or 8-byte configure image data command with horizontal and
      * vertical resolutions as well as a color count...
      */

      if (4 == NumPlanes)
         myfprintf(fp,"\033*g26W");
      else
         myfprintf(fp,"\033*g8W");

      fputc(2, fp);         /* Format 2 */
      fputc(NumPlanes, fp);     /* Output planes */
		if(usb_flag)
			usb_size +=2;

      if (4 == NumPlanes )
      {
         fputc(header.HWResolution[0]>>8, fp); /* Yellow resolution */
         fputc(header.HWResolution[0], fp);
         fputc(header.HWResolution[1]>>8, fp);
         fputc(header.HWResolution[1], fp);
         fputc(0, fp);
         fputc(1 << ColorBits, fp);      /* # of yellow levels */

         fputc(header.HWResolution[0]>>8, fp); /* Magenta resolution */
         fputc(header.HWResolution[0], fp);
         fputc(header.HWResolution[1]>>8, fp);
         fputc(header.HWResolution[1], fp);
         fputc(0, fp);
         fputc(1 << ColorBits, fp);      /* # of magenta levels */

         fputc(header.HWResolution[0]>>8, fp );  /* Cyan resolution */
         fputc(header.HWResolution[0], fp);
         fputc(header.HWResolution[1]>>8, fp);
         fputc(header.HWResolution[1], fp);
         fputc(0, fp);
         fputc(1 << ColorBits, fp);      /* # of cyan levels */
			if(usb_flag)
				usb_size +=18;
      }

      fputc(header.HWResolution[0]>>8, fp );  /* Black resolution */
      fputc(header.HWResolution[0], fp);
      fputc(header.HWResolution[1]>>8, fp );
      fputc(header.HWResolution[1], fp);
      fputc(0, fp);
      fputc(1 << ColorBits, fp);      /* # of black levels */
		if(usb_flag)
			usb_size +=6;

      myfprintf(fp, "\033*r1A");       /* Start graphics */

      //write the data for Yello, Magenta, Cyan, Black
      if (header.cupsCompression)
      {
         for (plane = NumPlanes-1; plane >= 0 ;plane--)
         {
            /*
             * Set the length of the data and write a raster plane...
             */
            //long lPlaneSize = WidthInBytes*printarea_y;
            //pbOut[plane] = Planes[plane];

            fWriteJBigHeader = CUPS_TRUE;

            jbg_enc_init(&se, printarea_x, printarea_y, 1, &Planes[plane], OutputBie, fp);

            jbg_enc_layers(&se,0);
            jbg_enc_options(&se, order, options, -1, mx, my);

            jbg_enc_out(&se);
            jbg_enc_free(&se);

            OutputPlane(OutBuffer,lSize); //send out the left data in the buffer even if lsize=0
            lSize = 0;

				//write dotcount information
				myfprintf(fp, STR_LAVAFLOW_CMD_DOTS, dwDotCount[plane], symDot[plane]);
				myfprintf(fp, STR_LAVAFLOW_CMD_WHITEDOTS, dwWhiteDots[plane], symDotW[plane]);
         }
      }
      else
      {
         for (plane = NumPlanes-1; plane >= 0 ;plane--)
         {
            /*
            * Set the length of the data and write a raster plane...
            */
            //long lPlaneSize = WidthInBytes*printarea_y;
            //pbOut[plane] = Planes[plane];
            OutputPlane(Planes[plane],WidthInBytes*printarea_y);

				//write dotcount information
				myfprintf(fp, STR_LAVAFLOW_CMD_DOTS, dwDotCount[plane], symDot[plane]);
				myfprintf(fp, STR_LAVAFLOW_CMD_WHITEDOTS, dwWhiteDots[plane], symDotW[plane]);
         }
      }

      /*
       * Eject the page...
       */

      EndPage();
   }


   /*
    * Shutdown the printer...
    */

   Shutdown();

   if (ppd)
      ppdClose(ppd);

   /*
    * Close the raster stream...
    */

   cupsRasterClose(ras);
   if (fd != 0)
      close(fd);

   /*
    * If no pages were printed, send an error message...
    */

   if (Page == 0)
      fputs("ERROR: No pages found!\n", stderr);
   else
      fputs("INFO: CUPS is ready to print.\n", stderr);

   return(Page == 0);
}

#ifndef CUPS_FLIP
///////////////////////////////////////////////////////////////////////////////
//
//  Function     :	Reverse.
//  
//  Detail       :	Bit reverse data
//  
//  Remarks      :	Reverse[x] is bit reversed data of x
//  
///////////////////////////////////////////////////////////////////////////////
const unsigned char Reverse[] = {
	0x00, 0x80, 0x40, 0xC0, 0x20, 0xA0, 0x60, 0xE0,
	0x10, 0x90, 0x50, 0xD0, 0x30, 0xB0, 0x70, 0xF0,
	0x08, 0x88, 0x48, 0xC8, 0x28, 0xA8, 0x68, 0xE8,
	0x18, 0x98, 0x58, 0xD8, 0x38, 0xB8, 0x78, 0xF8,
	0x04, 0x84, 0x44, 0xC4, 0x24, 0xA4, 0x64, 0xE4,
	0x14, 0x94, 0x54, 0xD4, 0x34, 0xB4, 0x74, 0xF4,
	0x0C, 0x8C, 0x4C, 0xCC, 0x2C, 0xAC, 0x6C, 0xEC,
	0x1C, 0x9C, 0x5C, 0xDC, 0x3C, 0xBC, 0x7C, 0xFC,
	0x02, 0x82, 0x42, 0xC2, 0x22, 0xA2, 0x62, 0xE2,
	0x12, 0x92, 0x52, 0xD2, 0x32, 0xB2, 0x72, 0xF2,
	0x0A, 0x8A, 0x4A, 0xCA, 0x2A, 0xAA, 0x6A, 0xEA,
	0x1A, 0x9A, 0x5A, 0xDA, 0x3A, 0xBA, 0x7A, 0xFA,
	0x06, 0x86, 0x46, 0xC6, 0x26, 0xA6, 0x66, 0xE6,
	0x16, 0x96, 0x56, 0xD6, 0x36, 0xB6, 0x76, 0xF6,
	0x0E, 0x8E, 0x4E, 0xCE, 0x2E, 0xAE, 0x6E, 0xEE,
	0x1E, 0x9E, 0x5E, 0xDE, 0x3E, 0xBE, 0x7E, 0xFE,
	0x01, 0x81, 0x41, 0xC1, 0x21, 0xA1, 0x61, 0xE1,
	0x11, 0x91, 0x51, 0xD1, 0x31, 0xB1, 0x71, 0xF1,
	0x09, 0x89, 0x49, 0xC9, 0x29, 0xA9, 0x69, 0xE9,
	0x19, 0x99, 0x59, 0xD9, 0x39, 0xB9, 0x79, 0xF9,
	0x05, 0x85, 0x45, 0xC5, 0x25, 0xA5, 0x65, 0xE5,
	0x15, 0x95, 0x55, 0xD5, 0x35, 0xB5, 0x75, 0xF5,
	0x0D, 0x8D, 0x4D, 0xCD, 0x2D, 0xAD, 0x6D, 0xED,
	0x1D, 0x9D, 0x5D, 0xDD, 0x3D, 0xBD, 0x7D, 0xFD,
	0x03, 0x83, 0x43, 0xC3, 0x23, 0xA3, 0x63, 0xE3,
	0x13, 0x93, 0x53, 0xD3, 0x33, 0xB3, 0x73, 0xF3,
	0x0B, 0x8B, 0x4B, 0xCB, 0x2B, 0xAB, 0x6B, 0xEB,
	0x1B, 0x9B, 0x5B, 0xDB, 0x3B, 0xBB, 0x7B, 0xFB,
	0x07, 0x87, 0x47, 0xC7, 0x27, 0xA7, 0x67, 0xE7,
	0x17, 0x97, 0x57, 0xD7, 0x37, 0xB7, 0x77, 0xF7,
	0x0F, 0x8F, 0x4F, 0xCF, 0x2F, 0xAF, 0x6F, 0xEF,
	0x1F, 0x9F, 0x5F, 0xDF, 0x3F, 0xBF, 0x7F, 0xFF
};

///////////////////////////////////////////////////////////////////////////////
//
//  Function     :	Rotate Image  Block
//  
//  Detail       :	Rotate image block by 180 degree
//  
//  Return Value :	always TRUE
//  
//  Remarks      :	Needs data "Reverse[]" for this function
//  
///////////////////////////////////////////////////////////////////////////////
int RotateImage(
	unsigned char *pBuffer,		// image buffer to be rotated
	unsigned long cbData			// size of data in bytes
)
{
	unsigned char *p = pBuffer;	// pointer
	unsigned long c =  cbData / 2;	// counter
	unsigned char bTmp;
	unsigned char *pDest = NULL;

	// Reverse the image block by 180 deg
	while( c-- )
	{
		pDest = pBuffer + cbData - 1 - ( p - pBuffer );
		bTmp = *pDest;			// save destination byte in 'b'
		*pDest = Reverse[ *p ];	// put reverse of *p
		*p = Reverse[ bTmp ];	// data is swapped and reversed.
		p++;
	}

	//if cbData is odd number, the most middle byte need reverse.
	if(cbData%2)		
		*p = Reverse[*p];
	
	
	return CUPS_TRUE;
}

#endif

//lookup table to count the number of 1's in numbers from 0x00 to 0xFF.
const int gLookupTb[]={ 0, 1, 1, 2, 1, 2, 2, 3, 
						1, 2, 2, 3, 2, 3, 3, 4, 
						1, 2, 2, 3, 2, 3, 3, 4, 
						2, 3, 3, 4, 3, 4, 4, 5, 
						1, 2, 2, 3, 2, 3, 3, 4, 
						2, 3, 3, 4, 3, 4, 4, 5, 
						2, 3, 3, 4, 3, 4, 4, 5, 
						3, 4, 4, 5, 4, 5, 5, 6, 
						1, 2, 2, 3, 2, 3, 3, 4, 
						2, 3, 3, 4, 3, 4, 4, 5, 
						2, 3, 3, 4, 3, 4, 4, 5, 
						3, 4, 4, 5, 4, 5, 5, 6, 
						2, 3, 3, 4, 3, 4, 4, 5, 
						3, 4, 4, 5, 4, 5, 5, 6, 
						3, 4, 4, 5, 4, 5, 5, 6, 
						4, 5, 5, 6, 5, 6, 6, 7, 
						1, 2, 2, 3, 2, 3, 3, 4, 
						2, 3, 3, 4, 3, 4, 4, 5, 
						2, 3, 3, 4, 3, 4, 4, 5, 
						3, 4, 4, 5, 4, 5, 5, 6, 
						2, 3, 3, 4, 3, 4, 4, 5, 
						3, 4, 4, 5, 4, 5, 5, 6, 
						3, 4, 4, 5, 4, 5, 5, 6, 
						4, 5, 5, 6, 5, 6, 6, 7, 
						2, 3, 3, 4, 3, 4, 4, 5, 
						3, 4, 4, 5, 4, 5, 5, 6, 
						3, 4, 4, 5, 4, 5, 5, 6, 
						4, 5, 5, 6, 5, 6, 6, 7, 
						3, 4, 4, 5, 4, 5, 5, 6, 
						4, 5, 5, 6, 5, 6, 6, 7, 
						4, 5, 5, 6, 5, 6, 6, 7, 
						5, 6, 6, 7, 6, 7, 7, 8	 };		
//Count the number of turned-on bits in the buffer and add the number 
//to the appropriate bit-counting variables.
int CountDots(int p, unsigned char *pBuf, unsigned long dwLen)
{
	unsigned long count=0, off=0, i;
	unsigned char *pTmp;

	if( !pBuf || dwLen == 0)
		return -1;
	pTmp = pBuf;
	for(i=0;i<dwLen;i++){
		//get the number of turned-on bits from the lookup table.
		count += gLookupTb[*pTmp];
		pTmp++;
	}

	off = 8*dwLen - count;

	dwDotCount[p] += count;
	dwWhiteDots[p] += off;

	return 0;
}

static void myfprintf(FILE *stream, const char *format, ...) 
{
   va_list args;
	int len;
   va_start(args, format);

	len=vfprintf(stream, format, args);

	if(usb_flag)
	{
		if(len>=0)
			usb_size += len%MAX_PACKET_SIZE;
		else
			fprintf(stderr, "DEBUG: output error\n");
	}
   va_end(args);
}

