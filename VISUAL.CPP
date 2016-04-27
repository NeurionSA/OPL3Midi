/********************************************************************
**
** VISUAL.CPP
**
** Piano roll visualizer
**
********************************************************************/

#include <stdio.h>      // for standard I/O and string printing
#include <string.h>     // for strncpy()
#include <graph.h>      // for screen-setting functions
#include "visual.h"
#include "globals.h"
#include "timer.h"
#include "svga.h"

// use the Visual namespace
namespace Visual {

/******** ENUMS ********/

// enum for CGA colors
enum COLORS {
   BLACK, BLUE, GREEN, CYAN,
   RED, MAGENTA, BROWN, LIGHTGRAY,
   DARKGRAY, LIGHTBLUE, LIGHTGREEN, LIGHTCYAN,
   LIGHTRED, LIGHTMAGENTA, YELLOW, WHITE
};

/******** CONSTANTS ********/
#define  MAX_NOTES      64    // maximum number of notes to track

/******** MACROS ********/
// makes a 32-bit value where each byte is equal to X
#define  MAKE_COLOR_32(x)  ( ( x << 24 ) | ( x << 16 ) | ( x << 8 ) | x )

// text-mode constants
#define  BANNER_SIZE    3     // number of text rows the banner occupies
#define  NOTE_SHIFT     24    // number of notes to shift to the left to line up the visualizer (text mode only)

// svga-mode constants
#define  ROLL_HEIGHT    400   // height in pixels of the piano roll region
#define  DOT_INTERVAL   4     // modulus value taken for drawing dotted lines

/******** STRUCTS ********/
// struct for tracking the states of notes
typedef struct NoteState {
   bool     active;     // if the note is playing or not
   bool     wasSeen;    // if the note has been drawn for at least 1 frame
   Byte     chan;       // the channel the note is playing on
   Byte     key;        // the note's key
   Byte     velocity;   // the note's velocity
   UInt32   startTick;  // tick when the note started playing
} NoteState;

// union for quickly manipulating the screen in text mode
typedef union {
   UInt16   value;      // raw value of the character
   struct {
      Byte  code;
      Byte  fore : 4;
      Byte  back : 4;
   } attr;              // character attributes
} ScreenChar;

// struct for storing the key graphic definitions
typedef struct {
   Byte     nWidth;     // width in dwords of the key drawn normally
   Byte     nHeight;    // height in pixels of the key drawn normally
   Byte     cWidth;     // width in dwords of the key drawn colored
   Byte     cHeight;    // height in pixels of the key drawn colored
   Byte     offset;     // horizontal offset of the key in dwords
   Byte     nMask[ 6 ];   // mask from nibble LUT for drawing normally, for top half and bottom half of key
   Byte     cMask[ 6 ];   // mask from nibble LUT for drawing colored, for top half and bottom half of key
} PianoKey;

// working row information struct
typedef struct {
   Byte     channel;    // channel number + 1 (or 0 for no note)
   UInt32   startTick;  // tick when the note started playing
} WorkingRow;

/******** FUNCTION DECLARATIONS ********/
// (for functions not declared in the header)
// enables text mode
STATUS   enableText ();
// enables SVGA mode
STATUS   enableSVGA ();
// disables text mode
STATUS   disableText ();
// disables SVGA mode
STATUS   disableSVGA ();
// updates text mode
STATUS   updateText ();
// updates SVGA mode
STATUS   updateSVGA ();
// draws a piano key
void     drawPianoKey ( Byte key, Byte channel );

/******** VARIABLES ********/
UInt16      hTimer;     // handle of the timer we use
NoteState   notes[ MAX_NOTES ];  // all the notes being tracked
char        fileName[ 80 ];   // the name of the file being played
bool        enabled = false;     // if the visualizer is enabled
bool        useSVGA;    // whether to use SVGA or TEXT modes
UInt16      updateHz;   // update rate in Hz
UInt32      elapsedTicks;  // elapsed time in update ticks

// variables related to TEXT mode
ScreenChar  *charMap = (ScreenChar *)(0xB8000);
ScreenChar  charBlank;     // blank note display element
ScreenChar  charChannel[ 16 ];   // elements for each channel

// variables related to SVGA mode
void *   linearFB;      // -> SVGA's linear framebuffer
UInt32   svBlank;       // blank note display element
UInt32   svDotted;      // dotted-line display element
UInt32   svChannel[ 16 ];  // elements/colors for each channel
WorkingRow  workingRows[ 256 ];  // flip-flopping record of what colors are being displayed on the new row AND the previous
bool     workingLower;  // whether the lower half of the working rows is the current row
// circular array that stores an encoded record of the difference between each row,
// which is used to speed up the process of drawing the update
Byte     rowRLE[ ROLL_HEIGHT << 7 ];
Int32    diffBase;      // which row in the circular row-differences array is the first
// LUT for nibble masks, used in drawing Piano keys
UInt32   nibbleLUT[ 16 ] = {
   0x00000000, 0xFF000000, 0x00FF0000, 0xFFFF0000,
   0x0000FF00, 0xFF00FF00, 0x00FFFF00, 0xFFFFFF00,
   0x000000FF, 0xFF0000FF, 0x00FF00FF, 0xFFFF00FF,
   0x0000FFFF, 0xFF00FFFF, 0x00FFFFFF, 0xFFFFFFFF };
PianoKey    pianoKey[ 12 ];   // graphical information for drawing an octave's worth of piano keys
UInt32      pianoBmp[ 192 ];  // bitmap containing the piano roll graphic

/******** FUNCTION DEFINITIONS ********/

// enables text mode
STATUS   enableText () {
   UInt16   i;    // for-loop iterator
   
   // initialize character-mode data
   // set up the blank note display element
   charBlank.attr.code = 250;
   charBlank.attr.fore = DARKGRAY;
   charBlank.attr.back = BLACK;
   
   charChannel[ 0 ].attr.code = 177;
   charChannel[ 0 ].attr.fore = YELLOW;
   charChannel[ 0 ].attr.back = RED;
   charChannel[ 1 ].attr.code = 219;
   charChannel[ 1 ].attr.fore = BLUE;
   charChannel[ 2 ].attr.code = 219;
   charChannel[ 2 ].attr.fore = GREEN;
   charChannel[ 3 ].attr.code = 219;
   charChannel[ 3 ].attr.fore = CYAN;
   charChannel[ 4 ].attr.code = 219;
   charChannel[ 4 ].attr.fore = RED;
   charChannel[ 5 ].attr.code = 219;
   charChannel[ 5 ].attr.fore = MAGENTA;
   charChannel[ 6 ].attr.code = 219;
   charChannel[ 6 ].attr.fore = BROWN;
   charChannel[ 7 ].attr.code = 219;
   charChannel[ 7 ].attr.fore = DARKGRAY;
   charChannel[ 8 ].attr.code = 219;
   charChannel[ 8 ].attr.fore = LIGHTGRAY;
   charChannel[ 9 ].attr.code = 219;
   charChannel[ 9 ].attr.fore = WHITE;
   charChannel[ 10 ].attr.code = 219;
   charChannel[ 10 ].attr.fore = LIGHTBLUE;
   charChannel[ 11 ].attr.code = 219;
   charChannel[ 11 ].attr.fore = LIGHTGREEN;
   charChannel[ 12 ].attr.code = 219;
   charChannel[ 12 ].attr.fore = LIGHTCYAN;
   charChannel[ 13 ].attr.code = 219;
   charChannel[ 13 ].attr.fore = LIGHTRED;
   charChannel[ 14 ].attr.code = 219;
   charChannel[ 14 ].attr.fore = LIGHTMAGENTA;
   charChannel[ 15 ].attr.code = 219;
   charChannel[ 15 ].attr.fore = YELLOW;
   
   // set up the screen (80 x 50)
   _clearscreen( _GCLEARSCREEN );
   _setvideomode( _TEXTC80 );
   _settextrows( 50 );
   // hide the text cursor
   _settextcursor( 0x2000 );
   
   // perform the initial draw
   printf( "File: %s\n", fileName );
   printf( "Time: \n" );
   // draw the blank note display
   for ( i = BANNER_SIZE * 80; i < 4000; i++ ) {
      charMap[ i ] = charBlank;
   }
   
   // return success
   return ( OK );
}

// enables SVGA mode
STATUS   enableSVGA () {
   SVGA::STATUS   status;  // returns from SVGA functions
   UInt32   i, x, y;
   UInt32 * dwordBuffer;
   char     textBuffer[ 80 ];    // text buffer for generating displayed info
   bool     dotted;
   FILE *   hFile;      // handle for the piano roll graphics file
   
   // initialize the SVGA driver
   status = SVGA::Init();
   if ( status != SVGA::OK ) return ( ERR_GENERIC );
   // set the video mode to 640 x 480 x 8
   status = SVGA::SetVideoMode( 640, 480, 8 );
   if ( status != SVGA::OK ) return ( ERR_GENERIC );
   // get the linear FB
   status = SVGA::GetLinearFB ( &linearFB );
   if ( status != SVGA::OK ) return ( ERR_GENERIC );
   dwordBuffer = ( UInt32 * ) linearFB;
   
   // load the data from the piano roll graphics file
   // TODO: Handle errors with the file
   hFile = fopen( "KEYGRAPH.DAT", "rb" );
   // read in the bitmap
   fread( pianoBmp, 192, 4, hFile );
   // read in the 12 key structues
   fread( pianoKey, sizeof( PianoKey ), 12, hFile );
   // close the file
   fclose( hFile );
   
   // generate the display elements
   svBlank = 0x00000000;
   svDotted = 0x00000013;     // dark gray (darker than CGA)
   
   // define the channel display elements/colors
   // rainbow is spread so adjacent channels aren't too close in color
   svChannel[ 0x0 ] = MAKE_COLOR_32( 0x20 );    // blue
   svChannel[ 0x1 ] = MAKE_COLOR_32( 0x24 );    // magenta
   svChannel[ 0x2 ] = MAKE_COLOR_32( 0x2A );    // orange
   svChannel[ 0x3 ] = MAKE_COLOR_32( 0x2C );    // yellow
   svChannel[ 0x4 ] = MAKE_COLOR_32( 0x2F );    // green
   svChannel[ 0x5 ] = MAKE_COLOR_32( 0x34 );    // cyan
   svChannel[ 0x6 ] = MAKE_COLOR_32( 0x36 );    // mid blue
   svChannel[ 0x7 ] = MAKE_COLOR_32( 0x40 );    // pink
   svChannel[ 0x8 ] = MAKE_COLOR_32( 0x19 );    // mid gray
   svChannel[ 0x9 ] = MAKE_COLOR_32( 0x1C );    // light gray (perc)
   svChannel[ 0xA ] = MAKE_COLOR_32( 0x22 );    // purple
   svChannel[ 0xB ] = MAKE_COLOR_32( 0x28 );    // red
   svChannel[ 0xC ] = MAKE_COLOR_32( 0x2B );    // yellow-orange
   svChannel[ 0xD ] = MAKE_COLOR_32( 0x2D );    // yellow-green
   svChannel[ 0xE ] = MAKE_COLOR_32( 0x35 );    // light blue
   svChannel[ 0xF ] = MAKE_COLOR_32( 0x38 );    // lighter blue
   
   // clear the working row array
   for ( i = 0; i < 256; i++ ) {
      workingRows[ i ].channel = 0;
      workingRows[ i ].startTick = 0;
   }
   workingLower = true;
   // clear the row difference array (by filling it with the END OF ROW code)
   for ( i = 0; i < ( ROLL_HEIGHT << 7 ); i++ ) {
      rowRLE[ i ] = 0xFF;
   }
   diffBase = 0;
   
   // draw the filename
   sprintf( textBuffer, "File: %s", fileName );
   SVGA::DrawFixedString( 0, 0, textBuffer );
   // draw the time header
   sprintf( textBuffer, "Time: " );
   SVGA::DrawFixedString( 0, 16, textBuffer );
   
   // draw all the piano keys as unpressed
   for ( i = 0; i < 128; i++ ) {
      drawPianoKey( i, 0 );
   }
   
   // draw the roll region (64 pixels from the left)
   // note that we draw an extra dotted area to the right of the roll
   i = 160 * ( 480 - ROLL_HEIGHT ) + 16;
   for ( y = 0; y < ROLL_HEIGHT; y++ ) {
      // determine if this line is dotted
      dotted = ( y % DOT_INTERVAL ) == 0;
      for ( x = 0; x <= 128; x++ ) {
         // if we're on an octave boundary, force the dot
         if ( ( dotted ) || ( x % 12 == 0 ) ) {
            dwordBuffer[ i + x ] = svDotted;
         } else {
            dwordBuffer[ i + x ] = svBlank;
         }  
      }
      // increment the offset to the next line
      i += 160;
   }
   
   // return success
   return ( OK );
}

// disables text mode
STATUS   disableText () {
   // reset the screen
	_clearscreen( _GCLEARSCREEN );
	_setvideomode( _TEXTC80 );
	_settextrows( 25 );
   
   // return success
   return ( OK );
}

// disables SVGA mode
STATUS   disableSVGA () {
   // uninit the SVGA driver
   SVGA::Uninit();
   
   // reset the screen
	_clearscreen( _GCLEARSCREEN );
	_setvideomode( _TEXTC80 );
	_settextrows( 25 );
   
   // return success
   return ( OK );
}

// updates text mode
STATUS   updateText () {
   UInt16   i;
   UInt16   endPoint;
   UInt16   startPoint;
   Int16    offset;
   Byte     row[ 80 ];
   UInt16   tSecond, tMinute;
   
   // update the elapsed time
   _settextposition( 2, 7 );
   // calculate the minutes and seconds
   tMinute = elapsedTicks / ( updateHz * 60 );
   tSecond = ( elapsedTicks % ( updateHz * 60 ) ) / updateHz;
   printf( "%02d:%02d\n", tMinute, tSecond );
   
   // clear the row-change array
   for ( i = 0; i < 80; i++ ) {
      row[ i ] = 0;
   }
   // update the piano roll by first copying everything down a row
   endPoint = ( BANNER_SIZE + 1 ) * 80;
   for ( i = 3999; i >= endPoint; i-- ) {
      charMap[ i ] = charMap[ i - 80 ];
   }
   
   // draw the new state by clearing the line with blanks
   startPoint = ( BANNER_SIZE * 80 );
   endPoint = startPoint + 80;
   for ( i = startPoint; i < endPoint; i++ ) {
      charMap[ i ] = charBlank;
   }
   
   // iterate through all the active notes and see if they should be plotted
   // (higher channel values trump lower ones)
   for ( i = 0; i < MAX_NOTES; i++ ) {
      // if a note is active OR if a note is inactive but hasn't been drawn yet
      if ( ( notes[ i ].active ) || ( !notes[ i ].active && !notes[ i ].wasSeen ) ) {
         // set the wasSeen flag
         notes[ i ].wasSeen = true;
         offset = notes[ i ].key - NOTE_SHIFT;
         // ignore characters out of bounds
         if ( ( offset < 0 ) || ( offset >= 80 ) ) continue;
         // compare the channels to see if we should overwrite
         if ( row[ offset ] < ( notes[ i ].chan + 1 ) ) {
            // overwrite
            row[ offset ] = notes[ i ].chan + 1;
            charMap[ startPoint + offset ] = charChannel[ notes[ i ].chan ];
         }
      }
   }
   
   // return success
   return ( OK );
}

// updates SVGA mode
STATUS   updateSVGA () {
   UInt32   i, x, y;       // loop iterators
   UInt16   newBase = 0;      // bases for the working row array
   UInt16   oldBase = 128;
   UInt16   skipCount = 0;    // skip count for RLE
   UInt32   rleBase, rleIndex;   // base and indexes for RLE loops
   UInt32   drawOffset;       // LFB draw offset
   UInt32 * dwordBuffer = ( UInt32 * ) linearFB;
   char     textBuffer[ 80 ]; // buffer for building strings
   UInt16   tMinute, tSecond; // elapsed time
   
   // calculate and draw the elapsed time
   tMinute = elapsedTicks / ( updateHz * 60 );
   tSecond = ( elapsedTicks % ( updateHz * 60 ) ) / updateHz;
   sprintf( textBuffer, "%02d:%02d", tMinute, tSecond );
   SVGA::DrawFixedString( 48, 16, textBuffer );
   
   // ensure the working row offsets are correct
   if ( !workingLower ) {
      newBase = 128;
      oldBase = 0;
   }
   // clear the current row
   for ( i = 0; i < 128; i++ ) {
      workingRows[ newBase + i ].channel = 0;
      workingRows[ newBase + i ].startTick = 0;
   }
   // iterate through the notes to generate the new row
   for ( i = 0; i < MAX_NOTES; i++ ) {
      // if a note is active OR if a note is inactive but hasn't been drawn yet
      if ( ( notes[ i ].active ) || ( !notes[ i ].active && !notes[ i ].wasSeen ) ) {
         // set the wasSeen flag
         notes[ i ].wasSeen = true;
         // compare the note's age to see if we should overwrite
         if ( workingRows[ newBase + notes[ i ].key ].startTick < ( notes[ i ].startTick ) ) {
            // overwrite
            workingRows[ newBase + notes[ i ].key ].channel = notes[ i ].chan + 1;
            workingRows[ newBase + notes[ i ].key ].startTick = notes[ i ].startTick;
         }
      }
   }
   
   // generate the encoding for the new row by comparing the new row to the previous
   rleIndex = diffBase << 7;
   for ( i = 0; i < 128; i++ ) {
      // check if the old and new match
      if ( workingRows[ newBase + i ].channel == workingRows[ oldBase + i ].channel ) {
         // they match, so increment the skip counter
         skipCount++;
         
      } else {
         // they do not match, so if the skip counter is non-zero,
         // write the number of notes to skip
         if ( skipCount ) {
            rowRLE[ rleIndex ] = skipCount;
            rleIndex++;
            skipCount = 0;
         }
         
         // if the new note is clear, write the CLEAR command
         if ( workingRows[ newBase + i ].channel == 0 ) {
            rowRLE[ rleIndex ] = 0x00;
            rleIndex++;
            
         } else {
            // the color has changed, so write the new color (with uppermost bit set)
            rowRLE[ rleIndex ] = ( workingRows[ newBase + i ].channel - 1 ) + 0x80;
            rleIndex++;
            
         }
         // update the piano key for this column
         drawPianoKey( i, workingRows[ newBase + i ].channel );
      }
   }
   // finally, write the END OF ROW command
   rowRLE[ rleIndex ] = 0xFF;
   
   // now we update the piano roll visual by decoding and acting on the RLE
   rleBase = diffBase << 7;
   drawOffset = ( 480 - ROLL_HEIGHT ) * 160 + 16;
   
   for ( y = 0; y < ROLL_HEIGHT; y++ ) {
      x = 0;
      // iterate through the current row of the RLE
      for ( rleIndex = rleBase; rleIndex < rleBase + 128; rleIndex++ ) {
         // operate on the code
         if ( rowRLE[ rleIndex ] == 0x00 ) {
            // CLEAR NOTE command
            // draw a blank or dot, depending on y position
            if ( ( y % DOT_INTERVAL == 0 ) || ( x % 12 == 0 ) ) {
               // dotted
               dwordBuffer[ drawOffset + x ] = svDotted;
            } else {
               // blank
               dwordBuffer[ drawOffset + x ] = svBlank;
            }
            x++;
            
         } else if ( rowRLE[ rleIndex ] < 0x80 ) {
            // SKIP command
            x += rowRLE[ rleIndex ];
            
         } else if ( rowRLE[ rleIndex ] == 0xFF ) {
            // END OF ROW command
            // break out of the rleIndex loop
            break;
            
         } else {
            // NEW COLOR command
            // set the new color
            dwordBuffer[ drawOffset + x ] = svChannel[ rowRLE[ rleIndex ] & 0x7F ];
            x++;
            
         }
         
      }  // END rleIndex loop
      
      // increment (and wrap if necessary) the rle index
      rleBase += 128;
      if ( rleBase == ( ROLL_HEIGHT << 7 ) ) rleBase = 0;
      // increment the draw offset to the next row
      drawOffset += 160;
      
   }  // END y LOOP
   
   // toggle the working row base
   workingLower = !workingLower;
   // decrement (and wrap if necessary) the diff base
   diffBase--;
   if ( diffBase < 0 ) diffBase += ROLL_HEIGHT;
   
   // return success
   return ( OK );
}

// draws a piano key
// key = piano key to draw
// channel = channel (+ 1) the key should hightlight, 0 = not pressed
void     drawPianoKey ( Byte key, Byte channel ) {
   UInt32 * dwordBuffer = ( UInt32 * ) linearFB;
   UInt32   offset, octave, semi, x, y, v;
   
   // determine the base drawing offset
   octave = key / 12;
   semi = key % 12;
   offset = ( 464 - ROLL_HEIGHT ) * 160 + 16 + octave * 12 + pianoKey[ semi ].offset;
   
   // draw the key clear or colored
   if ( channel == 0 ) {
      // key is clear
      for ( y = 0; y < pianoKey[ semi ].nHeight; y++ ) {
         v = y >> 3;
         for ( x = 0; x < pianoKey[ semi ].nWidth; x++ ) {
            // blank it
            dwordBuffer[ offset + x ] &= ~nibbleLUT[ pianoKey[ semi ].nMask[ x + v * 3 ] ];
            // OR in the masked graphic
            dwordBuffer[ offset + x ] |= nibbleLUT[ pianoKey[ semi ].nMask[ x + v * 3 ] ] & pianoBmp[ y * 12 + x + pianoKey[ semi ].offset ];
         }
         offset += 160;
      }
   } else {
      // key is colored
      for ( y = 0; y < pianoKey[ semi ].cHeight; y++ ) {
         v = y >> 3;
         for ( x = 0; x < pianoKey[ semi ].cWidth; x++ ) {
            // blank it
            dwordBuffer[ offset + x ] &= ~nibbleLUT[ pianoKey[ semi ].cMask[ x + v * 3 ] ];
            // OR in the masked graphic
            dwordBuffer[ offset + x ] |= nibbleLUT[ pianoKey[ semi ].cMask[ x + v * 3 ] ] & svChannel[ channel - 1 ];
         }
         offset += 160;
      }
   }
}

/* ------------------------------------------------------------------
** Activates the visualizer
**
** bool     mode     Whether to use SVGA (true) or TEXT modes (false)
*/
STATUS   Enable ( bool mode ) {
   UInt16   i;    // for-loop iterator
   STATUS   status;   // status of the mode-specific enable function
   
   // take the desired mode into consideration
   useSVGA = mode;
   // 0 time has elapsed
   elapsedTicks = 0;
   
   // initialize the note structures
   for ( i = 0; i < MAX_NOTES; i++ ) {
      notes[ i ].active = false;    // all notes are inactive to start
      notes[ i ].wasSeen = true;    // all notes start off having been seen
   }
   
   // call the mode-specific init functions and
   // create and start the timer for updating the visualizer
   // the rate will depend on what mode we use
   if ( useSVGA ) {
      status = enableSVGA();
      if ( status != OK ) return ( status );
      // (a rate of 19886 = ~60 Hz)
      Timer::CreateTimer( 19886, &hTimer );
      updateHz = 60;
   } else {
      status = enableText();
      if ( status != OK ) return ( status );
      // (a rate of 39772 = ~30 Hz)
      Timer::CreateTimer( 39772, &hTimer );
      updateHz = 30;
   }
   Timer::StartTimer( hTimer );
   enabled = true;
   
   // return success
   return ( OK );
}

/* ------------------------------------------------------------------
** Deactives the visualizer
*/
STATUS   Disable () {
   STATUS   status;  // status of the mode=specific disable function
   // free the timer we used
   Timer::FreeTimer( hTimer );
   
   enabled = false;
   
   if ( useSVGA ) {
      status = disableSVGA();
   } else {
      status = disableText();
   }
   
   // return status
   return ( status );
}

/* ------------------------------------------------------------------
** Updates the visualizer
*/
STATUS   Update () {
   UInt32   ticks = 0;
   STATUS   status;  // status of the mode=specific update function
   
   // check to see if a tick has occured, and if it has then we update
   Timer::GetTimerTicks( hTimer, &ticks );
   // if no tick, get outta here
   if ( ticks == 0 ) return ( OK );
   
   // increment the elapsed time by the number of ticks
   elapsedTicks += ticks;
   
   // call the appropriate update function and return its status
   if ( useSVGA ) {
      status = updateSVGA();
   } else {
      status = updateText();
   }
   
   // return status
   return ( status );
}

/* ------------------------------------------------------------------
** Sets the file name to display
**
** char *   fName    Name of the file to display
*/ 
STATUS   SetFileName ( char * fName ) {
   // copy up to 80 characters
   strncpy( fileName, fName, 80 );
   fileName[ 79 ] = 0;
   
   // return success
   return ( OK );
}

// sends a Note-off command to the visualizer
STATUS   NoteOff ( Byte chan, Byte key, Byte velocity ) {
   UInt16   i;
   
   // try to find the matching note to turn it off
   for ( i = 0; i < MAX_NOTES; i++ ) {
      if ( notes[ i ].active ) {
         if ( ( notes[ i ].chan == chan ) && ( notes[ i ].key == key ) ) {
            notes[ i ].active = false;
            // DO NOT change the wasSeen flag here
            // that will be changed in the drawing functions!
            break;
         }
      }
   }
   // return success
   return ( OK );
};

// sends a Note-On command to the visualizer
STATUS   NoteOn ( Byte chan, Byte key, Byte velocity ) {
   UInt16   i;
   
   // if velocity is 0, treat as a NoteOff
   if ( velocity == 0 ) {
      NoteOff( chan, key, 0x40 );
      return ( OK );
   }
   
   // try to find a free note
   for ( i = 0; i < MAX_NOTES; i++ ) {
      if ( !notes[ i ].active ) {
         notes[ i ].active = true;
         notes[ i ].wasSeen = false;   // the note is new and hasn't been drawn yet
         notes[ i ].chan = chan;
         notes[ i ].key = key;
         notes[ i ].velocity = velocity;
         notes[ i ].startTick = elapsedTicks;
         break;
      }
   }
   
   // return success
   return ( OK );
}

// sends an Aftertouch Key command to the visualizer
STATUS   AftertouchKey ( Byte chan, Byte key, Byte pressure ) {
   // we don't care about these, so return success
   return ( OK );
}

// sends an Aftertouch Channel command to the visualizer
STATUS   AftertouchChan ( Byte chan, Byte pressure ) {
   // we don't care about these, so return success
   return ( OK );
}

// sends a Controller Change command to the visualizer
STATUS   ControllerChange ( Byte chan, Byte number, Byte value ) {
   // we don't care about these, so return success
   return ( OK );
}

// sends a Program Change command to the visualizer
STATUS   ProgramChange ( Byte chan, Byte program ) {
   // we don't care about these, so return success
   return ( OK );
}
// sends a Pitch Bend command to the visualizer
STATUS   PitchBend ( Byte chan, Byte lsb, Byte msb ) {
   // we don't care about these, so return success
   return ( OK );
}
// forcibly turns off all notes
STATUS   AllNotesOff () {
   // return success
   return ( OK );
}

// resets a channel's controllers
STATUS   ResetChanControllers ( Byte chan ) {
   // we don't care about these, so return success
   return ( OK );
}

};    // end Visual namespace