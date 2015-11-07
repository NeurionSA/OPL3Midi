/********************************************************************
**
** OPL3.CPP
**
** The OPL3 driver functions
**
********************************************************************/

#include <stdio.h>      // for file I/O
#include <conio.h>      // for hardware port I/O
#include <string.h>     // for strncmp()
#include "globals.h"
#include "opl3.h"

/******** CONSTANTS ********/
#define  OPL3_ADDR         0x220 // base I/O address of the OPL3

#define  NUM_VOICES        18    // the number of physical voices (OPL3 channels) the driver supports

#define  PAN_THRESHOLD_L   27    // if the Pan (MSB) is <= this value then voice goes hard left
#define  PAN_THRESHOLD_R   100   // if the Pan (MSB) is >= this value then voice goes hard right
#define  PAN_MASK_L        0xDF  // bitmasks for channel's pan register (right bit clear)
#define  PAN_MASK_R        0xEF  // left bit clear
#define  PAN_MASK_C        0xFF  // no bits clear

#define  MOD_THRESHOLD     0x40  // if the Modulation (MSB) is >= this value then force voice's vibrato bits on

// voice statuses
#define  VOICE_FREE        0     // voice is free for use
#define  VOICE_KEYON       1     // voice is currently in use and in Key-On

// flags for the updateVoice function
#define  UPDATE_FREQ       0x01  // update voice's frequency
#define  UPDATE_VOLUME     0x02  // update voice's volume
#define  UPDATE_PAN        0x04  // update voice's pan
#define  UPDATE_MOD        0x08  // update voice's modulation setting
#define  UPDATE_ALL        0x0F  // update all the voice's settings

/******** STRUCTS ********/
// Patch definition, loaded from a bank file
// Operator 1 = Modulator, 2 = Carrier
typedef struct PatchDef {
   // data found in patch bank definitions
   BYTE  Op1FMult;      // Op-1 reg at base 0x20 (Trem, Vib, Sust, KSR, F-Mult)
   BYTE  Op1KSAtt;      // Op-1 reg at base 0x40 (KSL, Attenuation)
   BYTE  Op1AD;         // Op-1 reg at base 0x60 (Attack Rate, Decay Rate)
   BYTE  Op1SR;         // Op-1 reg at base 0x80 (Sustain Level, Release Rate)
   BYTE  Op1WF;         // Op-1 reg at base 0xE0 (Waveform Select)
   BYTE  Op2FMult;      // Op-2 reg at base 0x20
   BYTE  Op2KSAtt;      // Op-2 reg at base 0x40
   BYTE  Op2AD;         // Op-2 reg at base 0x60
   BYTE  Op2SR;         // Op-2 reg at base 0x80
   BYTE  Op2WF;         // Op-2 reg at base 0xE0
   BYTE  ChFNum;        // Chan reg at base 0xA0 (F-Num low byte)
   BYTE  ChKBF;         // Chan reg at base 0xB0 (clear Key-On, Block Num, Freq upper bits)
   BYTE  ChPFb;         // Chan reg at base 0xC0 (Pan, Feedback, Synth)
   // additional data
   BYTE  used;          // whether the patch is used (non-zero for true; unused patches will be ignored when Key-Ons occur)
} PatchDef;

// MIDI Channel information
typedef struct MidiChannel {
   // Basic Settings
   BYTE  patch;         // Index of the patch currently assigned to the channel
   // Controller Settings
   BYTE  volume;        // Channel's volume controller (only MSB is used)
   BYTE  pan;           // Channel's pan controller (MSB only)
   BYTE  expression;    // Channel's expression controller (MSB only)
   BYTE  modulation;    // Channel's modulation controller (MSB only)
   BYTE  sustainPedal;  // Channel's sustain pedal (0 or 1)
   // Pitch Bend Settings
   WORD  pitchBend;     // Channel's raw pitch bend value
   SBYTE pbSemi;        // Channel's calculated pitch bended semi-note transposition (signed)
   BYTE  pbFrac;        // Channel's calculated pitch bended 1/16 semi-note transposition (unsigned)
   // Registered Parameter Settings
   BYTE  rpnIndexLsb;      // Channel's currently selected Registered Parameter Number (low byte)
   BYTE  rpnIndexMsb;      // Channel's currently selected RPN (high byte)
   BYTE  rpPitchBendSemi;  // Pitch Bend Sensitivity (+/- semitones)
   BYTE  rpPitchBendCent;  // Pitch Bend Sensitivity (+/- cents) (ignored for now)
} MidiChannel;

// OPL3 Voice information
typedef struct Opl3Voice {
   BYTE  status : 2;          // voice status bits (free, Key-On)
   BYTE  fMultChanged : 1;    // flag for if the voice's F-Mults were changed from the patch's default
   BYTE  channel;             // MIDI channel the voice currently represents
   BYTE  noteKey;             // MIDI note the voice currently represents
   BYTE  noteVelocity;        // velocity of the MIDI note
   BYTE  patch;               // index of the patch currently loaded into the voice
   // register shadows
   BYTE  shadowKBF;           // shadowed copy of KBF register for quick Key-Off
   BYTE  shadowFMult[ 2 ];    // shadowed copy of both operators' Trem, Vib, Sust, KSR, F-Mult registers for Modulation controller
} Opl3Voice;

/******** FUNCTION DECLARATIONS ********/
// (for functions not declared in the header)
// writes a register to the OPL3
void     writeReg ( WORD reg, BYTE data );
// sets all the registers to their initial state
void     initRegs ();
// updates the note in the specified voice to reflect changes to its frequency or volume
void     updateVoice ( BYTE v, BYTE flags );

/******** VARIABLES ********/
// lookup tables
// register offsets for the 36 OPL3 operators
WORD        opRegOffset[ 36 ] = {
   0x000, 0x001, 0x002, 0x003, 0x004, 0x005, 0x008, 0x009, 0x00A, 0x00B, 0x00C, 0x00D,
   0x010, 0x011, 0x012, 0x013, 0x014, 0x015, 0x100, 0x101, 0x102, 0x103, 0x104, 0x105,
   0x108, 0x109, 0x10A, 0x10B, 0x10C, 0x10D, 0x110, 0x111, 0x112, 0x113, 0x114, 0x115 };
// register offsets for the 18 OPL3 channels
WORD        chRegOffset[ 18 ] = {
   0x000, 0x001, 0x002, 0x003, 0x004, 0x005, 0x006, 0x007, 0x008,
   0x100, 0x101, 0x102, 0x103, 0x104, 0x105, 0x106, 0x107, 0x108 };
// channel operator pairs (each 2 entries are the operators used in the channel, first being the moderator, second the carrier)
BYTE        chOpPairs[ 36 ] = {
    0,  3,  1,  4,  2,  5,  6,  9,  7, 10,  8, 11, 12, 15, 13, 16, 14, 17,
   18, 21, 19, 22, 20, 23, 24, 27, 25, 28, 26, 29, 30, 33, 31, 34, 32, 35 };
// frequency lookup table (12 semitones, with 16 fractions)
WORD        freqTable[ 192 ] = {
   0x0159, 0x015A, 0x015B, 0x015D, 0x015E, 0x015F, 0x0160, 0x0162,
   0x0163, 0x0164, 0x0166, 0x0167, 0x0168, 0x0169, 0x016B, 0x016C,
   0x016D, 0x016F, 0x0170, 0x0171, 0x0173, 0x0174, 0x0175, 0x0177,
   0x0178, 0x0179, 0x017B, 0x017C, 0x017E, 0x017F, 0x0180, 0x0182,
   0x0183, 0x0185, 0x0186, 0x0187, 0x0189, 0x018A, 0x018C, 0x018D,
   0x018E, 0x0190, 0x0191, 0x0193, 0x0194, 0x0196, 0x0197, 0x0199,
   0x019A, 0x019C, 0x019D, 0x019F, 0x01A0, 0x01A2, 0x01A3, 0x01A5,
   0x01A6, 0x01A8, 0x01A9, 0x01AB, 0x01AC, 0x01AE, 0x01AF, 0x01B1,
   0x01B3, 0x01B4, 0x01B6, 0x01B7, 0x01B9, 0x01BA, 0x01BC, 0x01BE,
   0x01BF, 0x01C1, 0x01C2, 0x01C4, 0x01C6, 0x01C7, 0x01C9, 0x01CB,
   0x01CC, 0x01CE, 0x01D0, 0x01D1, 0x01D3, 0x01D5, 0x01D6, 0x01D8,
   0x01DA, 0x01DC, 0x01DD, 0x01DF, 0x01E1, 0x01E2, 0x01E4, 0x01E6,
   0x01E8, 0x01E9, 0x01EB, 0x01ED, 0x01EF, 0x01F1, 0x01F2, 0x01F4,
   0x01F6, 0x01F8, 0x01FA, 0x01FB, 0x01FD, 0x01FF, 0x0201, 0x0203,
   0x0205, 0x0207, 0x0208, 0x020A, 0x020C, 0x020E, 0x0210, 0x0212,
   0x0214, 0x0216, 0x0218, 0x021A, 0x021C, 0x021E, 0x0220, 0x0221,
   0x0223, 0x0225, 0x0227, 0x0229, 0x022B, 0x022D, 0x022F, 0x0231,
   0x0233, 0x0236, 0x0238, 0x023A, 0x023C, 0x023E, 0x0240, 0x0242,
   0x0244, 0x0246, 0x0248, 0x024A, 0x024C, 0x024F, 0x0251, 0x0253,
   0x0255, 0x0257, 0x0259, 0x025C, 0x025E, 0x0260, 0x0262, 0x0264,
   0x0267, 0x0269, 0x026B, 0x026D, 0x026F, 0x0272, 0x0274, 0x0276,
   0x0279, 0x027B, 0x027D, 0x027F, 0x0282, 0x0284, 0x0286, 0x0289,
   0x028B, 0x028D, 0x0290, 0x0292, 0x0295, 0x0297, 0x0299, 0x029C,
   0x029E, 0x02A1, 0x02A3, 0x02A5, 0x02A8, 0x02AA, 0x02AD, 0x02AF };
// attenuation lookup table
BYTE     attenTable[ 128 ] = {
   0x3F, 0x38, 0x30, 0x2B, 0x28, 0x25, 0x23, 0x21, 0x20, 0x1E, 0x1D, 0x1C, 0x1B, 0x1A, 0x19, 0x18,
   0x17, 0x17, 0x16, 0x16, 0x15, 0x14, 0x14, 0x13, 0x13, 0x12, 0x12, 0x11, 0x11, 0x11, 0x10, 0x10,
   0x0F, 0x0F, 0x0F, 0x0E, 0x0E, 0x0E, 0x0D, 0x0D, 0x0D, 0x0D, 0x0C, 0x0C, 0x0C, 0x0C, 0x0B, 0x0B,
   0x0B, 0x0B, 0x0A, 0x0A, 0x0A, 0x0A, 0x09, 0x09, 0x09, 0x09, 0x09, 0x08, 0x08, 0x08, 0x08, 0x08,
   0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x05, 0x05, 0x05, 0x05,
   0x05, 0x05, 0x05, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04, 0x03, 0x03, 0x03, 0x03, 0x03, 0x03,
   0x03, 0x03, 0x03, 0x02, 0x02, 0x02, 0x02, 0x02, 0x02, 0x02, 0x02, 0x01, 0x01, 0x01, 0x01, 0x01,
   0x01, 0x01, 0x01, 0x01, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 };
// semi-tone lookup table
BYTE     semiToneTable[ 128 ] = {
    0,  1,  2,  3,  4,  5,  6,  7,  8,  9, 10, 11,  0,  1,  2,  3,
    4,  5,  6,  7,  8,  9, 10, 11,  0,  1,  2,  3,  4,  5,  6,  7,
    8,  9, 10, 11,  0,  1,  2,  3,  4,  5,  6,  7,  8,  9, 10, 11,
    0,  1,  2,  3,  4,  5,  6,  7,  8,  9, 10, 11,  0,  1,  2,  3,
    4,  5,  6,  7,  8,  9, 10, 11,  0,  1,  2,  3,  4,  5,  6,  7,
    8,  9, 10, 11,  0,  1,  2,  3,  4,  5,  6,  7,  8,  9, 10, 11,
    0,  1,  2,  3,  4,  5,  6,  7,  8,  9, 10, 11,  0,  1,  2,  3,
    4,  5,  6,  7,  8,  9, 10, 11,  0,  1,  2,  3,  4,  5,  6,  7 };
// octave lookup table
// octaves < 0 means right-shift the frequency by abs(octave) and use block num 0
// octaves > 7 require the two operators F-mult values to be changed reflect a right shift of the excess
char     octaveTable[ 128 ] = {
   -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,  0,  0,  0,  0,
    0,  0,  0,  0,  0,  0,  0,  0,  1,  1,  1,  1,  1,  1,  1,  1,
    1,  1,  1,  1,  2,  2,  2,  2,  2,  2,  2,  2,  2,  2,  2,  2,
    3,  3,  3,  3,  3,  3,  3,  3,  3,  3,  3,  3,  4,  4,  4,  4,
    4,  4,  4,  4,  4,  4,  4,  4,  5,  5,  5,  5,  5,  5,  5,  5,
    5,  5,  5,  5,  6,  6,  6,  6,  6,  6,  6,  6,  6,  6,  6,  6,
    7,  7,  7,  7,  7,  7,  7,  7,  7,  7,  7,  7,  8,  8,  8,  8,
    8,  8,  8,  8,  8,  8,  8,  8,  9,  9,  9,  9,  9,  9,  9,  9 };
// frequency multiplier doubling lookup table
// the index put in will spit out the new F-Mult to double the effective freq
// note that higher F-Mults will not be able to reach the freqs they need
BYTE     freqMultDouble[ 16 ] = {
    1,  2,  4,  6,  8, 10, 12, 15,
   15, 15, 15, 15, 15, 15, 15, 15 };
// initial register status table
BYTE     initRegsTable[ 512 ] = {
   0x00, 0x20, 0x00, 0x00, 0x60, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, // 0x000
   0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, // 0x010
   0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, // 0x020
   0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, // 0x030
   0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, // 0x040
   0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, // 0x050
   0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, // 0x060
   0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, // 0x070
   0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, // 0x080
   0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, // 0x090
   0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, // 0x0A0
   0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xC0, 0x00, 0x00, // 0x0B0
   0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, // 0x0C0
   0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, // 0x0D0
   0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, // 0x0E0
   0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, // 0x0F0
   0x00, 0x00, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, // 0x100
   0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, // 0x110
   0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, // 0x120
   0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, // 0x130
   0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, // 0x140
   0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, // 0x150
   0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, // 0x160
   0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, // 0x170
   0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, // 0x180
   0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, // 0x190
   0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, // 0x1A0
   0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, // 0x1B0
   0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, // 0x1C0
   0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, // 0x1D0
   0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, // 0x1E0
   0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 };  // 0x1F0

// driver state
PatchDef       patches[ 256 ];   // Patch definitions for the 128 melodic and 128 percussion instruments
MidiChannel    channels[ 16 ];   // Info and current state of the 16 MIDI channels
Opl3Voice      voices[ NUM_VOICES ];	// Info and current state of the OPL3 channels
BYTE           nextVoice;        // index of the next voice to try for a Key-On
BYTE           numVoicesUsed;    // the number of voices currently in use

/******** FUNCTION DEFINITIONS ********/

// writes a register to the OPL3
void     writeReg ( WORD reg, BYTE data ) {
   // if the high byte of reg is clear, write to the base
	if ( reg & 0x100 ) {
      // write to the extended registers
		outp( OPL3_ADDR + 2, reg & 0xFF );
		outp( OPL3_ADDR + 3, data );
	} else {
      // write to the base registers
		outp( OPL3_ADDR, reg & 0xFF );
		outp( OPL3_ADDR + 1, data );
	}
}

// Sets all the registers to their initial state
void     initRegs () {
   int      i;    // for-loop iterator
   
   for ( i = 0; i < 512; i++ ) {
      writeReg( i, initRegsTable[ i ] );
   }
}

// updates a voice to reflect changes to its note frequency, volume, pan, etc.
//    BYTE     v = the index of the voice to update
//    BYTE     flags = flags that determine what will be updated (for quicker execution)
void     updateVoice ( BYTE v, BYTE flags ) {
   BYTE     i;          // base index into the chOpPairs table for this voice
   WORD     regOff;     // cached register offset
   
   // set the base index for operator pairs (voice * 2)
   i = v << 1;
   
   // if the volume flag is set
   if ( flags & UPDATE_VOLUME ) {
      WORD     atten;      // attenuation to use for the note
      WORD     baseAtten;  // base attenuation to use

      // determine the value that should be used for the carrier's attenuation
      // calculate base attenuation (note velocity, channel volume, channel expression)
      baseAtten = attenTable[ voices[ v ].noteVelocity ] +
         attenTable[ channels[ voices[ v ].channel ].volume ] +
         attenTable[ channels[ voices[ v ].channel ].expression ];
      // do the carrier's attenuation first
      atten = ( patches[ voices[ v ].patch ].Op2KSAtt & 0x3F ) + baseAtten;
      // trim it to the max (0x3F)
      if ( atten > 0x3F ) atten = 0x3F;
      // write the carrier's attenuation register
      regOff = opRegOffset[ chOpPairs[ i + 1 ] ];
      writeReg( 0x40 + regOff, ( patches[ voices[ v ].patch ].Op2KSAtt & 0xC0 ) | atten );

      // if the patch is AM then we need to write to the other operator, too
      if ( patches[ voices[ v ].patch ].ChPFb & 0x01 ) {
         // the bit is set, so it's AM-synthesis
         atten = ( patches[ voices[ v ].patch ].Op1KSAtt & 0x3F ) + baseAtten;
         // trim it to the max (0x3F)
         if ( atten > 0x3F ) atten = 0x3F;
         // write the modulator's attenuation register
         regOff = opRegOffset[ chOpPairs[ i ] ];
         writeReg( 0x40 + regOff, (patches[ voices[ v ].patch ].Op1KSAtt & 0xC0) | atten );
      }
   }  // END UPDATE_VOLUME
   
   // if the pan flag is set
   if ( flags & UPDATE_PAN ) {
      BYTE     panMask;    // pan mask to use

      // determine the pan mask to use
      if ( channels[ voices[ v ].channel ].pan <= PAN_THRESHOLD_L ) {
         panMask = PAN_MASK_L;
      } else if ( channels[ voices[ v ].channel ].pan >= PAN_THRESHOLD_R ) {
         panMask = PAN_MASK_R;
      } else {
         panMask = PAN_MASK_C;
      }
      // update the register
      writeReg( 0xC0 + chRegOffset[ v ], patches[ voices[ v ].patch ].ChPFb & panMask );
   }  // END UPDATE_PAN

   // if the modulation update flag is set
   if ( flags & UPDATE_MOD ) {
      // determine what state the operators' Vibrato flag should be based on the channel's modulation controller setting
      if ( channels[ voices[ v ].channel ].modulation >= MOD_THRESHOLD ) {
         // set the vibrato bits on both operators
         voices[ v ].shadowFMult[ 0 ] = voices[ v ].shadowFMult[ 0 ] | 0x40;
         voices[ v ].shadowFMult[ 1 ] = voices[ v ].shadowFMult[ 1 ] | 0x40;
         // write the registers
         regOff = opRegOffset[ chOpPairs[ i ] ];
         writeReg( 0x20 + regOff, voices[ v ].shadowFMult[ 0 ] );
         regOff = opRegOffset[ chOpPairs[ i + 1 ] ];
         writeReg( 0x20 + regOff, voices[ v ].shadowFMult[ 1 ] );

      } else {
         // use the patch's definition
         voices[ v ].shadowFMult[ 0 ] = ( voices[ v ].shadowFMult[ 0 ] & 0xBF ) | ( patches[ voices[ v ].patch ].Op1FMult & 0x40 );
         voices[ v ].shadowFMult[ 1 ] = ( voices[ v ].shadowFMult[ 1 ] & 0xBF ) | ( patches[ voices[ v ].patch ].Op2FMult & 0x40 );
         // write the registers
         regOff = opRegOffset[ chOpPairs[ i ] ];
         writeReg( 0x20 + regOff, voices[ v ].shadowFMult[ 0 ] );
         regOff = opRegOffset[ chOpPairs[ i + 1 ] ];
         writeReg( 0x20 + regOff, voices[ v ].shadowFMult[ 1 ] );

      }
   }  // END UPDATE_MOD
   
   // if the frequency flag is set
   if ( flags & UPDATE_FREQ ) {
      BYTE     regFNum;    // value to write for voice's F-Num low reg
      BYTE     regKBF;     // value to write for the voice's Key-On, Block Num, Freq hi reg
      WORD     freq;       // the calculated frequency of the note
      int      semiTone;   // the calculated semi-tone of the note
      SBYTE    octave;     // the calculated octave of the note
      BYTE     fMult[ 2 ];   // new F-Mult values (if they need to change)

      // determine the values to write for the channel's frequency and block bytes
      if ( voices[ v ].channel == 9 ) {
         // it's the percussion channel so we just get that straight from the patch
         regFNum = patches[ voices[ v ].patch ].ChFNum;
         regKBF = patches[ voices[ v ].patch ].ChKBF;
         
      } else {
         // it's melodic, so we gotta calculate frequency and block number
         // pitch bend has been pre-calculated for the channel to make this faster
         octave = octaveTable[ voices[ v ].noteKey ];
         semiTone = semiToneTable[ voices[ v ].noteKey ] + channels[ voices[ v ].channel ].pbSemi;
         // normalize semiTone to 0 - 11 and alter octave if necessary
         while ( semiTone < 0 ) {
            semiTone += 12;
            octave--;
         }
         while ( semiTone >= 12 ) {
            semiTone -= 12;
            octave++;
         }
         
         // get the note's frequency from the table
         freq = freqTable[ ( semiTone << 4 ) + channels[ voices[ v ].channel ].pbFrac ];
         // restore the F-Mults to default if they should be normal now (octave is no longer one that needs F-Mult changes)
         if ( ( octave < 8 ) && voices[ v ].fMultChanged ) {
            voices[ v ].shadowFMult[ 0 ] = (voices[ v ].shadowFMult[ 0 ] & 0xF0) | ( patches[ voices[ v ].patch ].Op1FMult & 0x0F );
            voices[ v ].shadowFMult[ 1 ] = (voices[ v ].shadowFMult[ 1 ] & 0xF0) | ( patches[ voices[ v ].patch ].Op2FMult & 0x0F );
            regOff = opRegOffset[ chOpPairs[ i ] ];
            writeReg( regOff + 0x20, voices[ v ].shadowFMult[ 0 ] );
            regOff = opRegOffset[ chOpPairs[ i + 1 ] ];
            writeReg( regOff + 0x20, voices[ v ].shadowFMult[ 1 ] );
            // clear the change flag
            voices[ v ].fMultChanged = 0;
         }
         // alter the frequency and F-Mults (if necessary) if the octave is outside of normal bounds
         if ( octave < 0 ) {
            // alter the frequency if the octave is negative
            freq = freq >> -octave;
            octave = 0;
            
         } else if ( octave > 7 ) {
            // alter the F-Mult values if the octave is too high
            // determine what the new F-Mult values should be
            fMult[ 0 ] = patches[ voices[ v ].patch ].Op1FMult & 0xF;
            fMult[ 1 ] = patches[ voices[ v ].patch ].Op2FMult & 0xF;
            do {
               fMult[ 0 ] = freqMultDouble[ fMult[ 0 ] ];
               fMult[ 1 ] = freqMultDouble[ fMult[ 1 ] ];
               octave--;
            } while ( octave > 7 );
            // update the shadows
            voices[ v ].shadowFMult[ 0 ] = ( voices[ v ].shadowFMult[ 0 ] & 0xF0 ) | fMult[ 0 ];
            voices[ v ].shadowFMult[ 1 ] = ( voices[ v ].shadowFMult[ 1 ] & 0xF0 ) | fMult[ 1 ];
            
            // write the new F-Mult register values
            regOff = opRegOffset[ chOpPairs[ i ] ];
            writeReg( regOff + 0x20, voices[ v ].shadowFMult[ 0 ] );
            regOff = opRegOffset[ chOpPairs[ i + 1 ] ];
            writeReg( regOff + 0x20, voices[ v ].shadowFMult[ 1 ] );
            // record that the F-Mults have changed for this voice
            voices[ v ].fMultChanged = 1;
         }
         
         // now we just need to make the two registers
         regFNum = freq & 0xFF;
         regKBF = ( freq >> 8 ) | ( octave << 2 );
   
      }  // END if ( voices[ v ].channel == 9 )
      
      // write the registers and KEY-ON the note
      regOff = chRegOffset[ v ];
      writeReg( 0xA0 + regOff, regFNum );
      writeReg( 0xB0 + regOff, regKBF | 0x20 );
   
      // shadow the KBF in the voice's data for quick Key-Off
      voices[ v ].shadowKBF = regKBF;
   }  // END UPDATE_FREQ
   
}

// Initializes the OPL3 driver (must be called before any other functions)
// Returns an error code on failure
int      OPL3_Init () {
   int      i;    // for-loop iterator
   
   // initialize the OPL3 driver by resetting the registers
   initRegs();
   
   // set all patches to unused
   for ( i = 0; i < 256; i++ ) {
      patches[ i ].used = 0;
   }
   
   // init all voices
   for ( i = 0; i < NUM_VOICES ; i++ ) {
      // set the voice to free
      voices[ i ].status = VOICE_FREE;
      // set the patch to 0xFF, so a patch load will likely be forced for next key-on
      voices[ i ].patch = 0xFF;
   }
   
   // reset all MIDI channels to default states
   for ( i = 0; i < 16; i++ ) {
      // patch
      channels[ i ].patch = 0;            // Grand Piano
      // controller settings
      channels[ i ].volume = 100;         // ~80%
      channels[ i ].pan = 0x40;           // centered pan
      channels[ i ].expression = 0x7F;    // full expression
      channels[ i ].modulation = 0;       // no modulation effect
      channels[ i ].sustainPedal = 0;     // no sustain
      // pitch bend settings
      channels[ i ].pitchBend = 0x2000;   // centered pitch bend
      channels[ i ].pbSemi = 0;
      channels[ i ].pbFrac = 0;
      // registered parameter settings
      channels[ i ].rpnIndexLsb = 0x7F;   // Null RPN index
      channels[ i ].rpnIndexMsb = 0x7F;   // Null RPN index
      channels[ i ].rpPitchBendSemi = 0x2;   // 2 semitones
      channels[ i ].rpPitchBendCent = 0x0;   // 0 cents
   }
   
   // set the next voice to 0
   nextVoice = 0;
   // set the number of used voices to 0
   numVoicesUsed = 0;
   
   // return success
   return ( OPL3_OK );
}

// Loads an OPL3 patch bank from a file
//    char *   fileName       Name and optional path to the file to be loaded
//    BOOL     overlay        Whether the patch is loaded as a base (false) or over a previous load (true)
// Returns an error code on failure
int      OPL3_LoadPatchBank ( char * fileName, BOOL overlay ) {
   FILE *   hFile;         // handle for the bank file
   char     magicNum[4];   // scratchpad for magic num
   BYTE     patchIndex;    // patch index
   WORD     numPatches;    // number of patches to load
   WORD     i;             // for-loop iterator
   
   // open the patch bank
   hFile = fopen( fileName, "rb" );
   
   // if null there was an error
   if ( hFile == NULL ) return ( OPL3_ERR_FILE_OPEN );
   
   // check the magic num
   fread( magicNum, 4, 1, hFile );
   if ( strncmp( magicNum, "OBNK", 4 ) != 0 ) return ( OPL3_ERR_FILE_BAD );
   
   // read the number of patches
   fread( &numPatches, sizeof( numPatches ), 1, hFile );
   
   // set existing patches to unused if not overlay mode
   if ( !overlay ) {
      for ( i = 0; i < 256; i++ ) {
         patches[ i ].used = 0;
      }
   }
   
   // load the patches
   for ( i = 0; i < numPatches; i++ ) {
      // read the patch number
      fread( &patchIndex, sizeof( patchIndex ), 1, hFile );
      
      // load the patch into the appropriate index
      // note that the organization of the patch structure's first 13 bytes matches that of the file
      fread( &patches[ patchIndex ], 13, 1, hFile );
      patches[ patchIndex ].used = 1;
   }
   
   // close the file
   fclose( hFile );
   
   // return success
   return ( OPL3_OK );
}

// sends a Note-Off command to the driver
void     OPL3_NoteOff ( BYTE chan, BYTE key, BYTE velocity ) {
   BYTE     v;          // index of the voice we're checking/using
   BYTE     i;          // for-loop iterator
   
   // if there are no active voices, for whatever reason, just return
   if ( numVoicesUsed == 0 ) return;
   // if the sustain pedal is active for this channel then ignore the Note-Off
   if ( channels[ chan ].sustainPedal ) return;
   
   // we won't bother cycling and will just search for the note sequentially
   v = 0xFF;   // sentinel value
   for ( i = 0; i < NUM_VOICES; i++ ) {
      // skip voices that aren't on
      if ( voices[ i ].status != VOICE_KEYON ) continue;
      // skip if channel doesn't match
      if ( voices[ i ].channel != chan ) continue;
      // check if note matches
      if ( voices[ i ].noteKey == key ) {
         v = i;
         // break out of the loop
         break;
      }
   }
   
   // if the voice is still the sentinel value, the note was not found so return
   if ( v == 0xFF ) return;
   
   // set the voice's status to free
   voices[ v ].status = VOICE_FREE;
   // decrement the number of used voices
   numVoicesUsed--;
   // write the shadowed value to the register for quick Key-Off
   writeReg( 0xB0 + chRegOffset[ v ], voices[ v ].shadowKBF );
}

// sends a Note-On command to the driver
void     OPL3_NoteOn ( BYTE chan, BYTE key, BYTE velocity ) {
   BYTE     v;          // index of the voice we're checking/using
   BYTE     p;          // index of the patch the new note needs to use
   BYTE     i;          // base index into the chOpPairs table for this voice
   WORD     regOff;     // cached register offset
   
   // if the velocity is 0, then treat it as a Note-Off with a velocity of 0x40
   if ( velocity == 0 ) {
      OPL3_NoteOff( chan, key, 0x40 );
      return;
   }
   
   // if we've reached the maximum number of used voices, ignore this Note-On
   // NOTE: We may wish to instead determine the oldest used voice and kill it instead
   if ( numVoicesUsed == NUM_VOICES ) return;
   
   // determine what patch the voice should use
   // channel 9 (percussion) uses the new note's key with percussion bit set
   if ( chan == 9 ) {
      p = key | 0x80;
   } else {
      p = channels[ chan ].patch;
   }
   
   // ignore this command if the note's patch is unused
   if ( !patches[ p ].used ) return;
   
   // cycle through the voices to find a free one
   v = nextVoice;
   do {
      // break out of the loop if this voice is free
      if ( voices[ v ].status == VOICE_FREE ) break;
      // increment the voice index, wrapping if necessary
      v++;
      if ( v >= NUM_VOICES ) v -= NUM_VOICES;
   } while ( 1 );
   
   // check if the voice has the same patch as the new note
   if ( voices[ v ].patch != p ) {
      // set the base index for operator pairs (voice * 2)
      i = v << 1;
      
      // patch doesn't match, patch needs to be loaded into this voice
      // get the register offset for operator 1 (Modulator) and write its regs
      regOff = opRegOffset[ chOpPairs[ i ] ];
      writeReg( 0x20 + regOff, patches[ p ].Op1FMult );
      writeReg( 0x40 + regOff, patches[ p ].Op1KSAtt );
      writeReg( 0x60 + regOff, patches[ p ].Op1AD );
      writeReg( 0x80 + regOff, patches[ p ].Op1SR );
      writeReg( 0xE0 + regOff, patches[ p ].Op1WF );
      
      // get the register offset for operator 2 (Carrier) and write its regs
      regOff = opRegOffset[ chOpPairs[ i + 1 ] ];
      writeReg( 0x20 + regOff, patches[ p ].Op2FMult );
      // carrier's Attenuation register will be written later
      writeReg( 0x60 + regOff, patches[ p ].Op2AD );
      writeReg( 0x80 + regOff, patches[ p ].Op2SR );
      writeReg( 0xE0 + regOff, patches[ p ].Op2WF );
   
      // set the voice's patch index
      voices[ v ].patch = p;
      // clear the voice's fMultChanged bit
      voices[ v ].fMultChanged = 0;

      // shadow necessary registers
      voices[ v ].shadowFMult[ 0 ] = patches[ p ].Op1FMult;
      voices[ v ].shadowFMult[ 1 ] = patches[ p ].Op2FMult;
   }
   
   // update the voice's information
   voices[ v ].status = VOICE_KEYON;
   voices[ v ].channel = chan;
   voices[ v ].noteKey = key;
   voices[ v ].noteVelocity = velocity;
   
   // call the updateVoice function on the voice, updating all
   updateVoice( v, UPDATE_ALL );
   
   // increment the number of used voices
   numVoicesUsed++;
   // cycle the next voice index
   nextVoice = v + 1;
   if ( nextVoice >= NUM_VOICES ) nextVoice -= NUM_VOICES;
}

// sends an Aftertouch Key command to the driver
void     OPL3_AftertouchKey ( BYTE chan, BYTE key, BYTE pressure ) {
   // because I don't know what these events actually do and these are fairly rare they are ignored
}

// sends an Aftertouch Channel command to the driver
void     OPL3_AftertouchChan ( BYTE chan, BYTE pressure ) {
   // because I don't know what these events actually do and these are fairly rare they are ignored
}

// sends a Controller Change command to the driver
void     OPL3_ControllerChange ( BYTE chan, BYTE number, BYTE value ) {
   BYTE     i;       // for-loop iterator
   
   // branch based on which controller was changed
   switch ( number ) {
      case 0x01:  // Modulation Wheel (MSB)
         // set the channel's modulation
         channels[ chan ].modulation = value;
         // loop through all the voices to find any active notes on this channel
         for ( i = 0; i < NUM_VOICES; i++ ) {
            // skip inactive notes
            if ( voices[ i ].status != VOICE_KEYON ) continue;
            if ( voices[ i ].channel == chan ) {
               // update this voice's modulation
               updateVoice( i, UPDATE_MOD );
            }
         }
         break;

      case 0x06:  // Data Entry (MSB)
      case 0x26:  // Data Entry (LSB)
         // because we may need to react to any RPN change, the code is shared here
         // ignore any changes to RPN indexes other than 0x00--
         if ( channels[ chan ].rpnIndexMsb != 0x00 ) break;
         // branch based on the LSB of the index
         switch ( channels[ chan ].rpnIndexLsb ) {
            case 0x00:  // Pitch Bend Sensitivity
               if ( number == 0x06 ) {
                  // MSB (semitones)
                  channels[ chan ].rpPitchBendSemi = value;
               } else {
                  // LSB (cents)
                  channels[ chan ].rpPitchBendCent = value;
               }
               // TODO: Update frequencies of any active notes on this channel IF the Pitch Bend is currently not default
               // May not need to occur because I've yet to see a MIDI file that sets this after any notes play
               break;

            default:
               // Unhandled RPNs:
               // 0x00, 0x01  Channel Fine Tuning
               // 0x00, 0x02  Channel Coarse Tuning
               // 0x00, 0x03  Tuning Program Change
               // 0x00, 0x04  Tuning Bank Select
               // 0x00, 0x05  Modulation Depth Range
               // and ALL 3D Stuff (MSB = 0x3D)
               break;
         }
         break;

      case 0x07:  // Channel Volume (MSB)
         // set the channel's volume
         channels[ chan ].volume = value;
         // loop through all the voices to find any active notes on this channel
         for ( i = 0; i < NUM_VOICES; i++ ) {
            // skip inactive notes
            if ( voices[ i ].status != VOICE_KEYON ) continue;
            if ( voices[ i ].channel == chan ) {
               // update this voice's volume
               updateVoice( i, UPDATE_VOLUME );
            }
         }
         break;
      
      case 0x0A:  // Pan (MSB)
         // set the channel's pan
         channels[ chan ].pan = value;
         // loop through all the voices to find any active notes on this channel
         for ( i = 0; i < NUM_VOICES; i++ ) {
            // skip inactive notes
            if ( voices[ i ].status != VOICE_KEYON ) continue;
            if ( voices[ i ].channel == chan ) {
               // update this voice's pan
               updateVoice( i, UPDATE_PAN );
            }
         }
         break;
      
      case 0x0B:  // Expression (MSB)
         // set the channel's expression
         channels[ chan ].expression = value;
         // loop through all the voices to find any active notes on this channel
         for ( i = 0; i < NUM_VOICES; i++ ) {
            // skip inactive notes
            if ( voices[ i ].status != VOICE_KEYON ) continue;
            if ( voices[ i ].channel == chan ) {
               // update this voice's volume
               updateVoice( i, UPDATE_VOLUME );
            }
         }
         break;

      case 0x40:  // 0x40  Damper Pedal On/Off
         // set the channel's sustain pedal
         if ( value >= 64 ) {
            channels[ chan ].sustainPedal = 1;
         } else {
            channels[ chan ].sustainPedal = 0;
            // sustain was just released, so Note-Off should be sent for all active notes on the channel
            for ( i = 0; i < NUM_VOICES; i++ ) {
               // skip inactive notes
               if ( voices[ i ].status != VOICE_KEYON ) continue;
               if ( voices[ i ].channel == chan ) {
                  // send the Note-Off
                  OPL3_NoteOff( chan, voices[ i ].noteKey, 0x40 );
               }
            }
         }
         break;

      case 0x64:  // Registered Parameter Number (LSB)
         // set the LSB for the channel's selected RPN
         channels[ chan ].rpnIndexLsb = value;
         break;

      case 0x65:  // Registered Parameter Number (MSB)
         // set the MSB for the channel's selected RPN
         channels[ chan ].rpnIndexMsb = value;
         break;
      
      case 0x78:  // Channel Mode - All Sound Off
         // iterate through all the voices
         for ( i = 0; i < NUM_VOICES; i++ ) {
            // skip inactive notes
            if ( voices[ i ].status != VOICE_KEYON ) continue;
            if ( voices[ i ].channel == chan ) {
               // set note's velocity to 0 and force a volume update to kill the sound
               voices[ i ].noteVelocity = 0;
               updateVoice( i, UPDATE_VOLUME );
               // send a NoteOff
               OPL3_NoteOff( chan, voices[ i ].noteKey, 0x0 );
            }
         }
         break;
      
      case 0x79:  // Channel Mode - Reset All Controllers
         // reset all the controllers on this channel
         OPL3_ResetChanControllers( chan );
         break;
      
      case 0x7B:  // Channel Mode - All Notes Off
         // perform NoteOffs on any active notes on this channel
         for ( i = 0; i < NUM_VOICES; i++ ) {
            // skip inactive notes
            if ( voices[ i ].status != VOICE_KEYON ) continue;
            if ( voices[ i ].channel == chan ) {
               // turn off the note
               OPL3_NoteOff( chan, voices[ i ].noteKey, 0x40 );
            }
         }
         break;
         
      default:
         // Unhandled/ignored controller numbers:
         // 0x00  Bank Select (MSB)
         // 0x02  Breath Controller (MSB)
         // 0x04  Foot Controller (MSB)
         // 0x05  Portamento Time (MSB)
         // 0x08  Balance (MSB)
         // 0x0C  Effect Control 1 (MSB)
         // 0x0D  Effect Control 2 (MSB)
         // 0x10  General Purpose Controller 1 (MSB)
         // 0x11  General Purpose Controller 2 (MSB)
         // 0x12  General Purpose Controller 3 (MSB)
         // 0x13  General Purpose Controller 4 (MSB)
         // 0x20  Bank Select (LSB)
         // 0x21  Modulation Wheel (LSB)
         // 0x22  Breath Controller (LSB)
         // 0x24  Foot Controller (LSB)
         // 0x25  Portamento Time (LSB)
         // 0x27  Channel Volume (LSB)
         // 0x28  Balance (LSB)
         // 0x2A  Pan (LSB)
         // 0x2B  Expression Controller (LSB)
         // 0x2C  Effect Control 1 (LSB)
         // 0x2D  Effect Control 2 (LSB)
         // 0x30  General Purpose Controller 1 (LSB)
         // 0x31  General Purpose Controller 2 (LSB)
         // 0x32  General Purpose COntroller 3 (LSB)
         // 0x33  General Purpose Controller 4 (LSB)
         // 0x41  Portamento On/Off
         // 0x42  Sostenuto On/Off
         // 0x43  Soft Pedal On/Off
         // 0x44  Legato Footswitch
         // 0x45  Hold 2
         // 0x46  Sound Variation
         // 0x47  Harmonic Intensity
         // 0x48  Release Time
         // 0x49  Attack Time
         // 0x4A  Brightness
         // 0x4B  Decay Time
         // 0x4C  Vibrato Rate
         // 0x4D  Vibrato Depth
         // 0x4E  Vibrato Delay
         // 0x4F  Sound Controller 10
         // 0x50  General Purpose Controller 5
         // 0x51  General Purpose Controller 6
         // 0x52  General Purpose Controller 7
         // 0x53  General Purpose Controller 8
         // 0x54  Portamento Control
         // 0x58  High-Resolution Velocity Prefix
         // 0x5B  Effects 1 Depth
         // 0x5C  Effects 2 Depth
         // 0x5D  Effects 3 Depth
         // 0x5E  Effects 4 Depth
         // 0x5F  Effects 5 Depth
         // 0x60  Data Increment
         // 0x61  Data Decrement
         // 0x62  Non-Registered Parameter Number (LSB)
         // 0x63  Non-Registered Parameter Number (MSB)
         // 0x7A  Channel Mode - Local Control On/Off
         // 0x7C  Channel Mode - Omni Mode Off
         // 0x7D  Channel Mode - Omnu Mode On
         // 0x7E  Channel Mode - Mono Mode On
         // 0x7F  Channel Mode - Poly Mode On
         break;
   }
}

// sends a Program Change command to the driver
void     OPL3_ProgramChange ( BYTE chan, BYTE program ) {
   // set the channel's patch
   channels[ chan ].patch = program;
}

// sends a Pitch Bend command to the driver
void     OPL3_PitchBend ( BYTE chan, BYTE lsb, BYTE msb ) {
   WORD     i;             // for-loop iterator
   int      pitchBend;     // pitch bend used in calcs
   
   // load the raw pitch-bend value into the channel
   pitchBend = msb << 7 | lsb;
   channels[ chan ].pitchBend = pitchBend;

   // calculate the transpositions for the channel
   // normalize pitchBend to (-0x100 to +0x100)
   pitchBend = pitchBend >> 5;      // divide to 0 to 0x200
   pitchBend -= 0x100;
   // multiply it to match the channel's pitch bend sensitivity (semitones only)
   pitchBend = pitchBend * channels[ chan ].rpPitchBendSemi;
   // upper byte now contains the semi-tone offset
   channels[ chan ].pbSemi = pitchBend >> 8;
   // lower byte contains the fraction
   channels[ chan ].pbFrac = ( pitchBend & 0xFF ) >> 4;
   
   // loop through all the voices to find any active notes on this channel
   for ( i = 0; i < NUM_VOICES; i++ ) {
      // skip inactive notes
      if ( voices[ i ].status != VOICE_KEYON ) continue;
      if ( voices[ i ].channel == chan ) {
         // update this voice's frequency
         updateVoice( i, UPDATE_FREQ );
      }
   }
}

// turns off all notes
void     OPL3_AllNotesOff () {
   WORD     i;    // for-loop iterator
   
   for ( i = 0; i < NUM_VOICES; i++ ) {
      // skip inactive notes
      if ( voices[ i ].status != VOICE_KEYON ) continue;
      // turn off the note
      OPL3_NoteOff( voices[ i ].channel, voices[ i ].noteKey, 0x40 );
   }
}

// resets a channel's controllers
void     OPL3_ResetChanControllers ( BYTE chan ) {
   BYTE     i;    // for-loop iterator
   
   // reset controller settings to their defaults
   channels[ chan ].volume = 100;
   channels[ chan ].pan = 0x40;
   channels[ chan ].expression = 0x7F;
   channels[ chan ].modulation = 0;
   channels[ chan ].sustainPedal = 0;
   // reset Pitch Bend
   channels[ chan ].pitchBend = 0x2000;
   channels[ chan ].pbSemi = 0;
   channels[ chan ].pbFrac = 0;
   // reset Registered Parameter Settings
   channels[ chan ].rpnIndexLsb = 0x7F;   // Null RPN index
   channels[ chan ].rpnIndexMsb = 0x7F;   // Null RPN index
   channels[ chan ].rpPitchBendSemi = 0x2;   // 2 semitones
   channels[ chan ].rpPitchBendCent = 0x0;   // 0 cents
   // then update any active voices on it
   for ( i = 0; i < NUM_VOICES; i++ ) {
      // skip inactive notes
      if ( voices[ i ].status != VOICE_KEYON ) continue;
      if ( voices[ i ].channel == chan ) {
         // update this voice
         updateVoice( i, UPDATE_ALL );
      }
   }
}