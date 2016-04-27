// TIMER.H
//
// Timer include

#if !defined ( TIMER_H )
#define TIMER_H

#include "globals.h"

// use the Timer namespace
namespace Timer {

/******** Timer function returns ********/
typedef enum {
   OK = 0,           // function executed successfully
   ERR_NOT_INITED,   // driver is not inited
   ERR_MAX_TIMERS,   // maximum number of timers reached
   ERR_BAD_ARGUMENT, // argument supplied to function is bad or out-of-bounds
   ERR_GENERIC,      // generic error
} STATUS;

/******** Timer driver functions ********/

// Initializes the Timer driver with the desired overall reload rate (accuracy)
STATUS   Init ( UInt16 rate );
// Uninitializes the driver, restoring default behavior
STATUS   Uninit ();
// Creates a new timer with the desired reload rate
STATUS   CreateTimer ( UInt32 rate, UInt16 * hTimer );
// Frees an existing timer
STATUS   FreeTimer ( UInt16 hTimer );
// Starts an existing timer
STATUS   StartTimer ( UInt16 hTimer );
// Stops an existing timer
STATUS   StopTimer ( UInt16 hTimer );
// Gets (and resets) the number of timer ticks
STATUS   GetTimerTicks ( UInt16 hTimer, UInt32 * numTicks );
// Changes the reload rate of an existing timer
STATUS   SetTimerRate ( UInt16 hTimer, UInt32 rate );

};    // end Timer namespace

#endif