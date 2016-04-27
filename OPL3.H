// OPL3.H
//
// OPL3 Driver include

#if !defined( OPL3_H )
#define OPL3_H

#include "globals.h"    // for type defs

// use the OPL3 namespace
namespace OPL3 {

/******** MIDI player status messages ********/
typedef enum {
   OK = 0,           // function executed successfully
   ERR_FILE_OPEN,    // file could not be opened (file not found, etc)
   ERR_FILE_BAD,     // file failed format checks (magicnum, etc)
   ERR_NOT_INITED,   // the OPL3 driver hasn't been initialized yet
} STATUS;

/******** OPL3 driver functions ********/

// initializes the OPL3 driver (must be called before any other functions)
STATUS   Init ();
// loads an OPL3 patch bank from a file
STATUS   LoadPatchBank ( char * fileName, bool overlay );
// sends a Note-Off command to the driver
void     NoteOff ( Byte chan, Byte key, Byte velocity );
// sends a Note-On command to the driver
void     NoteOn ( Byte chan, Byte key, Byte velocity );
// sends an Aftertouch Key command to the driver
void     AftertouchKey ( Byte chan, Byte key, Byte pressure );
// sends an Aftertouch Channel command to the driver
void     AftertouchChan ( Byte chan, Byte pressure );
// sends a Controller Change command to the driver
void     ControllerChange ( Byte chan, Byte number, Byte value );
// sends a Program Change command to the driver
void     ProgramChange ( Byte chan, Byte program );
// sends a Pitch Bend command to the driver
void     PitchBend ( Byte chan, Byte lsb, Byte msb );
// forcibly turns off all notes
void     AllNotesOff ();
// resets a channel's controllers
void     ResetChanControllers ( Byte chan );

};    // end OPL3 namespace

#endif