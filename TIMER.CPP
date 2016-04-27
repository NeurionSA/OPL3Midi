/********************************************************************
**
** TIMER.CPP
**
** Timer Functions
**
********************************************************************/

#include <dos.h>        // for interrupt vectors and chaining
#include <conio.h>      // for hardware port I/O
#include "timer.h"
#include "globals.h"

// use the Timer namespace
namespace Timer {

/******** CONSTANTS ********/
#define  MAX_TIMERS        4     // max number of distinct timers supported
#define  OVERFLOW_MASK     0x80000000  // bitmask used to check for timer overflow (uppermost bit set)

/******** STRUCTS ********/
// structure that holds the state of each timer
typedef struct TimerState {
   bool     inUse;            // if this timer has been registered for use
   bool     isRunning;        // if this timer is currently running
   bool     overflowed;       // if the timer has overflowed and is no longer updating
   UInt32   elapsedTicks;     // count of elapsed PIT ticks since the TIMER's tick count was last checked
   UInt32   tickRate;         // number of PIT ticks per timer tick
} TimerState;

/******** FUNCTION DECLARATIONS ********/
// (for functions not declared in the header)
void __interrupt __far timerHandler ();   // handler for the PIT interrupt

/******** VARIABLES ********/
void ( __interrupt __far *biosTimerHandler )();  // function pointer for the BIOS PIT interrupt handler
volatile UInt32   biosClockTicks;   // counter of elapsed clock ticks for chaining to the original PIT interrupt
UInt32      timerReload;         // the value reloaded into the timer on each tick
volatile TimerState timers[ MAX_TIMERS ];   // the states of all the timers

bool        inited = false;      // whether the timer has been initialized

/******** FUNCTION DEFINITIONS ********/

// PIT interrupt handler
void __interrupt __far timerHandler () {
   UInt16      i;    // for-loop iterator
   
   // iterate through the timers and update their states
   for ( i = 0; i < MAX_TIMERS; i++ ) {
      // if they're in use, running, and haven't overflowed...
      if ( timers[ i ].inUse && timers[ i ].isRunning && !timers[ i ].overflowed ) {
         // increment the elapsed ticks by the global tick rate
         timers[ i ].elapsedTicks += timerReload;
         // check if it's overflowed (uppermost bit set)
         if ( timers[ i ].elapsedTicks & OVERFLOW_MASK ) timers[ i ].overflowed = true;
      }
   }
   
   // increment the tick counter by the value we loaded into the timer
   biosClockTicks += timerReload;
   // should we also call the BIOS handler?
   if ( biosClockTicks >= 0x10000 ) {
      // decrement the value
      biosClockTicks -= 0x10000;
      
      // call the BIOS handler
      _chain_intr( biosTimerHandler );
   } else {
      // we didn't call the BIOS handler so acknowledge the interrupt
      outp( 0x20, 0x20 );
   }
}

/* ------------------------------------------------------------------
** Initializes the Timer driver with the desired reload rate (accuracy)
**
** UInt16   rate        Desired overall reload rate (smaller is faster)
*/
STATUS   Init ( UInt16 rate ) {
   UInt16      i;    // for-loop iterator
   
   // return if it's already been initialized
   if ( inited ) return ( ERR_GENERIC );
   
   // set the initial values for everything
   biosClockTicks = 0;
   timerReload = rate;
   if ( timerReload == 0 ) timerReload = 0x10000;
   
   // reset the states of all the timers
   for ( i = 0; i < MAX_TIMERS; i++ ) {
      timers[ i ].inUse = false;
      timers[ i ].isRunning = false;
   }
   
   // save the current DOS interrupt timer handler (vector 0x08)
   biosTimerHandler = _dos_getvect( 0x08 );
   
   // set the interrupt vector and load the PIT with the reload value
   _disable();
   _dos_setvect( 0x08, timerHandler );
   outp( 0x43, 0x34 );
   outp( 0x40, timerReload & 0xFF );   // write lo-byte
   outp( 0x40, timerReload >> 8 );     // write hi-byte
   _enable();
   
   // return success
   inited = true;
   return ( OK );
}

/* ------------------------------------------------------------------
** Uninitializes the driver, restoring default PIT behavior
**
*/
STATUS   Uninit () {
   // return if driver's not been initialized
   if ( !inited ) return ( ERR_NOT_INITED );
   
   // disable interrupts while we reprogram the PIT
   _disable();
   // set the reload value to default
   outp( 0x43, 0x34 );
   outp( 0x40, 0x00 );
   outp( 0x40, 0x00 );
   // restore the BIOS vector
   _dos_setvect( 0x08, biosTimerHandler );
   // re-enable interrupts
   _enable();
   
   // return success
   inited = false;
   return ( OK );
}

/* ------------------------------------------------------------------
** Creates a new timer with the desired reload rate
**
** UInt32   rate     The reload rate for the new timer
** UInt16 * hTimer   -> Variable to receive the handle for the new timer
*/
STATUS   CreateTimer ( UInt32 rate, UInt16 * hTimer ) {
   UInt16      i;          // for-loop iterator
   UInt16      newT = MAX_TIMERS;  // index of the new timer
   
   // return if driver's not been initialized
   if ( !inited ) return ( ERR_NOT_INITED );
   // if rate is 0 return an error
   if ( rate == 0 ) return ( ERR_BAD_ARGUMENT );
   
   // iterate through the list of timers until we find a free one
   for ( i = 0; i < MAX_TIMERS; i++ ) {
      if ( !timers[ i ].inUse ) {
         newT = i;
         break;
      }
   }
   
   // if no free timer was found, return
   if ( newT == MAX_TIMERS ) return ( ERR_MAX_TIMERS );
   
   // set up the new timer, making sure to set its In0use state LAST
   // (in case the interrupt occurs in the middle of this)
   timers[ newT ].elapsedTicks = 0;
   timers[ newT ].tickRate = rate;
   timers[ newT ].isRunning = false;
   timers[ newT ].overflowed = false;
   timers[ newT ].inUse = true;
   
   // set the timer handler
   *hTimer = newT;
   
   // return success
   return ( OK );
}

/* ------------------------------------------------------------------
** Frees an existing timer
**
** UInt16   hTimer   Handle of the timer to free
*/
STATUS   FreeTimer ( UInt16 hTimer ) {
   // return if driver's not been initialized
   if ( !inited ) return ( ERR_NOT_INITED );
   // return if the timer handle is out-of-bounds
   if ( hTimer >= MAX_TIMERS ) return ( ERR_BAD_ARGUMENT );
   
   // clear the in-use flag of the timer
   if ( timers[ hTimer ].inUse ) {
      timers[ hTimer ].inUse = false;
      
   } else {
      // it wasn't in use to begin with
      return ( ERR_GENERIC );
   }
   
   // return success
   return ( OK );
}

/* ------------------------------------------------------------------
** Starts an existing timer
**
** UInt16   hTimer   Handle of the timer to start
*/
STATUS   StartTimer ( UInt16 hTimer ) {
   // return if driver's not been initialized
   if ( !inited ) return ( ERR_NOT_INITED );
   // return if the timer handle is out-of-bounds
   if ( hTimer >= MAX_TIMERS ) return ( ERR_BAD_ARGUMENT );
   // return if the timer isn't allocated
   if ( !timers[ hTimer ].inUse ) return ( ERR_GENERIC );
   
   // clear the elapsed ticks and set the running flag
   timers[ hTimer ].elapsedTicks = 0;
   timers[ hTimer ].overflowed = false;
   timers[ hTimer ].isRunning = true;
   
   // return success
   return ( OK );
}

/* ------------------------------------------------------------------
** Stops an existing timer
**
** UInt16   hTimer   Handle of the timer to stop
*/
STATUS   StopTimer ( UInt16 hTimer ) {
   // return if driver's not been initialized
   if ( !inited ) return ( ERR_NOT_INITED );
   // return if the timer handle is out-of-bounds
   if ( hTimer >= MAX_TIMERS ) return ( ERR_BAD_ARGUMENT );
   // return if the timer isn't allocated
   if ( !timers[ hTimer ].inUse ) return ( ERR_GENERIC );
   
   // clear the running flag
   timers[ hTimer ].isRunning = false;
   
   // return success
   return ( OK );
}

/* ------------------------------------------------------------------
** Gets (and resets) the number of ticks for an existing timer
**
** UInt16   hTimer      Handle of the timer to query
** UInt32 * numTicks    -> variable to receive the number of ticks
*/
STATUS   GetTimerTicks ( UInt16 hTimer, UInt32 * numTicks ) {
   UInt32      elapsed;    // elapsed ticks copied from the timer struct
   
   // return if driver's not been initialized
   if ( !inited ) return ( ERR_NOT_INITED );
   // return if the timer handle is out-of-bounds
   if ( hTimer >= MAX_TIMERS ) return ( ERR_BAD_ARGUMENT );
   // return if the timer isn't allocated
   if ( !timers[ hTimer ].inUse ) return ( ERR_GENERIC );
   
   // copy the elapsed ticks from the timer struct and work with that (in case interrupts occur)
   elapsed = timers[ hTimer ].elapsedTicks;
   // reset the elapsed ticks (by taking the modulus of the tick rate)
   timers[ hTimer ].elapsedTicks = timers[ hTimer ].elapsedTicks % timers[ hTimer ].tickRate;
   
   // return the number of ticks have elapsed for the timer (rounded down)
   *numTicks = elapsed / timers[ hTimer ].tickRate;
   // clear the overflow flag if it was set
   timers[ hTimer ].overflowed = false;   
   
   // return success
   return ( OK );
}

/* ------------------------------------------------------------------
** Changes the reload rate of an existing timer
**
** UInt16   hTimer      Handle of the timer to change
** UInt32   rate        New reload rate for the timer
*/
STATUS   SetTimerRate ( UInt16 hTimer, UInt32 rate ) {
   // return if driver's not been initialized
   if ( !inited ) return ( ERR_NOT_INITED );
   // return if the timer handle is out-of-bounds
   if ( hTimer >= MAX_TIMERS ) return ( ERR_BAD_ARGUMENT );
   // return if the timer isn't allocated
   if ( !timers[ hTimer ].inUse ) return ( ERR_GENERIC );
   // return if the new rate is bad
   if ( rate == 0 ) return ( ERR_BAD_ARGUMENT );
   
   // set the timer's new rate
   // NOTE: The next call to GetTimerTicks may be incorrect as it doesn't currently
   // account for the reload rate change
   timers[ hTimer ].tickRate = rate;
   
   // return success
   return ( OK );
}

};    // end Timer namespace