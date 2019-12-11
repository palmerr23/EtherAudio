/* Network audio transport and inter-host commms for Teensy Audio Library - common definitions
 * Richard Palmer (C) 2019
 
 *** W5100.h should be modified to enable fast SPI: 
 *** 	#define SPI_ETHERNET_SETTINGS SPISettings(30000000, MSBFIRST, SPI_MODE0) // Lines 21 & 28 
 *** Ethernet.h should be modified to enable bigger RX/TX buffers:
 ***    #define ETHERNET_LARGE_BUFFERS // Line 48
 ***    #define MAX_SOCK_NUM 2 		   // Line 39
 *** failing to do so will result in higher packet loss 
 
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

#ifndef Audio_Net_h_
#define Audio_Net_h_


#include "stdio.h"  // for NULL
#include <string.h> // for memcpy
#include "AudioStream.h"
#include "Ethernet.h"
#include "EthernetUdp.h"

// set pin defaults - can be overridden in code
#ifndef ResetWIZ_PIN
#define ResetWIZ_PIN 9
#endif

#ifndef WIZ_CS_PIN
#define WIZ_CS_PIN 10
#endif

/*
 * Items that are common across the UDP/IP control, input and output objects
 */
#define PKT_DECAY_A 4			// update() cycles of new packets before discarding audio packets if not released().
#define PKT_DECAY_C 5			// update() cycles of new packets before discarding control packets if not released().
#define STREAM_DECAY 10			// after this many update()cycles after the last packet received, assume stream is off-line.
// assumes 16 bit samples
#define MAXCHANNELS 2			// currrently only 2 channels implemented
#define BYTES_SAMPLE 2
#define STD_FREQ 44
#define BLOX_SEC 345		// 2.9mS bocks per second
#define SAMPLES_2 (AUDIO_BLOCK_SAMPLES * BYTES_SAMPLE) // samples in a stereo stream
#define TARGET_BCAST 255	// remoteHostID for broadcast streams and packets
#define BLOCK_SEQ_WINDOW 256	// transmitted packet sequence number recycles, sequence numbers in queues are synthetic
//#define PACKET_WINDOW 256	// sequence numbers in audio blocks wrap around - window is large enough to detect a few lost blocks. Longer breaks detected by stream aging process, ethernet cable disconnect. 
// Just for reference at this point, only error detection or correction is continuous stream of zero buffers transmitted when no packets arriving, and dumping of blocks with bad pktTypes/
//#define UBUFSIZ (8 + SAMPLES_2 * 2) // sruct audio2Datagram

/* Packet types:
pktType Use
------- ----------------
0		Reserved	
1-15	Audio data packets	
16-31	MIDI or other Audio library “content” packets (TBD)	
32-63	Audio library Stream Control/Status pkts  	
64-95	MIDI or other Audio library “control/status” packets (TBD)	
96-127	Transport (OSI Layers 1-5) control and status messages	
128-255	Available for user-defined packet types	

Packet maximum sizes
0-31	Content[] payload must a multiple of 256 bytes. 1500 byte TCP/UDP total packet including Ethernet and Datagram headers.
32-255	256 byte content[] limit, excluding header data (audio buffers are used for content, headers are stored separately)
*/
// The first byte of ALL packets is the pktType
// Streaming (AUDIO) BLOCK TYPES
#define NET_BAD_PKT 0
#define NET_STREAM_BASE 1			// reserve types 1-15 for audio datablocks
#define NET_AUDIO_DATA_PKT 2 
// 2 channels, sample blocks sequential in pkt (Ch1[1..n] , Ch2[1..n]) 
#define NET_MIDI_MSG_PKT	16 		// Not implemented

#define NET_STREAM_CONTROL_BASE 32	// 32-95 for control packets
#define NET_AUDIO_STREAM_INFO 32 // stream info pktType - packet struct for update message

#define NET_TRANSP_CONTROL_BASE 96 	// anything ethernet - IP, MAC, port, hostname etc.
#define NET_CONTROL_BLOCK_A 96 		// not used

#define NET_USER_CONTROL_BASE 128	// 128-255 available for user program use

/**************** AUDIO ****************/
 typedef struct audio2Dg{		// pktType = 2 
  uint8_t pktType = NET_AUDIO_DATA_PKT; // ALL datagrams have pktType as the first payload byte
  uint8_t sequence;		// to identify dropped packets *** rollover not fully implemented (rollover .sequence = zero will be dropped
  uint8_t netStreamID = 0; 		// fill this in later for output streams
  int16_t content[SAMPLES_2]; 	// channels are sequential or interleaved - as defined by the pktType. 
} audio2Datagram; 				// use (audio2Datagram) cast to map onto raw UDP buffers
#define TCPDG_HDR_A (sizeof(audio2Datagram) - (SAMPLES_2 *2))

// Audio queue item - holds incoming/outgoing audio blocks plut control and routing information. 
// Used by control_ethernet update() to handle physical send/receive and input/output objects for AudioStream transmit()/receive().
#define MAX_AUDIO_QUEUE 32		// number of slots in the audio queue
// subscriptions to audio streams is handled as per the main library - each subscribing object adds one to the counter, when all updates() are completed, count should be 0 and the block released;
 typedef struct audio2Q2{		// pktType = 2; 2 channels  MAXCHANNELS = 2
  uint8_t pktType ; 	
  short nextBlock;	  			// linked list index (-1 for last block in Q)
  uint8_t subscribed;			// set to stream.subscribers on input, each input object reading pkt decrements counter; discarded when == 0
  int8_t pktAge;				// count down to death (in update cycles)
  uint8_t localStreamID;		// local stream 
  uint8_t sourceStreamID; 		// host stream indicator; ignored for output pkts
  uint8_t sourcehostID;  		// host logical ID ; targetIP for output pkts
  uint8_t sequence; 			// rolls over regularly 
  audio_block_t * bufPtr[MAXCHANNELS]; 		// buffers are allocated and released from audioStream pool
} audio2Queue; 		
		
// Full definition of an incoming/outgoing audio stream. 
// Copied from an incoming StreamInfo packet with Source IP and host streamID.
// Stream management (active, subscribers) added
// control_ethernet maintains a streamsIn[] list and a streamsOut[] list for this interface.
#define MAX_TCP_STREAMS_IN 32 		// all available streams for this host
#define MAX_TCP_STREAMS_OUT 8 		// published streams from this host
#define UPD_MSGS_SECS 4
#define UPDATE_MSGS_EVERY (UPD_MSGS_SECS * BLOX_SEC)// how often stream status pkts are sent out - in update() cycles.
//#define 0(3 * UPDATE_MSGS_EVERY) //
#define S_NAME_LEN 16

typedef struct netAudioStr{  	// define a stream so that its structure doesn't need to be transmitted with each packet
  short hostStreamID = -1; 		// Input streams: Logical stream ID set by generating host. 
								// Output: Set by default to stream array index (= logical stream number); -1 for idle (input and output)
								// May be set (output only) by user program, to allow repeatable remote identification of streams
  uint8_t remoteHostID;			// remote host: input streams - logical host originating the stream. output streams -  target host 
  char streamName[S_NAME_LEN];
  uint8_t subscribers = 0; 		// Input stream: 0 = inactive (unsubscribed) non-zero = active = number of input objects reading this stream. Output streams: ignored. (Also use for control pkts related to this stream?)
  uint8_t active = 0;			// > 0 if packets recently recieved. Reset to STREAM_DECAY on each block received, decremented each cycle
  uint8_t nChannels = 2; 		// 1, 2 or 4 are practical. 8 may be too big for current Ethernet library to transmit in a cycle (max 1100kB/sec on T3/4 with Wiznet 5500)
  uint8_t nSamples = AUDIO_BLOCK_SAMPLES; // maybe allow division into smaller blocks later where struct length > (1500 - UDP/Ethernet overheads) i.e. exceeds MTU of 1500, as not all switches will transmit jumbo blocks.
  uint8_t btyesPerSample = 2; 	// can't assume all recipients are conforming to current AudioStream defaults
  uint8_t sampleFreq = 44;	  	// ditto 
} netAudioStream;

// Stream Information packet (#32 NET_AUDIO_STREAM_INFO), regularly sent out to potential subscribers or received from sender. 
// Payload  - mostly copied from the netAudioStream struct.
// Note: Host IP is in the UDP wrapper, so not duplicated here.
typedef struct streamInfo {
  uint8_t streamID;
  uint8_t nChannels; 		
  uint8_t nSamples; 
  uint8_t btyesPerSample; 	
  uint8_t sampleFreq;	
  char streamName[S_NAME_LEN];  
} streamInfoPkt;

#define MAX_MIDI_LEN 3 // biggest MIDI message
#define MAX_MIDIS_PKT 16 // cram up to 16 Messages in a packet

typedef struct MidiDG{ 	// To be implemented
	  uint8_t pktType = NET_MIDI_MSG_PKT; 		//  
	  uint8_t msgs; 		// CONTROLQ_PAYLOAD limit
	  uint8_t payload[MAX_MIDI_LEN * MAX_MIDIS_PKT]; // only msgs * MAX_MIDI_LEN bytes are used 
} MidiDatagram;
/*************** CONTROL **********************/
//Control Packet types (32-127)

//#define AUDIO_STREAM_BLOCK 97  // publishing or subscribing to audio streams
//#define AUDIO_CONTROL_BLOCK 98  // any other control or status messages supported directly by audio library (input output or control) objects
//#define MIDI_CONTROL_BLOCK  110 // anything to do with publishing or subscribing to audio streams


// this is the prototype for all CONTROL and STATUS datagrams (pktTypes 32 - 127)
// generally a single pktType is shared for all control and status messages in an application.
// as further differentiation of message types can be incorporated into the payload (usually a structure).
// note limited payload 
#define MAX_CONTROLQ_PAYLOAD 32 //256 byte potential, as payloadSize is 8 bit
typedef struct controlDG{ 	
	  uint8_t pktType = 0; 		// ALL datagrams have pktType as the first payload byte
	  uint8_t payloadSize; 		// CONTROLQ_PAYLOAD limit
	  uint8_t payload[MAX_CONTROLQ_PAYLOAD]; // only payloadSize bytes are used 
} controlDatagram;
#define TCPDG_HDR_C (sizeof(controlDatagram) - MAX_CONTROLQ_PAYLOAD)

#define CONT_DG_HDR_SIZE (sizeof(controlDatagram) - AUDIO_BLOCK_SAMPLES * 2)  // control DATAGRAM header size

#define MAX_CONTROL_QUEUE 32	// may need to be much bigger for MIDI or other multi message/update cycle protocols
#define MAX_CONTROL_SUBS 16		// maximum number of subscribers to the control stream
#define CONTROLQ_PAYLOAD 32 	// not sure yet what the appropriate size is for this
// subscriptions for object control packets are different to audio streams 
// - each OBJECT control pkt (pktType < NET_USER_CONTROL_BASE) only lasts a single update() cycle
// - USER pkts (pktType >= NET_USER_CONTROL_BASE) are marked for deletion when read once, or aged out  
typedef struct netControlQueue{ 	
	  uint8_t pktType; 			// set the default to a standard control block. 
	  short nextBlock;	  		// linked list index (-1 for last block in Q)
     // bool blockFree;			// managed by the queue manager
	 // bool ready_to_send;		// set to true when block is ready
      int8_t pktAge;			// count down to death (in update cycles) 
	  int8_t subscribed; 		// USER program still to read the item 
      uint8_t remoteHostID;  	  		// source host for inputs, target for outputs
	  short hostStreamID;		// source CONTROLstreamID 
	  short payloadSize; 		// actual payload
	  uint8_t payload[CONTROLQ_PAYLOAD]; // only payloadSize bytes are used 
} controlQueue;

#define CONT_HDR_SIZE (sizeof(controlQueue) - CONTROLQ_PAYLOAD) // control QUEUE header size

/* Ethernet - use a single UDP port (and socket)
*/
#define UDP_AUDIO_PORT   8888		// Ethernet/UDP implementation only supports a single port/socket

#endif
