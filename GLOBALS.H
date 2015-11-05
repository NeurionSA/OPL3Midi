// GLOBALS.H
//
// Global Include file

#if !defined( __GLOBALS_H )
#define __GLOBALS_H

// type definitions
typedef unsigned char   BYTE;
typedef unsigned short  WORD;    // 16 bits
typedef unsigned long   DWORD;   // 32 bits
typedef int             BOOL;    // 16 bits
typedef signed char     SBYTE;

// definition of true and false boolean values
// Yes, I know these are already defined as 'true' and 'false', but I like to do it this way
// to force their values to be consistent across any compiler
#define  TRUE   1
#define  FALSE  0

#endif