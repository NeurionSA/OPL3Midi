// OPL3.H
//
// OPL3 Driver include

#if !defined( _OPL3_H )
#define __OPL3_H

#include "globals.h"    // for type defs

/******** MIDI player status messages ********/
#define  OPL3_OK                  0    // function executed successfully
#define  OPL3_ERR_FILE_OPEN       1    // file could not be opened (file not found, etc)
#define  OPL3_ERR_FILE_BAD        2    // file failed format checks (magicnum, etc)

/******** OPL3 driver functions ********/

// initializes the OPL3 driver (must be called before any other functions)
int      OPL3_Init ();
// loads an OPL3 patch bank from a file
int      OPL3_LoadPatchBank ( char * fileName, BOOL overlay );
// sends a Note-Off command to the driver
void     OPL3_NoteOff ( BYTE chan, BYTE key, BYTE velocity );
// sends a Note-On command to the driver
void     OPL3_NoteOn ( BYTE chan, BYTE key, BYTE velocity );
// sends an Aftertouch Key command to the driver
void     OPL3_AftertouchKey ( BYTE chan, BYTE key, BYTE pressure );
// sends an Aftertouch Channel command to the driver
void     OPL3_AftertouchChan ( BYTE chan, BYTE pressure );
// sends a Controller Change command to the driver
void     OPL3_ControllerChange ( BYTE chan, BYTE number, BYTE value );
// sends a Program Change command to the driver
void     OPL3_ProgramChange ( BYTE chan, BYTE program );
// sends a Pitch Bend command to the driver
void     OPL3_PitchBend ( BYTE chan, BYTE lsb, BYTE msb );
// forcibly turns off all notes
void     OPL3_AllNotesOff ();
// resets a channel's controllers
void     OPL3_ResetChanControllers ( BYTE chan );


#endif