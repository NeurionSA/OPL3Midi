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
#include "timer.h"
#include "visual.h"

// use the MIDI namespace
namespace MIDI {

/******** CONSTANTS ********/
#define  MAX_TRACKS        32          // max number of tracks player supports
#define  MAX_DELTA_TIME    0xFFFFFF    // the maximum possible d-time
#define  MAX_FILE_SIZE     0x40000     // maximum file size (256 KB)
#define  MAX_STOP_TIME     1800        // maximum time that can be specified for MIDI stop (30 minutes)

/******** MACROS ********/
#define  CUR_OFFSET        tracks[ curTrack ].curOffset

/******** STRUCTS ********/
typedef struct MidiTrack {
   UInt32   length;           // length of the track's data in bytes
   UInt32   startOffset;      // offset in the file of the start of the track's MIDI messages (after the header)
   UInt32   curOffset;        // offset in the file to the track's next event
   UInt32   timeToNextEvent;  // delta-time units until the next MIDI event occurs for this track
   Byte     status;           // whether the track is playing (non-zero means it is)
   Byte     lastMidiEvent;    // stores the last MIDI event byte, for use in Running Status
} MidiTrack;

/******** FUNCTION DECLARATIONS ********/
// (for functions not declared in the header)
void     processEvents ();       // processes queued MIDI events

/******** VARIABLES ********/
bool        inited = false;      // whether the player has been initialized
Byte *      midiData = NULL;     // pointer to MIDI file data loaded into memory
UInt32      fileOffset;          // offset into the file for operations
bool        fileLoaded = false;  // whether a file is currently loaded in memory
bool        filePlaying = false; // whether a file is currently playing

UInt32      deltaTime;     // scratchpad for delta-time values (reading and manipulating)
UInt32      headerLength;  // length of the MIDI file header
UInt16      numTracks;     // number of MIDI tracks in the file
UInt16      midiFormat;    // format of the MIDI file
UInt16      division;      // timing division (d-time units per quarter note)
MidiTrack   tracks[ MAX_TRACKS ];	// Midi Track data for playback
Byte        curTrack;      // current track being handled
UInt16      tracksDone;    // the number of tracks that have finished processing
UInt32      usPerQNote;    // the current tempo setting (microseconds per quarter note)
UInt32      deltaNext;     // d-time ticks until the next MIDI event fires
volatile UInt32 deltaCounter;  // the counter for elapsed d-ticks
UInt32      timerReload;   // the value reloaded into the timer on each tick (tracked for elapsed time)
UInt32      endPlayTime;   // time when the MIDI should automatically stop playing, measured in PIT ticks (1,193,182 per second)
UInt32      elapsedTime;   // elapsed playback time, measured in PIT ticks (1,193,182 per second)
UInt16      hTimer;        // handle of the timer we allocate
bool        visualizer;    // whether to send events to the visualizer, too

/******** FUNCTION DEFINITIONS ********/

// processes queued MIDI events
void     processEvents () {
   int      i;          // for-loop iterator
   Byte     eventType;  // event's type
   Byte     subType;    // event's sub-type (for Meta events)
   Byte     b;          // byte scratchpad
   
   // perform as many events as possible given the current d-time
   do {
      // decrement all active tracks' d-times, so long as the next event's d-time is non-zero
      if ( deltaNext ) {
         for ( i = 0; i < numTracks; i++ ) {
            if ( tracks[ i ].status != 0 ) {
               tracks[ i ].timeToNextEvent -= deltaNext;
            }
         }

         // and decrement the delta counter as well
         deltaCounter -= deltaNext;
      }
      
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
            OPL3::NoteOff( eventType & 0x0F, midiData[ CUR_OFFSET ], midiData[ CUR_OFFSET + 1 ] );
            // send the command to the visualizer, too, if its active
            if ( visualizer ) Visual::NoteOff( eventType & 0x0F, midiData[ CUR_OFFSET ], midiData[ CUR_OFFSET + 1 ] );
            // shift the offset past the event
            CUR_OFFSET += 2;
            break;
            
         case 0x90:  // Note-On
            // send the Note-On command to the driver
            OPL3::NoteOn( eventType & 0x0F, midiData[ CUR_OFFSET ], midiData[ CUR_OFFSET + 1 ] );
            // send the command to the visualizer, too, if its active
            if ( visualizer ) Visual::NoteOn( eventType & 0x0F, midiData[ CUR_OFFSET ], midiData[ CUR_OFFSET + 1 ] );
            // shift the offset past the event
            CUR_OFFSET += 2;
            break;
            
         case 0xA0:  // Polyphonic Key Pressure
            // send the command to the driver
            OPL3::AftertouchKey( eventType & 0x0F, midiData[ CUR_OFFSET ], midiData[ CUR_OFFSET + 1 ] );
            // send the command to the visualizer, too, if its active
            if ( visualizer ) Visual::AftertouchKey( eventType & 0x0F, midiData[ CUR_OFFSET ], midiData[ CUR_OFFSET + 1 ] );
            // shift the offset past the event
            CUR_OFFSET += 2;
            break;
            
         case 0xB0:  // Controller Change
            // send the Controller Change command to the driver
            OPL3::ControllerChange( eventType & 0x0F, midiData[ CUR_OFFSET ], midiData[ CUR_OFFSET + 1 ] );
            // send the command to the visualizer, too, if its active
            if ( visualizer ) Visual::ControllerChange( eventType & 0x0F, midiData[ CUR_OFFSET ], midiData[ CUR_OFFSET + 1 ] );
            // shift the offset past the event
            CUR_OFFSET += 2;
            break;
            
         case 0xC0:  // Program Change
            // send the Program Change command to the driver
            OPL3::ProgramChange( eventType & 0x0F, midiData[ CUR_OFFSET ] );
            // send the command to the visualizer, too, if its active
            if ( visualizer ) Visual::ProgramChange( eventType & 0x0F, midiData[ CUR_OFFSET ] );
            // shift the offset past the event
            CUR_OFFSET += 1;
            break;
            
         case 0xD0:  // Channel Key Pressure
            // send the command to the driver
            OPL3::AftertouchChan( eventType & 0x0F, midiData[ CUR_OFFSET ] );
            // send the command to the visualizer, too, if its active
            if ( visualizer ) Visual::AftertouchChan( eventType & 0x0F, midiData[ CUR_OFFSET ] );
            // shift the offset past the event
            CUR_OFFSET += 1;
            break;
            
         case 0xE0:  // Pitch Bend
            // send the Pitch Bend command to the driver
            OPL3::PitchBend( eventType & 0x0F, midiData[ CUR_OFFSET ], midiData[ CUR_OFFSET + 1 ] );
            // send the command to the visualizer, too, if its active
            if ( visualizer ) Visual::PitchBend( eventType & 0x0F, midiData[ CUR_OFFSET ], midiData[ CUR_OFFSET + 1 ] );
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
                        // set its end-time to max so it'll be passed up for newer tracks
                        tracks[ curTrack ].timeToNextEvent = MAX_DELTA_TIME;
                        // increment the number of completed tracks
                        tracksDone++;
                        break;
                        
                     case 0x51:  // Set Tempo
                        // assume the event's length is correct
                        CUR_OFFSET++;
                        // get the new tempo (3-byte MSB)
                        usPerQNote = ( midiData[ CUR_OFFSET ] << 16 ) |
                           ( midiData[ CUR_OFFSET + 1 ] << 8 ) |
                           ( midiData[ CUR_OFFSET + 2 ] );
                        // increment the track offset
                        CUR_OFFSET += 3;
                        // calculate the new timer reload value
                        timerReload = ( 596591.0 * usPerQNote ) / ( 500000.0 * division );
                        // set the new reload rate for the timer
                        Timer::SetTimerRate( hTimer, timerReload );
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
         filePlaying = false;
         // stop the timer we were using
         Timer::StopTimer( hTimer );         
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
      
      // loop as long as the next event is within the scope of the delta counter
   } while ( deltaCounter >= deltaNext );
}

// Initializes the player and prepares it for use
// Returns an error code on failure (or if it's already been called)
STATUS   Init () {
   Timer::STATUS  timerStatus;
   // return if it's already been initialized
   if ( inited ) return ( ERR_GENERIC );
   
   timerStatus = Timer::CreateTimer( 0x10000, &hTimer );
   if ( timerStatus != Timer::OK ) return ( ERR_GENERIC );
   
   // init the OPL3 driver
   OPL3::Init();
   
   // default to not using the visualizer
   visualizer = false;
   
   // init was successful
   inited = true;
   
   // return success
   return ( OK );
}

// Loads a MIDI file into the player
//    char * fileName      Name and optional path to the file to be loaded
// Returns an error code on failure
// Cannot be called during playback
STATUS   LoadFile ( char * fileName ) {
   FILE *   hFile;      // handle of the MIDI file for file operations
   UInt32   fileSize;   // size of the file
   int      i;          // for-loop iterator   
   
   // if the player hasn't been Inited yet abort
   if ( !inited ) return ( ERR_NOT_INITED );
   // if the file is currently playing abort
   if ( filePlaying ) return ( ERR_PLAYING );
   
   // attempt to open the file for reading
   hFile = fopen( fileName, "rb" );
   
   // if NULL, we couldn't open it
   if ( hFile == NULL ) {
      // we failed to load the file
      fileLoaded = false;
      // return an error status
      return ( ERR_FILE_OPEN );
   }
   
   // we were able to open the file, now check its size
   fseek( hFile, 0, SEEK_END );
   fileSize = ftell( hFile );
   fseek( hFile, 0, SEEK_SET );
   // if the file is too big or too small, close it and return
   if ( fileSize > MAX_FILE_SIZE || fileSize < 22 ) {
      fileLoaded = false;
      fclose( hFile );
      // return an error
      return ( ERR_FILE_SIZE );
   }
   
   // free the allocated memory if necessary and allocate memory
   if ( midiData != NULL ) free( midiData );
   midiData = (Byte *) malloc( fileSize );
   // abort on failure
   if ( midiData == NULL ) {
      fileLoaded = false;
      fclose( hFile );
      return ( ERR_MALLOC );
   }
   
   // load the file into memory
   fread( midiData, 1, fileSize, hFile );
   // close the file
   fclose( hFile );
   
   // check the header's magic number
   fileOffset = 0;
   if ( strncmp( (char *)&midiData[ 0 ], "MThd", 4 ) != 0 ) {
      // magic-num failure
      fileLoaded = false;
      return ( ERR_FILE_BAD );
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
      fileLoaded = false;
      return ( ERR_FILE_BAD );
   }
   
   // read the format (MSB)
   midiFormat = midiData[ fileOffset ] << 8 | midiData[ fileOffset + 1 ];
   fileOffset += 2;
   // abort on unsupported formats (anything other than 0 or 1)
   if ( midiFormat > 1 ) {
      fileLoaded = false;
      return ( ERR_FILE_FORMAT );
   }
   
   // read the number of tracks (MSB)
   numTracks = midiData[ fileOffset ] << 8 | midiData[ fileOffset + 1 ];
   fileOffset += 2;
   // abort if the number of tracks is more than the player supports, or 0
   if ( ( numTracks > MAX_TRACKS ) || ( numTracks == 0 ) ) {
      fileLoaded = false;
      return ( ERR_FILE_TRACKS );
   }
   
   // read the time division (MSB)
   division = midiData[ fileOffset ] << 8 | midiData[ fileOffset + 1 ];
   fileOffset += 2;
   // abort if the division is in SMTPE format
   if ( division & 0x8000 ) {
      fileLoaded = false;
      return ( ERR_FILE_TIMING );
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
         fileLoaded = false;
         return ( ERR_FILE_BAD );
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
   fileLoaded = true;
   
   // reset the end play time to 0 (ignored)
   endPlayTime = 0;
   // tell the visualizer the name of the file, even if we aren't using it
   Visual::SetFileName( fileName );
   
   // rewind the MIDI to prepare it for play
   Rewind();
   
   // return success
   return ( OK );
}

// Rewinds a loaded MIDI to the beginning and restores the player's state to default
// Returns an error code on failure
// Cannot be called during playback
STATUS   Rewind () {
   int      i;          // for-loop iterator
   Byte     b;          // byte scratchpad
   
   // if the player hasn't been Inited yet abort
   if ( !inited ) return ( ERR_NOT_INITED );
   // if the file is currently playing abort
   if ( filePlaying ) return ( ERR_PLAYING );
   // if no file has been loaded then abort
   if ( !fileLoaded ) return ( ERR_NOT_LOADED );
   
   // set the tempo to the default
   usPerQNote = 500000;
   // set the number of completed tracks to 0
   tracksDone = 0;
   // calculate the value to reload the PIT with
   timerReload = ( 596591.0 * usPerQNote ) / ( 500000.0 * division );
   Timer::SetTimerRate( hTimer, timerReload );
   // reset the elapsed time
   elapsedTime = 0;
   
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
      OPL3::ResetChanControllers( i );
   }
   
   // return success
   return ( OK );
}

// Begins playback of the loaded MIDI (unpausing if it was paused)
// Returns an error code on failure
// Cannot be called during playback
STATUS   Play () {
   // if the player hasn't been Inited yet abort
   if ( !inited ) return ( ERR_NOT_INITED );
   // if the file is currently playing abort
   if ( filePlaying ) return ( ERR_PLAYING );
   // if no file has been loaded then abort
   if ( !fileLoaded ) return ( ERR_NOT_LOADED );
   
   // file is now playing
   filePlaying = true;
   
   // Start the timer
   Timer::StartTimer( hTimer );
   
   // return an OK status
   return ( OK );
}

// Stops playback of the loaded MIDI and rewinds to the beginning
// Returns an error code on failure
STATUS   Stop () {
   // if the player hasn't been Inited yet abort
   if ( !inited ) return ( ERR_NOT_INITED );
   // if no file has been loaded then abort
   if ( !fileLoaded ) return ( ERR_NOT_LOADED );
   // if file is not playing then abort
   if ( !filePlaying ) return ( ERR_NOT_PLAYING );
   
   // stop the timer
   Timer::StopTimer( hTimer );
   
   // stop playback of all notes
   OPL3::AllNotesOff();
   
   // file is not playing
   filePlaying = false;
   // rewind
   Rewind();
   
   // return an OK status
   return ( OK );
}

// Pauses playback of the loaded MIDI
// Returns an error code on failure
STATUS   Pause () {
   // if the player hasn't been Inited yet abort
   if ( !inited ) return ( ERR_NOT_INITED );
   // if no file has been loaded then abort
   if ( !fileLoaded ) return ( ERR_NOT_LOADED );
   // if file is not playing then abort
   if ( !filePlaying ) return ( ERR_NOT_PLAYING );
   
   // stop the timer
   Timer::StopTimer( hTimer );
   
   // stop playback of all notes
   OPL3::AllNotesOff();
   
   // file is not playing
   filePlaying = false;
   
   // return an OK status
   return ( OK );
}

// Shuts down the player and does cleanup (i.e. restoring PIT behavior)
// Returns an error code on failure
STATUS   ShutDown () {
   
   // if the player hasn't been Inited yet abort
   if ( !inited ) return ( ERR_NOT_INITED );
   
   // stop the timer and free it
   Timer::StopTimer( hTimer );
   Timer::FreeTimer( hTimer );
   
   // shut down/reset OPL3 driver by calling init on it
   OPL3::Init();
   
   // we're uninited now
   inited = false;
   
   // return success
   return ( OK );
}

// Returns if the player is currently playing
bool    IsPlaying () {
   return ( filePlaying );
}

// This function should be called periodically (ideally many times a frame) to service
// the MIDI driver and handle new event ticks
STATUS   Update () {
   UInt32   timerTicks = 0;
   
   // return an error if the file isn't playing
   if ( !filePlaying ) return ( ERR_NOT_PLAYING );
   
   // get the number of ticks that have elapsed for the timer
   Timer::GetTimerTicks( hTimer, &timerTicks );
   
   // proceed only if new delta ticks have occured
   if ( timerTicks ) {
      // increment the elapsed time
      elapsedTime += timerReload * timerTicks;
      // if the elapsed time has met/exceeded the end-time, then stop and return
      if ( ( endPlayTime > 0 ) && ( elapsedTime >= endPlayTime ) ) {
         Stop();
         // return success
         return ( OK );
      }
      // increment the deltaCounter
      deltaCounter += timerTicks;
      
      // check if enough d-ticks have passed
      if ( deltaCounter >= deltaNext ) {
         // process MIDI events
         processEvents();
      }
   }
   
   // return success
   return ( OK );
}

// Sets the time, in seconds, at which the MIDI should be prematurely stopped
//    WORD  seconds      Time in seconds when the MIDI should stop playing
// Note that accuracy of the stop time is dependent on MIDI_Update being called frequently,
// because the check for stopping is made there
// Returns an error code on failure
STATUS   SetPlayTime ( UInt16 seconds ) {

   // if the player hasn't been Inited yet abort
   if ( !inited ) return ( ERR_NOT_INITED );
   // if the file is currently playing abort
   if ( filePlaying ) return ( ERR_PLAYING );
   // if no file has been loaded then abort
   if ( !fileLoaded ) return ( ERR_NOT_LOADED );
   
   // return an error if the provided value was too high
   if ( seconds > MAX_STOP_TIME ) return ( ERR_BAD_ARGUMENT );

   // set the variable (measured in PIT clock ticks)
   endPlayTime = seconds * 1193182;

   // return success
   return ( OK );
}

STATUS   EnableVisualizer () {
      // if the player hasn't been Inited yet abort
   if ( !inited ) return ( ERR_NOT_INITED );
   // if no file has been loaded then abort
   if ( !fileLoaded ) return ( ERR_NOT_LOADED );
   
   visualizer = true;
   
   // return success
   return ( OK );
}

};    // end MIDI namespace