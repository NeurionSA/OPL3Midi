// DPMI.H
//
// DPMI include

#if !defined( DPMI_H )
#define DPMI_H

#include "globals.h"

// use the DPMI namespace
namespace DPMI {

/******** DPMI function returns ********/
typedef enum {
   OK = 0,              // function executed successfully
   ERR_Generic,         // generic error occured
} STATUS;

/******** STRUCTS ********/
// real-mode call structure for DPMI simulate real-mode interrupt function
typedef struct {
   UInt32  edi;
   UInt32  esi;
   UInt32  ebp;
   UInt32  reserved;
   UInt32  ebx;
   UInt32  edx;
   UInt32  ecx;
   UInt32  eax;
   UInt16  flags;
   UInt16  es;
   UInt16  ds;
   UInt16  fs;
   UInt16  gs;
   UInt16  ip;
   UInt16  cs;
   UInt16  sp;
   UInt16  ss;
} RMCall;

/******** DPMI functions ********/

// Allocates memory from the DOS free memory pool
STATUS   AllocateDOSMemory ( UInt32 size, UInt16 * p_RMSeg, UInt16 * p_PMSel );
// Frees DOS memory allocated by the previous function
STATUS   FreeDOSMemory ( UInt16 PMSel );
// Locks a Linear Address Range
STATUS   LockLinearAddressRange ( UInt32 address, UInt32 size );
// Unlocks a Linear Address Range
STATUS   UnlockLinearAddressRange ( UInt32 address, UInt32 size );
// Maps a Physical Address to linear space
STATUS   MapPhysicalAddress ( UInt32 physAdd, UInt32 size, void ** p_LinAdd );
// Frees a mapped Physical Address
STATUS   FreePhysicalAddress ( void * linAdd );
// Simulates a Real-Mode Interrupt
STATUS   SimulateRealModeInt ( Byte intNum, RMCall * p_rmCall );

};

#endif
