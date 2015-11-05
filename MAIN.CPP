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
#include <stdlib.h>     // for atof
#include "globals.h"
#include "midi.h"
#include "opl3.h"

/******** CONSTANTS ********/
#define  ARG_NULL       0     // no argument
#define  ARG_PATCHBANK  1     // patch bank commandline arg

// This function prints the program's usage/help
void     printUsage () {
   printf( "USAGE: PLAYMIDI filename [/P patch-bank ...]\n" );
   printf( "  %-14s %s\n", "filename", "The MIDI file to play" );
   printf( "  %-14s %s\n", "/P patch-bank [...]", "Load alternate bank from file 'patch-bank'" );
}

// Main entrypoint
int      main ( int argc, char **argv ) {
   int      midiStatus;    // return code from MIDI funcs
   int      oplStatus;     // return code from OPL3 funcs
   char     key;           // keyboard key pressed
   WORD     i;             // for-loop iterator
   BYTE     curArg;        // current argument being handled
   BOOL     baseBank;      // base bank has been loaded?
   
   // not handling any argument yet
   curArg = ARG_NULL;
   baseBank = FALSE;

   // parse the command-line to check the arguments
   // if there are too-few arguments, print the usage and exit
   if ( argc < 2 ) {
      printUsage();

   } else {
      // init the MIDI player
      MIDI_Init();

      // load the MIDI file
      midiStatus = MIDI_LoadFile( argv[ 1 ] );
      // output the result to the console for debug purposes
      printf( "MIDI_LoadFile returned: %d\n", midiStatus );
      
      // load the default bank file
      oplStatus = OPL3_LoadPatchBank( "DEFAULT.BNK", FALSE );
      
      // iterate through the remaining arguments and handle them
      for ( i = 2; i < argc; i++ ) {
         // check if this argument is a switch
         if ( argv[ i ][ 0 ] == 0x2F ) {
            // it's a switch, determine what kind it is
            // upper-case it
            strupr( argv[ i ] );
            // compare it to the expected arguments
            if ( strcmp( argv[ i ], "/P" ) == 0 ) {
               // now a patch arg
               curArg = ARG_PATCHBANK;

            } else {
               // Unknown argument
               curArg = ARG_NULL;

            }
         } else {
            // it's an argument, switch based on type
            switch ( curArg ) {
               case ARG_PATCHBANK:
                  // load patch bank
                  // TODO: Report load failures
                  OPL3_LoadPatchBank( "DEFAULT.BNK", baseBank );
                  baseBank = TRUE;
                  break;

            }

         }
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
      
   }
   
   return 0;
}