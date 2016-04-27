/********************************************************************
**
** MAIN.CPP
**
** The entrypoint for the OPL3 MIDI player.
**
********************************************************************/

#include <stdio.h>		// for standard I/O
#include <conio.h>      // for getch()
#include <string.h>     // for string functions
#include <stdlib.h>     // for atoi
#include "globals.h"
#include "midi.h"
#include "opl3.h"
#include "timer.h"
#include "visual.h"

/******** CONSTANTS ********/
#define  ARG_NULL       0     // unknown argument
#define  ARG_PATCHBANK  1     // patch bank commandline arg
#define  ARG_ENDTIME    2     // ending time of the MIDI

#define  VIS_OFF        0     // no visualizer
#define  VIS_TEXT       1     // visualizer in text mode
#define  VIS_SVGA       2     // visualizer in SVGA mode

// This function prints the program's usage/help
void     printUsage () {
   printf( "USAGE: PLAYMIDI filename [/P patch-bank ...][/E end-time]\n" );
   printf( "  %-14s %s\n", "filename", "The MIDI file to play" );
   printf( "  %-14s %s\n", "/P patch-bank [...]", "Load alternate bank from file 'patch-bank'" );
   printf( "  %-14s %s\n", "/E end-time", "Time to force-end the MIDI in format MM:SS" );
   printf( "  %-14s s%\n", "/V0", "Enable piano roll visualizer in Text mode" );
   printf( "  %-14s s%\n", "/V1", "Enable piano roll visualizer in SVGA mode" );
}

// Main entrypoint
// TODO: Tidy this up, perhaps splitting parts into other methods (argument parse, etc)
int      main ( int argc, char **argv ) {
   MIDI::STATUS   midiStatus;    // return code from MIDI funcs
   OPL3::STATUS   oplStatus;        // return code from OPL3 funcs
   char     key;              // keyboard key pressed
   UInt16   i;                // for-loop iterator
   Byte     curArg;           // current argument being handled
   Byte     midiFileIndex;    // argument index that contains the MIDI file
   Byte     patchFileIndex;   // argument index of the first patch file
   Byte     numPatchFiles;    // number of patches to load from the command line
   UInt16   endTimeSec;       // playtime when the MIDI should be ended, in seconds
   Byte     visMode;          // visualizer mode
   
   // initialize argument variables
   midiFileIndex = 0;
   patchFileIndex = 0;
   numPatchFiles = 0;
   endTimeSec = 0;
   curArg = ARG_NULL;
   visMode = VIS_OFF;

   // parse the command-line to check the arguments
   // if there are too-few arguments, print the usage and exit
   if ( argc < 2 ) {
      printUsage();
      return 0;
   }

   // iterate through the arguments to gather info on execution options
   for ( i = 1; i < argc; i++ ) {
      // check if this argument is a switch
      if ( argv[ i ][ 0 ] == 0x2F ) {
         // it's a switch, determine what kind it is
         strupr( argv[ i ] );
         if ( strcmp( argv[ i ], "/P" ) == 0 ) {
            // patch bank argument
            curArg = ARG_PATCHBANK;
         } else if ( strcmp( argv[ i ], "/E" ) == 0 ) {
            // playback end time argument
            curArg = ARG_ENDTIME;
         } else if ( strcmp( argv[ i ], "/V0" ) == 0 ) {
            // visualizer text mode
            visMode = VIS_TEXT;
         } else if ( strcmp( argv[ i ], "/V1" ) == 0 ) {
            // visualizer SVGA mode
            visMode = VIS_SVGA;
         } else {
            // unknown argument
            curArg = ARG_NULL;
         }
      } else {
         // it's an argument, switch based on type
         switch ( curArg ) {
            case ARG_PATCHBANK:
               // if the index of the first patch file hasn't been set, then do so
               if ( patchFileIndex == 0 ) patchFileIndex = i;
               // increment the number of patch files
               numPatchFiles++;
               break;

            case ARG_ENDTIME:
               // get the end time from the argument and then exit the argument
               char *   colonPos;
               // search for the colon in the string
               colonPos = strchr( argv[ i ], 0x3A );
               if ( colonPos != NULL ) {
                  // turn the colon into a null to artificially split the string
                  *colonPos = 0;
                  // compute the end-time
                  endTimeSec = atoi( argv[ i ] ) * 60 + atoi( colonPos + 1 );
               } else {
                  // no colon was found, so treat the argument as raw seconds
                  endTimeSec = atoi( argv[ i ] );
               }

               curArg = ARG_NULL;
               break;

            default:
               // if the index of the MIDI file hasn't been set, then do so
               if ( midiFileIndex == 0 ) midiFileIndex = i;
               // all other unknown arguments are ignored
               break;
         }
      }

   } // END argument loop for

   // if no MIDI has been defined, then print an error message and exit
   if ( midiFileIndex == 0 ) {
      printf( "The syntax of the command is incorrect.\n" );
      return 0;
   }
   
   // init the Timer functionality with hopefully a good refresh rate
   // 4970 = ~240 Hz
   Timer::Init( 4970 );
   // init the MIDI player
   midiStatus = MIDI::Init();
   
   // load the MIDI file
   midiStatus = MIDI::LoadFile( argv[ midiFileIndex ] );
   
   // if the load was ok, proceed
   if ( midiStatus == MIDI::OK ) {
      // if no banks were passed, then load the default
      if ( numPatchFiles == 0 ) {
         oplStatus = OPL3::LoadPatchBank( "DEFAULT.BNK", false );
   
      } else {
         // otherwise, load the banks
         for ( i = 0; i < numPatchFiles; i++ ) {
            // TODO: Handle patch load failures
            oplStatus = OPL3::LoadPatchBank( argv[ patchFileIndex + i ], i != 0 );
         }
      }
   
      // set the End time for the MIDI if one was provided
      if ( endTimeSec ) {
         MIDI::SetPlayTime( endTimeSec );
      }
      
      // test playing
      if ( midiStatus == MIDI::OK ) {
         if ( visMode ) {
            // tell the MIDI player to speak to the visualizer
            MIDI::EnableVisualizer();
            // activate the visualizer
            Visual::Enable( visMode == VIS_SVGA );
         }
         MIDI::Play();
         // loop while we wait for the player to finish
         do {
            // have the MIDI driver service itself if it needs to
            MIDI::Update();
            // update the visualizer if it's on
            if ( visMode ) Visual::Update();
            
            // abort if a key is pressed
            if ( kbhit() ) {
               key = getch();
   
               // break on any key
               break;
            }
         } while ( MIDI::IsPlaying() );
   
      }
      // if we used the visualizer, disable it
      if ( visMode ) {
         Visual::Disable();
      }
      
   } else {
      // MIDI load failed, print error code
      printf( "\n" );
      printf( "ERROR - MIDI::LoadFile returned: %d\n", midiStatus );
   } // END load status check
   
   // shut down the player when done
   MIDI::ShutDown();
   // uninitialize the Timer driver
   Timer::Uninit();

   return 0;
}