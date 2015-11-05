/********************************************************************
**
** MIDI.CPP
**
** The MIDI player functions and timing logic
**
********************************************************************/

#include <stdio.h>      // for file I/O
#include <stdlib.h>     // for malloc, etc
#include <string.h>     // for strncmp, etc
#include <dos.h>        // for interrupt vectors and chaining
#include <conio.h>      // for hardware port I/O
#include "globals.h"
#include "midi.h"
#include "opl3.h"

/******** CONSTANTS ********/
#define  MAX_TRACKS        32          // max number of tracks player supports
#define  MAX_DELTA_TIME    0xFFFFFF    // the maximum possible d-time
#define  MAX_FILE_SIZE     0x40000     // maximum file size (256 KB)

/******** MACROS ********/
#define  CUR_OFFSET        tracks[curTrack].curOffset

/******** STRUCTS ********/
typedef struct MidiTrack {
   DWORD    length;           // length of the track's data in bytes
   DWORD    startOffset;      // offset in the file of the start of the track's MIDI messages (after the header)
   DWORD    curOffset;        // offset in the file to the track's next event
   DWORD    timeToNextEvent;  // delta-time units until the next MIDI event occurs for this track
   BYTE     status;           // whether the track is playing (non-zero means it is)
   BYTE     lastMidiEvent;    // stores the last MIDI event byte, for use in Running Status
} MidiTrack;

/******** FUNCTION DECLARATIONS ********/
// (for functions not declared in the header)
void __interrupt __far timerHandler ();   // real-mode handler for the PIT interrupt
void     processEvents ();       // processes queued MIDI events

/******** VARIABLES ********/
void ( __interrupt __far *biosTimerHandler )();  // function pointer for the BIOS PIT interrupt handler
volatile DWORD    clockTicks;    // counter of elapsed clock ticks for chaining to the original PIT interrupt
DWORD       timerReload;         // the value reloaded into the timer on each tick

BOOL        inited = FALSE;      // whether the player has been initialized
BYTE *      midiData = NULL;     // pointer to MIDI file data loaded into memory
DWORD       fileOffset;          // offset into the file for operations
BOOL        fileLoaded = FALSE;  // whether a file is currently loaded in memory
BOOL        filePlaying = FALSE; // whether a file is currently playing

DWORD       deltaTime;     // scratchpad for delta-time values (reading and manipulating)
DWORD       headerLength;  // length of the MIDI file header
WORD        numTracks;     // number of MIDI tracks in the file
WORD        midiFormat;    // format of the MIDI file
WORD        division;      // timing division (d-time units per quarter note)
MidiTrack   tracks[ MAX_TRACKS ];	// Midi Track data for playback
BYTE        curTrack;      // current track being handled
WORD        tracksDone;    // the number of tracks that have finished processing
DWORD       tempo;         // the current tempo setting (microseconds per quarter note)
DWORD       deltaNext;     // d-time ticks until the next MIDI event fires
volatile DWORD deltaCounter;  // the counter for elapsed d-ticks
volatile BYTE updateFlag;    // flag is set when a timer tick has been received

/******** FUNCTION DEFINITIONS ********/

// real-mode PIT interrupt handler
// it's short and sweet, and DOES NOT call the event-processing function!
// this is to ensure that the handler runs in real mode, while the event-processing
// is all handled in protected mode
void __interrupt __far timerHandler () {
   // set the flag indicating an update is ready
   updateFlag = 1;
   // increment the delta counter
   deltaCounter++;
   
   // increment the tick counter by the value we loaded into the timer
   clockTicks += timerReload;
   // should we also call the BIOS handler?
   if ( clockTicks >= 0x10000 ) {
      // decrement the value
      clockTicks -= 0x10000;
      
      // call the BIOS handler
      _chain_intr( biosTimerHandler );
   } else {
      // we didn't call the BIOS handler so acknowledge the interrupt
      outp( 0x20, 0x20 );
   }
}

// processes queued MIDI events
void     processEvents () {
   int      i;          // for-loop iterator
   BYTE     eventType;  // event's type
   BYTE     subType;    // event's sub-type (for Meta events)
   BYTE     b;          // byte scratchpad
   
   // decrement the tracks' d-times
   for ( i = 0; i < numTracks; i++ ) {
      tracks[ i ].timeToNextEvent -= deltaNext;
   }
   
   // decement the delta counter
   deltaCounter -= deltaNext;
   
   // try to perform as many concurrent events as possible
   do {
      // get the event's type
      eventType = midiData[ CUR_OFFSET ];
      CUR_OFFSET++;
      
      // if the high bit of the eventType is set, then it's a new event
      // otherwise it's a Running-Status and we need to account for that
      if ( ( eventType & 0x80 ) == 0 ) {
         // it's a Running-Status, so make the eventType match that of the previous
         eventType = tracks[ curTrack ].lastMidiEvent;
         // go back a byte so it can be re-read
         CUR_OFFSET--;
      } else {
         // set the last event to match the current event
         tracks[ curTrack ].lastMidiEvent = eventType;
      }
      
      // branch based on the event type (upper nibble)
      switch ( eventType & 0xF0 ) {
         case 0x80:  // Note-Off
            // send the Note-Off command to the driver
            OPL3_NoteOff( eventType & 0x0F, midiData[ CUR_OFFSET ], midiData[ CUR_OFFSET + 1 ] );
            // shift the offset past the event
            CUR_OFFSET += 2;
            break;
            
         case 0x90:  // Note-On
            // send the Note-On command to the driver
            OPL3_NoteOn( eventType & 0x0F, midiData[ CUR_OFFSET ], midiData[ CUR_OFFSET + 1 ] );
            // shift the offset past the event
            CUR_OFFSET += 2;
            break;
            
         case 0xA0:  // Polyphonic Key Pressure
            // send the command to the driver
            OPL3_AftertouchKey( eventType & 0x0F, midiData[ CUR_OFFSET ], midiData[ CUR_OFFSET + 1 ] );
            // shift the offset past the event
            CUR_OFFSET += 2;
            break;
            
         case 0xB0:  // Controller Change
            // send the Controller Change command to the driver
            OPL3_ControllerChange( eventType & 0x0F, midiData[ CUR_OFFSET ], midiData[ CUR_OFFSET + 1 ] );
            // shift the offset past the event
            CUR_OFFSET += 2;
            break;
            
         case 0xC0:  // Program Change
            // send the Program Change command to the driver
            OPL3_ProgramChange( eventType & 0x0F, midiData[ CUR_OFFSET ] );
            // shift the offset past the event
            CUR_OFFSET += 1;
            break;
            
         case 0xD0:  // Channel Key Pressure
            // send the command to the driver
            OPL3_AftertouchChan( eventType & 0x0F, midiData[ CUR_OFFSET ] );
            // shift the offset past the event
            CUR_OFFSET += 1;
            break;
            
         case 0xE0:  // Pitch Bend
            // send the Pitch Bend command to the driver
            OPL3_PitchBend( eventType & 0x0F, midiData[ CUR_OFFSET ], midiData[ CUR_OFFSET + 1 ] );
            // shift the offset past the event
            CUR_OFFSET += 2;
            break;
            
         case 0xF0:  // Meta or Sysex
            // branch based on whether it's Meta or Sysex
            switch ( eventType ) {
               case 0xF0:
               case 0xF7:  // Sysex Events
                  // we ignore these, so increment the offset past them
                  CUR_OFFSET += midiData[ CUR_OFFSET ] + 1;
                  break;
                  
               case 0xFF:  // Meta Event
                  // get the event's sub-type
                  subType = midiData[ CUR_OFFSET ];
                  CUR_OFFSET++;
                  
                  // branch based on the sub-type
                  switch ( subType ) {
                     case 0x2F:  // End of Track
                        // assume the event's length is correct
                        CUR_OFFSET++;
                        // set the track to done to stop processing it
                        tracks[ curTrack ].status = 0;
                        // and set its time to next event obscenely high to avoid it processing in the timer code
                        tracks[ curTrack ].timeToNextEvent = MAX_DELTA_TIME;
                        // increment the number of completed tracks
                        tracksDone++;
                        break;
                        
                     case 0x51:  // Set Tempo
                        // assume the event's length is correct
                        CUR_OFFSET++;
                        // get the new tempo (3-byte MSB)
                        tempo = ( midiData[ CUR_OFFSET ] << 16 ) |
                           ( midiData[ CUR_OFFSET + 1 ] << 8 ) |
                           ( midiData[ CUR_OFFSET + 2 ] );
                        // increment the track offset
                        CUR_OFFSET += 3;
                        // calculate the new timer reload value
                        timerReload = ( 596591.0 * tempo ) / ( 500000.0 * division );
                        // reprogram the PIT with the new reload value
								_disable();
                        outp( 0x43, 0x34 );
                        outp( 0x40, timerReload & 0xFF );   // write lo-byte
                        outp( 0x40, timerReload >> 8 );     // write hi-byte
								_enable();
                        break;
                     
                     default:
                        // unimportant events
                        // these events are considered unimportant to actual playback and will be skipped
                        // 0x00 - Sequence Number
                        // 0x01 - Text Event
                        // 0x02 - Copyright Notice
                        // 0x03 - Sequence/Track Name
                        // 0x04 - Instrument Name
                        // 0x05 - Lyric
                        // 0x06 - Marker
                        // 0x07 - Cue Point
                        // 0x20 - MIDI Channel Prefix
                        // 0x58 - Time Signature
                        // 0x59 - Key Signature
                        // 0x7F - Sequencer-Specific Meta-event
                        // increment past it
                        CUR_OFFSET += midiData[ CUR_OFFSET ] + 1;
                        break;
                  }  // END Meta Event subtype switch
            }  // END Meta or Sysex switch
            break;
      }  // END eventType switch
      
      // get the delta-time of the track's next event if the track isn't done
      if ( tracks[ curTrack ].status != 0 ) {
         deltaTime = 0;
         do {
            // shift the deltaTime left by 7
            deltaTime = deltaTime << 7;
            // grab a byte for the VLQ
            b = midiData[ CUR_OFFSET ];
            // OR in the lower 7 bits
            deltaTime = deltaTime | (b & 0x7F);
            // advance the current offset and loop until the uppermost bit is clear
            CUR_OFFSET++;
         } while ( b & 0x80 );
         tracks[ curTrack ].timeToNextEvent = deltaTime;
      }
      
      // check if all tracks have finished
      if ( tracksDone >= numTracks ) {
         // playback has stopped
         filePlaying = FALSE;
         // restore the PIT interval and handler
         _disable();
         outp( 0x43, 0x34 );
         outp( 0x40, 0x00 );
         outp( 0x40, 0x00 );
         _dos_setvect( 0x08, biosTimerHandler );
         _enable();
         // break out of the loop
         break;
      }
      
      // figure out which track has the next event (lowest d-time)
      curTrack = 0;
      for ( i = 0; i < numTracks; i++ ) {
         if ( tracks[ i ].status != 0 ) {
            if ( tracks[ i ].timeToNextEvent < tracks[ curTrack ].timeToNextEvent ) curTrack = i;
         }
      }
      // store the d-time until the next event
      deltaNext = tracks[ curTrack ].timeToNextEvent;
      
      // loop until the next event's d-time is non-zero
   } while ( deltaCounter > deltaNext );
}

// Initializes the player and prepares it for use
// Returns an error code on failure (or if it's already been called)
int      MIDI_Init () {
   // return if it's already been initialized
   if ( inited ) return ( MIDI_ERR_GENERIC );
   
   // set default values
   clockTicks = 0;
   timerReload = 0x10000;
   // save the current DOS interrupt timer handler (vector 0x08)
   biosTimerHandler = _dos_getvect( 0x08 );
   
   // init the OPL3 driver
   OPL3_Init();
   
   // init was successful
   inited = TRUE;
   updateFlag = 0;
   
   // return success
   return ( MIDI_OK );
}

// Loads a MIDI file into the player
//    char * fileName      Name and optional path to the file to be loaded
// Returns an error code on failure
// Cannot be called during playback
int      MIDI_LoadFile ( char * fileName ) {
   FILE *   hFile;      // handle of the MIDI file for file operations
   DWORD    fileSize;   // size of the file
   int      i;          // for-loop iterator   
   
   // if the player hasn't been Inited yet abort
   if ( !inited ) return ( MIDI_ERR_NOT_INITED );
   // if the file is currently playing abort
   if ( filePlaying ) return ( MIDI_ERR_PLAYING );
   
   // attempt to open the file for reading
   hFile = fopen( fileName, "rb" );
   
   // if NULL, we couldn't open it
   if ( hFile == NULL ) {
      // we failed to load the file
      fileLoaded = FALSE;
      // return an error status
      return ( MIDI_ERR_FILE_OPEN );
   }
   
   // we were able to open the file, now check its size
   fseek( hFile, 0, SEEK_END );
   fileSize = ftell( hFile );
   fseek( hFile, 0, SEEK_SET );
   // if the file is too big or too small, close it and return
   if ( fileSize > MAX_FILE_SIZE || fileSize < 22 ) {
      fileLoaded = FALSE;
      fclose( hFile );
      // return an error
      return ( MIDI_ERR_FILE_SIZE );
   }
   
   // free the allocated memory if necessary and allocate memory
   if ( midiData != NULL ) free( midiData );
   midiData = (BYTE *) malloc( fileSize );
   // abort on failure
   if ( midiData == NULL ) {
      fileLoaded = FALSE;
      fclose( hFile );
      return ( MIDI_ERR_MALLOC );
   }
   
   // load the file into memory
   fread( midiData, 1, fileSize, hFile );
   // close the file
   fclose( hFile );
   
   // check the header's magic number
   fileOffset = 0;
   if ( strncmp( (char *)&midiData[ 0 ], "MThd", 4 ) != 0 ) {
      // magic-num failure
      fileLoaded = FALSE;
      return ( MIDI_ERR_FILE_BAD );
   }
   fileOffset += 4;
   
   // get the length of the header (MSB)
   headerLength = ( midiData[ fileOffset ] << 24 ) |
      ( midiData[ fileOffset + 1 ] << 16 ) |
      ( midiData[ fileOffset + 2 ] << 8 ) |
      ( midiData[ fileOffset + 3 ] );
   fileOffset += 4;
   // if the header length is < 6 then abort
   if ( headerLength < 6 ) {
      fileLoaded = FALSE;
      return ( MIDI_ERR_FILE_BAD );
   }
   
   // read the format (MSB)
   midiFormat = midiData[ fileOffset ] << 8 | midiData[ fileOffset + 1 ];
   fileOffset += 2;
   // abort on unsupported formats (anything other than 0 or 1)
   if ( midiFormat > 1 ) {
      fileLoaded = FALSE;
      return ( MIDI_ERR_FILE_FORMAT );
   }
   
   // read the number of tracks (MSB)
   numTracks = midiData[ fileOffset ] << 8 | midiData[ fileOffset + 1 ];
   fileOffset += 2;
   // abort if the number of tracks is more than the player supports, or 0
   if ( ( numTracks > MAX_TRACKS ) || ( numTracks == 0 ) ) {
      fileLoaded = FALSE;
      return ( MIDI_ERR_FILE_TRACKS );
   }
   
   // read the time division (MSB)
   division = midiData[ fileOffset ] << 8 | midiData[ fileOffset + 1 ];
   fileOffset += 2;
   // abort if the division is in SMTPE format
   if ( division & 0x8000 ) {
      fileLoaded = FALSE;
      return ( MIDI_ERR_FILE_TIMING );
   }
   
   // skip past any unknown header bytes
   if ( headerLength > 6 ) fileOffset += ( headerLength - 6 );
   
   // reset track info
   for ( i = 0; i < numTracks; i++ ) {
      tracks[ i ].length = 0;
      tracks[ i ].startOffset = 0;
      tracks[ i ].timeToNextEvent = MAX_DELTA_TIME;
      tracks[ i ].status = 0;
      tracks[ i ].lastMidiEvent = 0;
   }
   
   // determine information for all the tracks, specifically their data offset and lengths in the file
   for ( i = 0; i < numTracks; i++ ) {
      // read the track's magic num and abort if it's bad
      if ( strncmp( (char *)&midiData[ fileOffset ], "MTrk", 4 ) != 0 ) {
         fileLoaded = FALSE;
         return ( MIDI_ERR_FILE_BAD );
      }
      fileOffset += 4;
      
      // read the track's length (MSB)
      tracks[ i ].length = ( midiData[ fileOffset ] << 24 ) |
         ( midiData[ fileOffset + 1 ] << 16 ) |
         ( midiData[ fileOffset + 2 ] << 8 ) |
         ( midiData[ fileOffset + 3 ] );
      fileOffset += 4;
      
      // get the track's offset
      tracks[ i ].startOffset = fileOffset;
      tracks[ i ].curOffset = fileOffset;
      
      // advance to the next track's header
      fileOffset = tracks[ i ].startOffset + tracks[ i ].length;
   }
   
   // load was successful
   fileLoaded = TRUE;
   
   // rewind the MIDI to prepare it for play
   MIDI_Rewind();
   
   // return success
   return ( MIDI_OK );
}

// Rewinds a loaded MIDI to the beginning and restores the player's state to default
// Returns an error code on failure
// Cannot be called during playback
int      MIDI_Rewind () {
   int      i;          // for-loop iterator
   BYTE     b;          // byte scratchpad
   
   // if the player hasn't been Inited yet abort
   if ( !inited ) return ( MIDI_ERR_NOT_INITED );
   // if the file is currently playing abort
   if ( filePlaying ) return ( MIDI_ERR_PLAYING );
   // if no file has been loaded then abort
   if ( !fileLoaded ) return ( MIDI_ERR_NOT_LOADED );
   
   // set the tempo to the default tempo
   tempo = 500000;
   // set the number of completed tracks to 0
   tracksDone = 0;
   // calculate the value to reload the PIT with
   timerReload = ( 596591.0 * tempo ) / ( 500000.0 * division );
   // clear the update request flag
   updateFlag = 0;
   // iterate through the tracks and reset them
   for ( i = 0; i < numTracks; i++ ) {
      // reset the track's current offset
      tracks[ i ].curOffset = tracks[i].startOffset;
      // set the track as playable
      tracks[ i ].status = 1;
      
      // read the delta-time of the track's first event
      deltaTime = 0;
      do {
         // shift the deltaTime left by 7
         deltaTime = deltaTime << 7;
         // grab a byte for the VLQ
         b = midiData[ tracks[ i ].curOffset ];
         // OR in the lower 7 bits
         deltaTime = deltaTime | ( b & 0x7F );
         // advance the current offset and loop until the uppermost bit is clear
         tracks[ i ].curOffset++;
      } while ( b & 0x80 );
      tracks[ i ].timeToNextEvent = deltaTime;
   }
   
   // figure out which track has the next event (lowest d-time)
   curTrack = 0;
   for ( i = 0; i < numTracks; i++ ) {
      if ( tracks[ i ].status != 0 ) {
         if ( tracks[ i ].timeToNextEvent < tracks[ curTrack ].timeToNextEvent ) curTrack = i;
      }
   }
   // store the d-time until the next event
   deltaNext = tracks[ curTrack ].timeToNextEvent;
   deltaCounter = 0;
   
   // reset all OPL3 channel controllers
   for ( i = 0; i < 16; i++ ) {
      OPL3_ResetChanControllers( i );
   }
   
   // return success
   return ( MIDI_OK );
}

// Begins playback of the loaded MIDI (unpausing if it was paused)
// Returns an error code on failure
// Cannot be called during playback
int      MIDI_Play () {
   // if the player hasn't been Inited yet abort
   if ( !inited ) return ( MIDI_ERR_NOT_INITED );
   // if the file is currently playing abort
   if ( filePlaying ) return ( MIDI_ERR_PLAYING );
   // if no file has been loaded then abort
   if ( !fileLoaded ) return ( MIDI_ERR_NOT_LOADED );
   
   // file is now playing
   filePlaying = TRUE;
   
   // set the interrupt vector and load the PIT with the reload value
   _disable();
   _dos_setvect( 0x08, timerHandler );
   outp( 0x43, 0x34 );
   outp( 0x40, timerReload & 0xFF );   // write lo-byte
   outp( 0x40, timerReload >> 8 );     // write hi-byte
   _enable();
   
   // return an OK status
   return ( MIDI_OK );
}

// Stops playback of the loaded MIDI and rewinds to the beginning
// Returns an error code on failure
int      MIDI_Stop () {
   // if the player hasn't been Inited yet abort
   if ( !inited ) return ( MIDI_ERR_NOT_INITED );
   // if no file has been loaded then abort
   if ( !fileLoaded ) return ( MIDI_ERR_NOT_LOADED );
   // if file is not playing then abort
   if ( !filePlaying ) return ( MIDI_ERR_NOT_PLAYING );
   
   // restore the PIT to normal
   _disable();
   outp( 0x43, 0x34 );
   outp( 0x40, 0x00 );
   outp( 0x40, 0x00 );
   _dos_setvect( 0x08, biosTimerHandler );
   _enable();
   
   // stop playback of all notes
   OPL3_AllNotesOff();
   
   // file is not playing
   filePlaying = FALSE;
   // rewind
   MIDI_Rewind();
   
   // return an OK status
   return ( MIDI_OK );
}

// Pauses playback of the loaded MIDI
// Returns an error code on failure
int      MIDI_Pause () {
   // if the player hasn't been Inited yet abort
   if ( !inited ) return ( MIDI_ERR_NOT_INITED );
   // if no file has been loaded then abort
   if ( !fileLoaded ) return ( MIDI_ERR_NOT_LOADED );
   // if file is not playing then abort
   if ( !filePlaying ) return ( MIDI_ERR_NOT_PLAYING );
   
   // restore the PIT to normal
   _disable();
   outp( 0x43, 0x34 );
   outp( 0x40, 0x00 );
   outp( 0x40, 0x00 );
   _dos_setvect( 0x08, biosTimerHandler );
   _enable();
   
   // stop playback of all notes
   OPL3_AllNotesOff();
   
   // file is not playing
   filePlaying = FALSE;
   
   // return an OK status
   return ( MIDI_OK );
}

// Shuts down the player and does cleanup (i.e. restoring PIT behavior)
// Returns an error code on failure
int      MIDI_ShutDown () {
   
   // if the player hasn't been Inited yet abort
   if ( !inited ) return ( MIDI_ERR_NOT_INITED );
   
   // disable interrupts while we reprogram the PIT to its default behavior
   _disable();
   // restore the normal clock frequency
   outp( 0x43, 0x34 );
   outp( 0x40, 0x00 );
   outp( 0x40, 0x00 );
   // restore the interrupt handler
   _dos_setvect( 0x08, biosTimerHandler );
   // re-enable interrupts
   _enable();
   
   // shut down/reset OPL3 driver by calling init on it
   OPL3_Init();
   
   // we're uninited now
   inited = FALSE;
   
   // return success
   return ( MIDI_OK );
}

// Returns if the player is currently playing
BOOL     MIDI_IsPlaying () {
   return ( filePlaying );
}

// This function should be called periodically (ideally many times a frame) to service
// the MIDI driver and handle new event ticks
void     MIDI_Update () {
   // check if the update flag was set, and then proceed with other checks
   if ( updateFlag ) {
      // clear the flag
      updateFlag = 0;
      
      // check if enough d-ticks have passed
      if ( deltaCounter >= deltaNext ) {
         // process MIDI events
         processEvents();
      }
   }
}