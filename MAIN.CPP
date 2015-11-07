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

/******** CONSTANTS ********/
#define  ARG_NULL       0     // unknown argument
#define  ARG_PATCHBANK  1     // patch bank commandline arg
#define  ARG_ENDTIME    2     // ending time of the MIDI

// This function prints the program's usage/help
void     printUsage () {
   printf( "USAGE: PLAYMIDI filename [/P patch-bank ...][/E end-time]\n" );
   printf( "  %-14s %s\n", "filename", "The MIDI file to play" );
   printf( "  %-14s %s\n", "/P patch-bank [...]", "Load alternate bank from file 'patch-bank'" );
   printf( "  %-14s %s\n", "/E end-time", "Time to force-end the MIDI in format MM:SS" );
}

// Main entrypoint
int      main ( int argc, char **argv ) {
   int      midiStatus;       // return code from MIDI funcs
   int      oplStatus;        // return code from OPL3 funcs
   char     key;              // keyboard key pressed
   WORD     i;                // for-loop iterator
   BYTE     curArg;           // current argument being handled
   BYTE     midiFileIndex;    // argument index that contains the MIDI file
   BYTE     patchFileIndex;   // argument index of the first patch file
   BYTE     numPatchFiles;    // number of patches to load from the command line
   WORD     endTimeSec;       // playtime when the MIDI should be ended, in seconds
   
   // initialize argument variables
   midiFileIndex = 0;
   patchFileIndex = 0;
   numPatchFiles = 0;
   endTimeSec = 0;
   curArg = ARG_NULL;

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
         }
      }

   } // END argument loop for

   // if no MIDI has been defined, then print an error message and exit
   if ( midiFileIndex == 0 ) {
      printf( "The syntax of the command is incorrect.\n" );
      return 0;
   }

   // init the MIDI player
   MIDI_Init();

   // load the MIDI file
   midiStatus = MIDI_LoadFile( argv[ midiFileIndex ] );
   // output the result to the console for debug purposes
   printf( "MIDI_LoadFile returned: %d\n", midiStatus );

   // if no banks were passed, then load the default
   if ( numPatchFiles == 0 ) {
      oplStatus = OPL3_LoadPatchBank( "DEFAULT.BNK", FALSE );

   } else {
      // otherwise, load the banks
      for ( i = 0; i < numPatchFiles; i++ ) {
         // TODO: Handle load failures
         oplStatus = OPL3_LoadPatchBank( argv[ patchFileIndex + i ], i != 0 );
      }
   }

   // set the End time for the MIDI if one was provided
   if ( endTimeSec ) {
      MIDI_SetPlayTime( endTimeSec );
   }
   
   // test playing
   if ( midiStatus == MIDI_OK ) {
      MIDI_Play();
      // loop while we wait for the player to finish
      do {
         // have the MIDI driver service itself if it needs to
         MIDI_Update();
         
         // abort if a key is pressed
         if ( kbhit() ) {
            key = getch();

            // break on any key
            break;
         }
      } while ( MIDI_IsPlaying() );

   }
   // shut down the player when done
   MIDI_ShutDown();

   return 0;
}