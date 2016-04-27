// VISUAL.H
//
// Visualizer include

#if !defined( VISUAL_H )
#define VISUAL_H

#include "globals.h"

// use the Visual namespace
namespace Visual {

/******** Visualizer function returns ********/
typedef enum {
   OK = 0,              // function executed successfully
   ERR_GENERIC,         // generic error occured
} STATUS;

/******** Visualizer functions ********/
// activates the visualizer in text or SVGA mode
STATUS   Enable ( bool mode );
// deactives the visualizer
STATUS   Disable ();
// updates the visualizer
STATUS   Update ();
// sets the file name to display
STATUS   SetFileName ( char * fName );

// sends a Note-off command to the visualizer
STATUS   NoteOff ( Byte chan, Byte key, Byte velocity );
// sends a Note-On command to the visualizer
STATUS   NoteOn ( Byte chan, Byte key, Byte velocity );
// sends an Aftertouch Key command to the visualizer
STATUS   AftertouchKey ( Byte chan, Byte key, Byte pressure );
// sends an Aftertouch Channel command to the visualizer
STATUS   AftertouchChan ( Byte chan, Byte pressure );
// sends a Controller Change command to the visualizer
STATUS   ControllerChange ( Byte chan, Byte number, Byte value );
// sends a Program Change command to the visualizer
STATUS   ProgramChange ( Byte chan, Byte program );
// sends a Pitch Bend command to the visualizer
STATUS   PitchBend ( Byte chan, Byte lsb, Byte msb );
// forcibly turns off all notes
STATUS   AllNotesOff ();
// resets a channel's controllers
STATUS   ResetChanControllers ( Byte chan );

};    // end Visual namespace

#endif
