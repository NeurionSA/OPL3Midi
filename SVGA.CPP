/********************************************************************
**
** VIDEO.CPP
**
** Video Driver functions and logic
**
********************************************************************/

//#include <stdio.h>      // for debug output
#include <string.h>     // for strncpy, memcpy
#include "svga.h"
#include "dpmi.h"       // for DOS low-memory allocation
#include "globals.h"    // for data types

// use the SVGA namespace
namespace SVGA {

/******** CONSTANTS ********/
#define  MAX_NUM_MODES     64    // maximum number of video modes we copy from the list
#define  MAX_VIDEO_HEIGHT  1024  // maximum vertical resolution the driver will support
#define  MAX_VIDEO_PAGES   8     // maximum number of video pages we provide support for
#define  MAX_FONT_HEIGHT   16    // maximum pixel height of fonts

// SuperVGA Mode Attribute Flags
#define  MA_SUPPORTED      0x0001   // mode supported by present hardware configuration
#define  MA_OPTIONALINFO   0x0002   // optional information available (must be =1 for VBE v1.2+)
#define  MA_BIOSOUTPUT     0x0004   // BIOS output supported
#define  MA_COLOR          0x0008   // set if color, clear if monochrome
#define  MA_GRAPHICS       0x0010   // set if graphics mode, clear if text mode
// --- VBE v2.0+ ---
#define  MA_NOTVGACOMPAT   0x0020   // mode is not VGA-compatible
#define  MA_NOBANKSWITCH   0x0040   // bank-switched mode not supported
#define  MA_LINEARSUPPORT  0x0080   // linear framebuffer mode supported
#define  MA_DOUBLESCAN     0x0100   // double-scan mode available (e.g. 320x200 and 320x240)
// --- VBE v3.0 ---
#define  MA_INTERLACED     0x0200   // interlaced mode available
#define  MA_TRIPLEBUFFER   0x0400   // hardware supports triple buffering
#define  MA_STEREOSCOPIC   0x0800   // hardware supports stereoscopic display
#define  MA_DUALDISPLAY    0x1000   // dual display start address support

// SuperVGA Memory Model Types
#define  MM_TEXT        0x00  // Text
#define  MM_CGA         0x01  // CGA graphics
#define  MM_HGC         0x02  // HGC graphics
#define  MM_EGA         0x03  // 16-color (EGA) graphics
#define  MM_PACKED      0x04  // packed pixel graphics
#define  MM_SEQU256     0x05  // "sequ 256" (non-chain 4) graphics
#define  MM_DIRECT      0x06  // direct color (HiColor, 24-bit color)
#define  MM_YUV         0x07  // YUV (luminance-chrominance, also called YIQ)

/******** MACROS ********/
// converts a real-mode pointer (in DWORD SEG:OFF form) to a protected-mode pointer
#define  RM_TO_PM(x)    ( ( ( ( x ) & 0xFFFF0000 ) >> 12 ) + ( ( x ) & 0xFFFF ) )

#define  FONT_HEIGHT       16    // default fixed font size is 8 x 16
// definitions of page dimensions, so I can easily incorporate logical page sizes later
#define  PAGE_WIDTH        modeInfo -> width
#define  PAGE_HEIGHT       modeInfo -> height

/******** STRUCTS ********/
// SVGA General Information
typedef struct {
   Byte     signature[ 4 ];
   UInt16   vesaVersion;
   UInt32   oemNamePtr;
   UInt32   capabilities;
   UInt32   modeListPtr;
   UInt16   totalVideoMemory;    // in 64K blocks
   // --- VBE v2.0 ---
   UInt16   oemVersion;
   UInt32   vendorNamePtr;
   UInt32   productNamePtr;
   UInt32   productRevisionPtr;
   UInt16   vbeAfVersion;
   UInt32   accelModeListPtr;
   Byte     reserved[ 216 ];
   Byte     oemScratch[ 256 ];
} SVGAInfo;

// SVGA Mode Information
typedef struct {
   UInt16   modeAttr;   // mode attributes
   Byte     winAttrA;   // window attributes, window A
   Byte     winAttrB;   // window attributes, window B
   UInt16   winGran;    // window granularity in KB
   UInt16   winSize;    // window size in KB
   UInt16   winSegA;    // start segment of window A
   UInt16   winSegB;    // start segment of window B
   UInt32   winPosFuncPtr; // -> FAR window positioning function
   UInt16   bytesScanline; // bytes per scanline
   UInt16   width;      // width in pixels (graphics) or characters (text)
   UInt16   height;     // height in pixels (graphics) or characters (text)
   Byte     charWidth;  // width of character cell in pixels
   Byte     charHeight; // height of character cell in pixels
   Byte     numPlanes;  // number of memory planes
   Byte     bitDepth;   // number of bits per pixel
   Byte     numBanks;   // number of banks
   Byte     memModel;   // memory model type
   Byte     bankSize;   // size of bank in KB
   Byte     numPages;   // number of image pages (less one) that will fit in video RAM
   Byte     reserved1E;
   // --- VBE v1.2+ ---
   Byte     rMaskSize;
   Byte     rFieldPos;
   Byte     gMaskSize;
   Byte     gFieldPos;
   Byte     bMaskSize;
   Byte     bFieldPos;
   Byte     aMaskSize;  // reserved mask size
   Byte     aFieldPos;  // reserved mask position
   Byte     dcmInfo;    // direct color mode info
   // --- VBE v2.0+ ---
   UInt32   linearBufferPhys; // physical address of linear video buffer
   UInt32   offscreenMemPtr;  // pointer to start of offscreen memory
   UInt16   offscreenMemSize; // KB of offscreen memory
   // --- VBE v3.0 ---
   UInt16   linearScanline;   // bytes per scan line in linear modes
   Byte     numPagesBanked;   // number of images (less one) for banked video modes
   Byte     numPagesLinear;   // number of images (less one) for linear video modes
   Byte     dcRMaskSize;
   Byte     dcRFieldPos;
   Byte     dcGMaskSize;
   Byte     dcGFieldPos;
   Byte     dcBMaskSize;
   Byte     dcBFieldPos;
   Byte     dcAMaskSize;
   Byte     dcAFieldPos;
   UInt32   maxPixelClock; // maximum pixel clock for graphics video mode, in Hz
   Byte     reserved42[ 190 ];
} ModeInfo;

/******** FUNCTION DECLARATIONS ********/
// (for functions not declared in the header)

/******** VARIABLES ********/
bool        inited = false;   // whether the driver has been initialized

// video hardware info
SVGAInfo    svgaInfo;         // SVGA info block
ModeInfo *  modeInfo;         // pointer to mode info block (allocated low)
UInt16      modeInfoSel;      // selector returned by DPMI for allocated mode info block
UInt16      modeList[ MAX_NUM_MODES ];    // list of supported video modes
UInt16      numVideoModes;    // number of supported video modes
UInt16      numPages;         // number of video pages the current mode supports

// drawing settings
UInt16      targetPage;       // index of the target page for drawing operations
Byte        drawColor;        // current color for drawing functions

// rendering pointers
void *      linearFB;         // pointer to the mapped linear framebuffer
Byte *      romFont;          // pointer to a font defined in ROM

// run-time generated LUTs
UInt32      scanOffsetLUT[ MAX_VIDEO_HEIGHT ];  // LUT of scanline offsets (into each page)
UInt32      pageOffsetLUT[ MAX_VIDEO_PAGES ];   // LUT of video page offsets (into linear framebuffer)
UInt32      fixedFontLUT[ MAX_FONT_HEIGHT << 9 ];  // LUT for drawing 8x? fonts quickly (4 pixels at a time)

/**** Pre-Defined LUTs ****/
// LUT for nibble masks
UInt32      nibbleLUT[ 16 ] = {  0x00000000, 0xFF000000, 0x00FF0000, 0xFFFF0000,
                                 0x0000FF00, 0xFF00FF00, 0x00FFFF00, 0xFFFFFF00,
                                 0x000000FF, 0xFF0000FF, 0x00FF00FF, 0xFFFF00FF,
                                 0x0000FFFF, 0xFF00FFFF, 0x00FFFFFF, 0xFFFFFFFF };
// masks for horizontal rollover of fill functions
UInt32      fillRollMask[ 4 ] = { 0x00000000, 0x000000FF, 0x0000FFFF, 0x00FFFFFF };

/******** FUNCTION DEFINITIONS ********/

/* ------------------------------------------------------------------
** Initializes the driver
*/
STATUS   Init () {
   DPMI::RMCall   rmCall;        // DPMI real-mode interrupt structure
   SVGAInfo *     svgaInfoTemp;  // -> temporary SVGAInfo block
   UInt16         svgaInfoSeg;   // segment of temp SVGAInfo block
   UInt16         svgaInfoSel;   // selector of temp SVGAInfo block
   UInt16         modeInfoSeg;   // segment of ModeInfo block
   UInt16 *       vmList;        // pointer for building video mode list
   
   // abort if the driver is already inited
   if ( inited ) return ( ERR_INITED );
   
   // set initial values
   linearFB = NULL;
   targetPage = 0;   // drawing targets page 0
   // attempt to allocate space for the SVGA info block in low memory
   // and if it failed, then return a DPMI failure
   if ( DPMI::AllocateDOSMemory( sizeof( SVGAInfo ), &svgaInfoSeg, &svgaInfoSel ) )
      return ( ERR_DPMI );
   
   // convert the real-mode segments into a protected-mode addresses
   svgaInfoTemp = ( SVGAInfo * )( svgaInfoSeg << 4 );
   // set the signature to "VBE2" to receive extra SVGA data
   strncpy( ( char * ) svgaInfoTemp -> signature, "VBE2", 4 );
   // clear the contents of the real-mode call struct first
   memset( &rmCall, 0, sizeof( rmCall ) );
   // set up the simulated real-mode interrupt
   rmCall.eax = 0x4F00;
   rmCall.es = svgaInfoSeg;
   rmCall.edi = 0;
   // perform the interrupt
   if ( DPMI::SimulateRealModeInt( 0x10, &rmCall ) )
      return ( ERR_DPMI );
   
   // copy the SVGA info block from temp low memory to local copy
   memcpy( &svgaInfo, svgaInfoTemp, sizeof( svgaInfo ) );
   // free the temp SVGA info block
   if ( DPMI::FreeDOSMemory( svgaInfoSel ) )
      return ( ERR_DPMI );
   
   // allocate space for the Mode info block in low memory
   if ( DPMI::AllocateDOSMemory( sizeof( ModeInfo ), &modeInfoSeg, &modeInfoSel ) )
      return ( ERR_DPMI );
   
   // convert the real-mode segment into a protected-mode address
   modeInfo = ( ModeInfo * )( modeInfoSeg << 4 );
   
   // get the list of supported video modes and copy it to the driver
   numVideoModes = 0;
   vmList = ( UInt16 * ) RM_TO_PM( svgaInfo.modeListPtr );
   while ( vmList[ numVideoModes ] != 0xFFFF ) {
      modeList[ numVideoModes ] = vmList[ numVideoModes ];
      numVideoModes++;
      // break if we can't copy any more
      if ( numVideoModes == MAX_NUM_MODES ) break;
   }
   
   // get the pointer to the font definition we'll be using
   memset( &rmCall, 0, sizeof( rmCall ) );
   rmCall.eax = 0x1130;
   rmCall.ebx = 0x0600;    // get the pointer for the 8x16 font
   // perform the interrupt
   if ( DPMI::SimulateRealModeInt( 0x10, &rmCall ) )
      return ( ERR_DPMI );
   // set the pointer
   romFont = ( Byte * )( ( rmCall.es << 4 ) | ( rmCall.ebp ) );
   
   // set the text color to 7 (CGA light gray)
   // (note that this also builds the fixed-width font LUT)
   SetTextColor( 7 );
   
   // init successful
   inited = true;
   // return success
   return ( OK );
}

/* ------------------------------------------------------------------
** Uninitializes / removes the driver
*/
STATUS   Uninit () {
   // abort if the driver is not yet inited
   if ( !inited ) return ( ERR_NOT_INITED );
   
   // free the mode info block
   if ( DPMI::FreeDOSMemory( modeInfoSel ) )
      return ( ERR_DPMI );
   
   // free the previous linear FB mapping if there is one
   if ( linearFB != NULL ) {
      // if it failed, then return a DPMI failure
      if ( DPMI::FreePhysicalAddress( linearFB ) )
         return ( ERR_DPMI );
   }
   
   // successfully uninited
   inited = false;
   // return success
   return ( OK );
}

/* ------------------------------------------------------------------
** Sets video mode based on requested dimensions, bit depth, etc
**
** UInt16   width    Desired screen width
** UInt16   height   Desired screen height
** UInt16   bpp      Desired bit depth
*/
// TODO: Add support for things like physical vs. logical screen sizes
STATUS   SetVideoMode ( UInt16 width, UInt16 height, UInt16 bpp ) {
   DPMI::RMCall   rmCall;        // DPMI real-mode interrupt structure
   UInt16   i;       // iterator for mode list
   UInt16   newMode; // selected video mode
   UInt32   pageSize;   // the size of a single video page in bytes
   
   // abort if the driver is not yet inited
   if ( !inited ) return ( ERR_NOT_INITED );
   
   // if the requested height is beyond the max supported value, return failure
   if ( height > MAX_VIDEO_HEIGHT ) return ( ERR_BAD_ARGUMENT );
   
   // set the new mode to a sentinel value
   newMode = 0;
   // clear the contents of the real-mode call struct first
   memset( &rmCall, 0, sizeof( rmCall ) );
   // set up for all the SVGA calls
   rmCall.es = ( UInt32 ) modeInfo >> 4;
   rmCall.edi = 0;
   
   // loop through all the valid video modes, trying to find the desired mode
   for ( i = 0; i < numVideoModes; i++ ) {
      // get the video mode's info
      rmCall.eax = 0x4F01;
      rmCall.ecx = modeList[ i ];
      // perform the interrupt
      DPMI::SimulateRealModeInt( 0x10, &rmCall );
      
      // skip processing if criteria aren't matched
      if ( modeInfo -> width != width ) continue;
      if ( modeInfo -> height != height ) continue;
      if ( modeInfo -> bitDepth != bpp ) continue;
      // also make sure all the flags we want in mode attributes are present
      // specifically, linear framebuffers, color, graphics
      if ( ! modeInfo -> modeAttr &
         ( MA_LINEARSUPPORT | MA_SUPPORTED | MA_COLOR | MA_GRAPHICS )
         ) continue;
      
      // criteria was met, break out of the loop
      newMode = modeList[ i ];
      break;
   }
   
   // if we still have the sentinel, then we couldn't find a matching mode
   if ( !newMode ) return ( ERR_NO_MODE_FOUND );
   
   // setup information for using the video mode
   // free the previous mapping if there is one
   if ( linearFB != NULL ) {
      // if it failed, then return a DPMI failure
      if ( DPMI::FreePhysicalAddress( linearFB ) )
         return ( ERR_DPMI );
   }
   // map the physical linear framebuffer
   if ( DPMI::MapPhysicalAddress( modeInfo -> linearBufferPhys, svgaInfo.totalVideoMemory << 16, &linearFB ) )
      return ( ERR_DPMI );
   
   // build the scanline offset LUT
   for ( i = 0; i < modeInfo -> height; i++ ) {
      scanOffsetLUT[ i ] = i * modeInfo -> bytesScanline;
   }
   // get the number of video pages the current mode can use
   numPages = modeInfo -> numPages + 1;
   if ( numPages > MAX_VIDEO_PAGES ) numPages = MAX_VIDEO_PAGES;
   // build the video page offset LUT
   pageSize = modeInfo -> bytesScanline * modeInfo -> height;
   for ( i = 0; i < numPages; i++ ) {
      pageOffsetLUT[ i ] = i * pageSize;
   }
   
   // set the mode
   // clear the contents of the real-mode call struct first
   memset( &rmCall, 0, sizeof( rmCall ) );
   rmCall.eax = 0x4F02;
   rmCall.ebx = newMode | 0x4000;   // set the new mode, with a linear framebuffer
   // perform the interrupt
   DPMI::SimulateRealModeInt( 0x10, &rmCall );
   
   // finally, reset target page and current page variables
   targetPage = 0;
   
   // this is how you'd plot a color 'c' at co-ordinates 'x','y', on page 'p'
   // *( ( Byte * )linearFB + pageOffsetLUT[ p ] + scanOffsetLUT[ y ] + x ) = c;
   
   // return success
   return ( OK );
}

/* ------------------------------------------------------------------
** Sets the color to use for text drawing functions
**
** Byte     newColor    Color to use
*/
STATUS   SetTextColor ( Byte newColor ) {
   UInt32      colorMask;
   UInt16      i;
   // DO NOT check if the driver is inited (for minor speedup and so it can be called in Init)
   
   // rebuild the fixed font LUT to use the current color
   colorMask = ( newColor << 24 ) | ( newColor << 16 ) | ( newColor << 8 ) | newColor;
   
   for ( i = 0; i < FONT_HEIGHT << 8; i++ ) {
      fixedFontLUT[ i << 1 ] = ( nibbleLUT[ romFont[ i ] >> 4 ] ) & colorMask;
      fixedFontLUT[ ( i << 1 ) + 1 ] = ( nibbleLUT[ romFont[ i ] & 0xF ] ) & colorMask;
   }
   
   // return success
   return ( OK );
}

/* ------------------------------------------------------------------
** Clears the page by filling it entirely with the specified color
**
** Byte     newColor    Color to fill the page with
*/
STATUS   DrawClear ( Byte newColor ) {
   UInt32   fillValue;
   UInt32 * fillBuffer;
   UInt32   fillSize;
   UInt32   i;
   
   // abort if the driver is not yet inited
   if ( !inited ) return ( ERR_NOT_INITED );
   
   // make the fill value
   fillValue = ( newColor << 24 ) | ( newColor << 16 ) | ( newColor << 8 ) | newColor;
   // fill the page
   fillBuffer = ( UInt32 * )linearFB;
   fillSize = ( modeInfo -> bytesScanline >> 2 ) * modeInfo -> height;
   for ( i = 0; i < fillSize; i++ ) {
      fillBuffer[ i ] = fillValue;
   }
   
   // return success
   return ( OK );
}

/* ------------------------------------------------------------------
** Draws a string using a fixed-width font (8x? pixels)
**
** Int16    x     x-position of the draw operation
** Int16    y     y-position of the draw operation
** char *   str   Null-terminated string to draw; supports newlines
*/
STATUS   DrawFixedString ( Int16 x, Int16 y, const char * str ) {
   UInt16      i = 0;      // index into the string
   UInt16      lineStart;  // start of the current line
   UInt16      lineEnd;    // length of the current line in chars
   UInt16      cutoffLeft; // number of characters to cut from the left of every line
   UInt16      maxLineLen; // maximum line length (in chars)
   UInt16      nextLine;   // char index for start of next line
   UInt16      fontOffset; // offset into the font table
   Int16       xOverLeft;  // x left rollover
   Int16       xOverRight; // x right rollover
   Int16       yChar;      // y-position
   Int16       xChar;      // x-position
   Int16       yCharStart; // y loop start
   Int16       yCharEnd;   // y loop stop
   UInt32 *    drawBase;   // cached base pointer for scanline
   bool        lastLine = false; // whether the drawing function is on the last line
   
   // abort if the driver is not yet inited
   if ( !inited ) return ( ERR_NOT_INITED );
   // abort if either of the starting co-ordinates are out of bounds
   if ( x >= PAGE_WIDTH ) return ( ERR_BAD_ARGUMENT );
   if ( y >= PAGE_HEIGHT ) return ( ERR_BAD_ARGUMENT );
   
   // perform calculations for horizontal trimming
   // determine the maximum line length in chars
   maxLineLen = ( PAGE_WIDTH - x + 7 ) >> 3;
   // determine how many chars to cut off from the beginning, and adjust x
   cutoffLeft = 0;
   if ( x + 8 < 0 ) {
      cutoffLeft = ( -x ) >> 3;
      x += cutoffLeft << 3;
   }
   // determine the left rollover value (constant for all lines)
   xOverLeft = -x;
   if ( xOverLeft < 0 ) xOverLeft = 0;
   
   // in this version we draw line by line instead of char by char
   while ( !lastLine ) {
      // determine start and end of the line
      lineStart = i;
      // loop through the line to find out where it ends
      while ( 1 ) {
         // check for EOL characters (newline and null-terminator)
         if ( str[ i ] == '\n' ) {
            lineEnd = i;
            nextLine = i + 1;
            // break out of this inner loop
            break;
         } else if ( str[ i ] == '\0' ) {
            lineEnd = i;
            lastLine = true;
            // break out of this inner loop
            break;
         }
         // increment to next char
         i++;
      };
      
      // adjust the lineStart and lineEnd indices to account for horizontal clipping
      if ( ( lineEnd - lineStart ) > maxLineLen ) {
         // clip line end so it fits
         lineEnd = lineStart + maxLineLen;
      }
      lineStart += cutoffLeft;
      // determine the right rollover value for this line
      xOverRight = 0;
      if ( ( ( lineEnd - lineStart ) << 3 ) + x > PAGE_WIDTH ) {
         xOverRight = 8 - ( x % 8 );
      }
      
      // check to see if this line of characters should be skipped
      // (it's too far up or too far down)
      if ( y + FONT_HEIGHT <= 0 ) {
         // don't draw this line
         
      } else if ( y >= PAGE_HEIGHT ) {
         // stop drawing entirely -- break out of the main loop
         break;
         
      } else {
         // set the line vertical start/end to account for vert clipping
         yCharStart = -y;
         if ( yCharStart < 0 ) yCharStart = 0;
         yCharEnd = PAGE_HEIGHT - y;
         if ( yCharEnd > FONT_HEIGHT ) yCharEnd = FONT_HEIGHT;
         
         // adjust lineStart if we are drawing a partial left
         if ( xOverLeft ) lineStart++;
         // adjust lineEnd if we are drawing a partial right
         if ( xOverRight ) lineEnd--;
         // go scanline by scanline, and loop through all the characters on this line
         for ( yChar = yCharStart; yChar < yCharEnd; yChar++ ) {
            // calculate the scanline base
            drawBase = ( UInt32 * )( ( UInt32 )( linearFB ) + pageOffsetLUT[ targetPage ] + scanOffsetLUT[ y + yChar ] + x );
            xChar = 0;
            
            // do a partial left draw for this line if we need to
            if ( xOverLeft ) {
               fontOffset = ( ( str[ lineStart - 1 ] << 4 ) + yChar ) << 1;
               // draw the partial upper nibble if need be
               if ( xOverLeft < 4 ) {
                  drawBase[ xChar ] = ( drawBase[ xChar ] & fillRollMask[ xOverLeft ] ) |
                     ( fixedFontLUT[ fontOffset ] & ~fillRollMask[ xOverLeft ] );
               }
               xChar++;
               xOverLeft -= 4;
               if ( xOverLeft < 4 ) {
                  // partial lower
                  drawBase[ xChar ] = ( drawBase[ xChar ] & fillRollMask[ xOverLeft ] ) |
                     ( fixedFontLUT[ fontOffset + 1 ] & ~fillRollMask[ xOverLeft ] );
               } else {
                  // full lower
                  drawBase[ xChar ] = fixedFontLUT[ fontOffset + 1 ];
               }
               // restore xOverLeft for the next scanline
               xOverLeft += 4;
               xChar++;
            }
            // do all the full draws for this line
            for ( i = lineStart; i < lineEnd; i++ ) {
               fontOffset = ( ( str[ i ] << 4 ) + yChar ) << 1;
               // upper nibble
               drawBase[ xChar ] = fixedFontLUT[ fontOffset ];
               xChar++;
               // lower nibble
               drawBase[ xChar ] = fixedFontLUT[ fontOffset + 1 ];
               xChar++;
            }
            // do a partial right draw if we need to
            if ( xOverRight ) {
               fontOffset = ( ( str[ i ] << 4 ) + yChar ) << 1;
               // draw the upper nibble as full or partial
               if ( xOverRight < 4 ) {
                  // trimmed on the right
                  drawBase[ xChar ] = ( drawBase[ xChar ] & ~fillRollMask[ xOverRight ] ) |
                     ( fixedFontLUT[ fontOffset ] & fillRollMask[ xOverRight ] ); 
               } else {
                  // full upper
                  drawBase[ xChar ] = fixedFontLUT[ fontOffset ];
               }
               xChar++;
               xOverRight -= 4;
               // draw the partial lower nibble
               if ( xOverRight > 0 ) {
                  // partial lower
                  drawBase[ xChar ] = ( drawBase[ xChar ] & ~fillRollMask[ xOverRight ] ) |
                     ( fixedFontLUT[ fontOffset + 1 ] & fillRollMask[ xOverRight ] );
               }
               xChar++;
               // restore xOverRight for the next scanline
               xOverRight += 4;
            }
            
         }  // END y-looping FOR
         
      }  // END y-trimming IF

      // move the index to the start of the next line
      i = nextLine;
      // now that the line is over, increment y
      y += FONT_HEIGHT;
      
   }; // end main loop
   
   // return success
   return ( OK );
}

/* ------------------------------------------------------------------
** Exposes a pointer to the linear framebuffer
**
** void **  pLFB     -> pointer to receive linear framebuffer
*/
STATUS   GetLinearFB ( void ** pLFB ) {
   // abort if the driver is not yet inited
   if ( !inited ) return ( ERR_NOT_INITED );
   
   // copy the LFB pointer
   *pLFB = linearFB;
      
   // return success
   return ( OK );
}

}; // end namespace
