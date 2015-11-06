// MIDI.H
//
// MIDI Player include

#if !defined( __MIDI_H )
#define __MIDI_H

#include "globals.h"    // for type defs

/******** MIDI player status messages ********/
#define  MIDI_OK                  0    // function executed successfully
#define  MIDI_ERR_FILE_SIZE       1    // file load failed because the file was too large
#define  MIDI_ERR_FILE_OPEN       2    // file could not be opened (file not found, etc)
#define  MIDI_ERR_FILE_TRACKS     3    // file has too many (or 0) tracks
#define  MIDI_ERR_FILE_TIMING     4    // file has the wrong timing format (SMPTE)
#define  MIDI_ERR_FILE_FORMAT     5    // file has the wrong track format (serial instead of simultaneous)
#define  MIDI_ERR_FILE_BAD        6    // file failed format checks (magicnum, etc)
#define  MIDI_ERR_MALLOC          7    // failure on memory allocation
#define  MIDI_ERR_PLAYING         8    // function can't complete because file is in the middle of playback
#define  MIDI_ERR_NOT_LOADED      9    // a file is not loaded (can't be played, etc)
#define  MIDI_ERR_NOT_INITED     10    // the MIDI player has not been initialized yet
#define  MIDI_ERR_GENERIC        11    // generic error
#define  MIDI_ERR_NOT_PLAYING    12    // function can't complete because file is not playing
#define  MIDI_ERR_BAD_ARGUMENT   13    // an argument supplied to the function is bad or out-of-bounds

/******** MIDI player functions ********/

// initializes the MIDI player (must be called before any other functions)
int      MIDI_Init ();
// loads a specified file for play and returns an error or OK code
int      MIDI_LoadFile ( char * fileName );
// rewinds a loaded MIDI file to the beginning and restores the player's state
int      MIDI_Rewind ();
// begins playback of the loaded MIDI (unpausing if it was paused)
int      MIDI_Play ();
// stops playback of the loaded MIDI and rewinds to the beginning
int      MIDI_Stop ();
// pauses playback of the loaded MIDI
int      MIDI_Pause ();
// shuts down the player and does cleanup (i.e. restoring PIT behavior)
int      MIDI_ShutDown ();
// returns whether the MIDI player is currently playing
BOOL     MIDI_IsPlaying ();
// this function should be called periodically (ideally many times a frame) to service
// the MIDI driver and handle new event ticks
void     MIDI_Update ();
// sets the time, in seconds, at which the MIDI should be prematurely stopped
int      MIDI_SetPlayTime ( WORD seconds );

#endif