// MIDI.H
//
// MIDI Player include

#if !defined( MIDI_H )
#define MIDI_H

#include "globals.h"    // for type defs

// use the MIDI namespace
namespace MIDI {

/******** MIDI player status messages ********/
typedef enum {
   OK = 0,              // function executed successfully
   ERR_FILE_SIZE,       // file load failed because the file was too large
   ERR_FILE_OPEN,       // file could not be opened (file not found, etc)
   ERR_FILE_TRACKS,     // file has too many (or 0) tracks
   ERR_FILE_TIMING,     // file has the wrong timing format (SMPTE)
   ERR_FILE_FORMAT,     // file has the wrong track format (serial instead of simultaneous)
   ERR_FILE_BAD,        // file failed format checks (magicnum, etc)
   ERR_MALLOC,          // failure on memory allocation
   ERR_PLAYING,         // function can't complete because file is in the middle of playback
   ERR_NOT_LOADED,      // a file is not loaded (can't be played, etc)
   ERR_NOT_INITED,      // the MIDI player has not been initialized yet
   ERR_GENERIC,         // generic error
   ERR_NOT_PLAYING,     // function can't complete because file is not playing
   ERR_BAD_ARGUMENT,    // an argument supplied to the function is bad or out-of-bounds
   ERR_UNSUPPORTED,     // function is currently unsupported
} STATUS;

/******** MIDI player functions ********/

// initializes the MIDI player (must be called before any other functions)
STATUS   Init ();
// loads a specified file for play and returns an error or OK code
STATUS   LoadFile ( char * fileName );
// rewinds a loaded MIDI file to the beginning and restores the player's state
STATUS   Rewind ();
// begins playback of the loaded MIDI (unpausing if it was paused)
STATUS   Play ();
// stops playback of the loaded MIDI and rewinds to the beginning
STATUS   Stop ();
// pauses playback of the loaded MIDI
STATUS   Pause ();
// shuts down the player and does cleanup (i.e. restoring PIT behavior)
STATUS   ShutDown ();
// returns whether the MIDI player is currently playing
bool     IsPlaying ();
// this function should be called periodically (ideally many times a frame) to service
// the MIDI driver and handle new event ticks
STATUS   Update ();
// sets the time, in seconds, at which the MIDI should be prematurely stopped
STATUS   SetPlayTime ( UInt16 seconds );

// enables the visualizer
STATUS   EnableVisualizer ();

};    // end MIDI namespace

#endif