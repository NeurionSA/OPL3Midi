/********************************************************************
**
** DPMI.CPP
**
** DPMI Service Functions
**
********************************************************************/

#include <dos.h>        // for interrupt calls
#include "dpmi.h"
#include "globals.h"    // for data types

// use the DPMI namespace
namespace DPMI {

/******** FUNCTION DECLARATIONS ********/
// (for functions not declared in the header)

/******** VARIABLES ********/

/******** FUNCTION DEFINITIONS ********/

/* ------------------------------------------------------------------
** Allocates Memory from the DOS free memory pool
**
** UInt32   size        Desired number of bytes to allocate
** UInt16 * p_RMSeg     -> Ret: Real-Mode Segment of the allocated block
** UInt16 * p_PMSel     -> Ret: P-Mode base selector of allocated block
*/
STATUS   AllocateDOSMemory ( UInt32 size, UInt16 * p_RMSeg, UInt16 * p_PMSel ) {
   union REGS     regs;
   
   // set the registers for the DPMI call
   regs.w.ax = 0x0100;
   regs.w.bx = (size | 15) >> 4; // number of paragraphs (16 byte chunks)
   // perform the interrupt
   int386( 0x31, &regs, &regs );
   // if carry flag is set, call has failed
   if ( regs.x.cflag ) return ( ERR_Generic );
   
   // return the real-mode segment and protected-mode selector we got
   *p_RMSeg = regs.w.ax;
   *p_PMSel = regs.w.dx;   
   
   // return success
   return ( OK );
}

/* ------------------------------------------------------------------
** Frees DOS memory allocated by the previous function
**
** UInt16   PMSel       Selector of block to be freed
*/
STATUS   FreeDOSMemory ( UInt16 PMSel ) {
   union REGS     regs;
   
   // set the registers for the DPMI call
   regs.w.ax = 0x0101;
   regs.w.dx = PMSel;
   // perform the interrupt
   int386( 0x31, &regs, &regs );
   // if carry flag is set, call has failed
   if ( regs.x.cflag ) return ( ERR_Generic );
   
   // return success
   return ( OK );
}

/* ------------------------------------------------------------------
** Locks a Linear Address Range
**
** UInt32   address     Start of linear address to lock
** UInt32   size        Size of the range to lock
*/
STATUS   LockLinearAddressRange ( UInt32 address, UInt32 size ) {
   union REGS     regs;
   
   // set the registers for the DPMI call
   regs.w.ax = 0x0600;
   regs.w.bx = ( address >> 16 );      // high bytes of address
   regs.w.cx = ( address & 0xFFFF );   // low bytes of address
   regs.w.si = ( size >> 16 );         // high bytes of size
   regs.w.di = ( size & 0xFFFF );      // low bytes of size
   // perform the interrupt
   int386( 0x31, &regs, &regs );
   // if carry flag is set, call has failed
   if ( regs.x.cflag ) return ( ERR_Generic );
   
   // return success
   return ( OK );
}

/* ------------------------------------------------------------------
** Unlocks a Linear Address Range
**
** UInt32   address     Start of linear address to unlock
** UInt32   size        Size of the range to unlock
*/
STATUS   UnlockLinearAddressRange ( UInt32 address, UInt32 size ) {
   union REGS     regs;
   
   // set the registers for the DPMI call
   regs.w.ax = 0x0601;
   regs.w.bx = ( address >> 16 );      // high bytes of address
   regs.w.cx = ( address & 0xFFFF );   // low bytes of address
   regs.w.si = ( size >> 16 );         // high bytes of size
   regs.w.di = ( size & 0xFFFF );      // low bytes of size
   // perform the interrupt
   int386( 0x31, &regs, &regs );
   // if carry flag is set, call has failed
   if ( regs.x.cflag ) return ( ERR_Generic );
   
   // return success
   return ( OK );
}

/* ------------------------------------------------------------------
** Maps a Physical Address to linear space
**
** UInt32   physAdd     Physical address to map
** UInt32   size        Size of the range to map
** void **  p_LinAdd    -> Ret: Linear address of mapped physical memory
*/
STATUS   MapPhysicalAddress ( UInt32 physAdd, UInt32 size, void ** p_LinAdd ) {
   union REGS     regs;
   
   // set the registers for the DPMI call
   regs.w.ax = 0x0800;
   regs.w.bx = ( physAdd & 0xFFFF0000 ) >> 16;
   regs.w.cx = ( physAdd & 0xFFFF );
   regs.w.si = ( size & 0xFFFF0000 ) >> 16;
   regs.w.di = ( size & 0xFFFF );
   // perform the interrupt
   int386( 0x31, &regs, &regs );
   // if carry flag is set, call has failed
   if ( regs.x.cflag ) return ( ERR_Generic );
   
   // return the linear address of the mapping
   *p_LinAdd = ( void * )( ( regs.w.bx << 16 ) | regs.w.cx );
   
   // return success
   return ( OK );
}

/* ------------------------------------------------------------------
** Frees a mapped Physical Address
**
** void *   linAdd      Mapped physical address to free
*/
STATUS   FreePhysicalAddress ( void * linAdd ) {
   union REGS     regs;
   
   // set the registers for the DPMI call
   regs.w.ax = 0x0801;
   regs.w.bx = ( ( UInt32 ) linAdd & 0xFFFF0000 ) >> 16;
   regs.w.cx = ( ( UInt32 ) linAdd & 0xFFFF );
   // perform the interrupt
   int386( 0x31, &regs, &regs );
   // if carry flag is set, call has failed
   if ( regs.x.cflag ) return ( ERR_Generic );
   
   // return success
   return ( OK );
}

/* ------------------------------------------------------------------
** Simulates a Real-Mode Interrupt
**
** Byte     intNum      Interrupt number
** RMCall * p_rmCall    Pointer to a RMCall real-mode call structure
*/
STATUS   SimulateRealModeInt ( Byte intNum, RMCall * p_rmCall ) {
   union REGS     regs;
   
   // set the registers for the DPMI call
   regs.w.ax = 0x0300;
   regs.h.bl = intNum;
   regs.h.bh = 1;       // reset interrupt controller and A20 line
   regs.w.cx = 0;
   regs.x.edi = ( UInt32 ) p_rmCall;
   // perform the interrupt
   int386( 0x31, &regs, &regs );
   // if carry flag is set, call has failed
   if ( regs.x.cflag ) return ( ERR_Generic );
   
   // return success
   return ( OK );
}

}; // end DPMI namespace