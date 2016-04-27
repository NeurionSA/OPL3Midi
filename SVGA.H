// VIDEO.H
//
// Video Driver include

#if !defined( SVGA_H )
#define SVGA_H

#include "globals.h"    // for type defs

// use the SVGA namespace
namespace SVGA {

/******** Video Driver function returns ********/
typedef enum {
   OK = 0,              // function executed successfully
   ERR_INITED,          // driver is already inited
   ERR_NOT_INITED,      // driver is not yet inited
   ERR_DPMI,            // error with DPMI functions
   ERR_BAD_ARGUMENT,    // one of the function's arguments is invalid
   ERR_NO_MODE_FOUND,   // video mode matching the desired criteria couldn't be found
} STATUS;

/******** Video Driver functions ********/
// Initializes the driver
STATUS   Init ();
// Uninitializes / removes the driver
STATUS   Uninit ();
// Sets video mode based on requested dimensions, bit depth, etc
STATUS   SetVideoMode ( UInt16 width, UInt16 height, UInt16 bpp );
// Sets the color to use for text drawing functions
STATUS   SetTextColor ( Byte newColor );
// Clears the page by filling it entirely with the specified color
STATUS   DrawClear ( Byte newColor );
// Draws a string using a fixed-width font
STATUS   DrawFixedString ( Int16 x, Int16 y, const char * str );
// Exposes a pointer to the linear framebuffer
STATUS   GetLinearFB ( void ** pLFB );

}; // end namespace

#endif
