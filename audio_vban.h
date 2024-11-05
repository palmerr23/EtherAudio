/* Network audio transport and inter-host commms for Teensy Audio Library 
 * VBAN  definitions
 * see https://vb-audio.com/Services/support.htm#VBAN
 
 * Only AUDIO and SERVICE:IDENTIFICATION protocols are implemented
 * VBAN definititions are (C) 2015-2022 Vincent Burel www.vbaudio.com 
 * modified for Teensy 4.1 implementation Richard Palmer 2024
 * 
 * For the implementation:
  
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice, development funding notice, and this permission
 * notice shall be included in all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 */
 
#ifndef _Audio_VBAN_h_
#define _Audio_VBAN_h_

#define CE_VERSION "0.1.0"

#define VBAN_UDP_PORT   				6980	// VBAN default.
#define VBAN_HDR_SIZE 					28
#define VBAN_MAX_DATA						1436 	// max data bytes in a std UDP datagram
#define VBAN_MAX_SAMPLES 				(VBAN_MAX_DATA/2)		// max INT16 samples payload
#define VBAN_STREAM_NAME_LENGTH 16
#define VBAN_FLAG 							'NABV'	// quick uint32_t flag-word test
struct vban_header { 
	uint32_t vban = VBAN_FLAG;
	uint8_t format_SR; 		// SR index (3 MSB) = protocol, and 5 LSB (usage varies see below). 
	uint8_t format_nbs; 	// Audio: samples per frame (1 to 256 stored as 0 to 255)
	uint8_t format_nbc; 	// Audio: channels (1 to 256 stored as 0 to 255) 
	uint8_t format_bit; 	// Audio: Sample format (see definitions below)
	char streamname[VBAN_STREAM_NAME_LENGTH]; 	// stream name 
	uint32_t nuFrame; 		// frame number 
};

/**************** UDP PACKETS ****************/
struct vbanPkt
{	
  vban_header hdr;
	uint8_t content[VBAN_MAX_DATA];
};

// format_SR
#define VBAN_PROTOCOL_MASK	0xE0  // top three bits of format_SR 
#define VBAN_PROTO_SHIFT 		5			// shift protocol down before accessing the name array or enum
#define VBAN_SPEEDMASK			0x1F  // bottom 5 bits of format_SR 
// format_bit
#define VBAN_TYPE_MASK 	0x07

// AUDIO format_SR - 3 MSB: protocol
// 5 LSB vary by protocol
#define VBAN_AUDIO_PROTO_MAXNUMBER	4
enum vban_sub_protocol {VBAN_AUDIO = 0, VBAN_SERIAL = 0x20, VBAN_TEXT = 0x40, VBAN_SERVICE = 0x60};
#define VBAN_AUDIO_SHIFTED 0x00

extern char vban_sub_protocol_name [][8];

/***** Audio protocol = 0x00  *****/
// format_SR - 5 LSB: sample rate
#define VBAN_AUDIO_SR_MAXNUMBER 21

#define VBAN_AUDIO_441	16		// sample rate index (see control_ethernet.cpp)
#define VBAN_AUDIO_48		3
extern uint32_t VBAN_AUDIO_SRList[VBAN_AUDIO_SR_MAXNUMBER];

// format_bit: data format (3 LSBs)
#define VBAN_AUDIO_TYPE_MAXNUMBER		8
#define VBAN_AUDIO_INT16 						1
enum VBAN_AUDIO_dataType {BYTE8, INT16, INT24, INT32, FLOAT32, FLOAT64, BITS12, BITS10};
extern char VBAN_AUDIO_dataType_name[VBAN_AUDIO_TYPE_MAXNUMBER][8];
// 4 MSBs: CODEC (only PCM is implemented here)
#define VBAN_AUDIO_CODEC_MAXNUMBER 	3
#define VBAN_AUDIO_CODEC_MASK				0xf0
#define VBAN_AUDIO_CODEC_SHIFT 			4
		
enum VBAN_AUDIO_codec {PCM = 0, VBCA = 0x10, VBCV = 0x20}; // the rest are undefined
extern char VBAN_AUDIO_CODEC_name[VBAN_AUDIO_CODEC_MAXNUMBER][5];
// format_bit:
#define OK_VBAN_FMT (INT16 + PCM)	//  INT16 + PCM format_bit
// format_SR:
#define OK_VBAN_AUDIO_PROTO	((VBAN_AUDIO << 5) + VBAN_AUDIO_441)	// AUDIO proto, 44.1kHz 


/**** Serial protocol = 0x20 ****/
// format_SR - 5 LSB: bps rate
#define VBAN_SERIAL_SHIFTED   		0x20	// shifted
#define VBAN_MIDI_SHIFTED   		0x10	// shifted
#define VBAN_BPS_MAXNUMBER 25
extern uint32_t VBAN_BPSList[VBAN_BPS_MAXNUMBER]; // defined in control_ethernet

// format_nbs
// COM port config
#define VBAN_SERIAL_STOP_MASK 0x03
enum VBAN_SERIAL_compStop {ONE_STOP = 0, ONE_PLUS_HALF_STOP = 1, TWO_STOP = 2};
#define VBAN_SERIAL_START_MASK 0x04
enum VBAN_SERIAL_comStart {NO_START = 0, ONE_START = 4};
#define VBAN_SERIAL_PARITY_MASK 0x08
enum VBAN_SERIAL_comParity {NO_PARITY = 0, PARITY = 8};
#define VBAN_SERIAL_MULTIBLOCK_MASK 0x80
enum VBAN_SERIAL_comBlocks {ONE_BLOCK = 0, MULTI_BLOCK = 0x80};

// format_bit: data format (3 LSBs)
#define VBAN_SERIAL_DATATYPE_MASK 0x07
#define VBAN_SERIAL_8BIT	0x00	// 0x01..0x07 are underfined.
// 4 MSB
#define VBAN_SERIAL_STREAMTYPE_MASK 0xF0
enum VBAN_SERIAL_streamType {GENERIC = 0, MIDI = 0x10};

/***** Text protocol *****/
// format_SR - 5 LSB: bps rate
// same as Serial

// format_nbs is unused 

// format_bit: data format (3 LSBs)
#define VBAN_TEXT_SHIFTED 0x40
#define VBAN_TEXT_DATALENGTH_MASK 0xF0
#define VBAN_TEXT_LENGTH8				0x00	// rest are undefined
// 4 MSB
#define VBAN_TEXT_DATATYPE_MASK 0xF0
enum VBAN_TEXT_dataType {ASCII = 0, UTF8 = 0x10, WCHAR = 0x20}; // the rest are undefined

/***** Service protocol *****/
// ONLY the Identification protocol is supported
// Get hostname for incoming streams
// send hostname and capability when requested
// format_SR: 5 LSB must be zero

#define VBAN_SERVICE_SHIFTED   		0x60	// shifted
// format_nbc
#define VBAN_SERVICE_TYPE_MASK	0xFF
#define VBAN_SERVICE_ID					0x00
#define VBAN_SERVICE_CHAT				0x01
enum VBAN_SERVICE_type {SERVICE_ID = 0, CHATUTF8 = 1, RT_PKT_REG = 32, RT_PKT = 33};

// format_nbs
#define VBAN_SERVICE_FUNCTION_MASK	0xf0
enum VBAN_SERVICE_function {PING_REQUEST = 0, PING_REPLY = 0x80};

// format_bit
#define VBAN_SERVICE_BIT_VALUE	0

// nuFrame
#define VBAN_SERVICE_PING_FRAME	11	// anything will do, PING reply will have same nuFrame
#define VBAN_HOSTNAME_LEN  64

// Application type 
#define VBANPING_TYPE_RECEPTOR 0x00000001 // Simple receptor 
#define VBANPING_TYPE_TRANSMITTER 0x00000002 // Simple Transmitter 
#define VBANPING_TYPE_RECEPTORSPOT 0x00000004 // SPOT receptor (able to receive several streams) 
#define VBANPING_TYPE_TRANSMITTERSPOT 0x00000008 // SPOT transmitter (able to send several streams) 
#define VBANPING_TYPE_VIRTUALDEVICE 0x00000010 // Virtual Device 
#define VBANPING_TYPE_VIRTUALMIXER 0x00000020 // Virtual Mixer 
#define VBANPING_TYPE_MATRIX 0x00000040 // MATRIX 
#define VBANPING_TYPE_DAW 0x00000080 // Workstation 
#define VBANPING_TYPE_SERVER 0x01000000 // VBAN SERVER 

//Features (supported sub protocol) 
#define VBANPING_FEATURE_AUDIO 0x00000001 
#define VBANPING_FEATURE_AOIP 0x00000002 
#define VBANPING_FEATURE_VOIP 0x00000004 
#define VBANPING_FEATURE_SERIAL 0x00000100 
#define VBANPING_FEATURE_MIDI 0x00000300 
#define VBANPING_FEATURE_FRAME 0x00001000 
#define VBANPING_FEATURE_TXT 0x00010000

struct vban_ping 
{ 
		uint32_t bitType = VBANPING_TYPE_SERVER; 				/* VBAN device type*/ 
		uint32_t bitfeature = VBANPING_FEATURE_AUDIO; 	/* VBAN bit feature */ 
		uint32_t bitfeatureEx; 					/* VBAN extra bit feature */ 
		uint32_t PreferedRate = 44100; 	/* VBAN Preferred sample rate */ 
		uint32_t MinRate = 44100; 			/* VBAN Min samplerate supported */ 
		uint32_t MaxRate = 44100; 			/* VBAN Max Samplerate supported */ 
		uint32_t color_rgb; 						/* user color */ 
		uint8_t nVersion[4]; 						/* App version 4 bytes number */ 
		char GPS_Position[8]; 					/* Device position */ 
		char USER_Position[8]; 					/* Device position defined by a user process */ 
		char LangCode_ascii[8] = "EN"; 	/* main language used by user FR, EN, etc..*/ 
		char reserved_ascii[8]; 				/* unused : must be ZERO*/ 
		char reservedEx[64]; 						/* unused : must be ZERO*/ 
		char DistantIP_ascii[32]; 			/* Distant IP*/ 
		uint16_t DistantPort; 					/* Distant port*/ 
		uint16_t DistantReserved; 			/* Reserved*/ 
		char  DeviceName_ascii[64] 			= "Teensy 4.1";	/* Device Name (physical device) */ 
		char ManufacturerName_ascii[64] = "PJRC";/* Manufacturer Name */ 
		char ApplicationName_ascii[64]  = "ControlEthernet"; /* Application Name */ 
		char HostName_ascii[64]; 				/* dns host name */ 
		char UserName_utf8[128]; 				/* User Name */ 
		char UserComment_utf8[128]; 		/* User Comment/ Mood/ Remark/ message */ 
};


#endif
