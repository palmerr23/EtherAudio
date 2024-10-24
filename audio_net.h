/* Common definitions for ethernet input, output and control objects
 * VBAN compliant network audio transport and inter-host commms for Teensy Audio Library 

 * Only AUDIO and SERVICE:IDENTIFICATION protocols are currently implemented
 * AudioControlEthernet manages the ethernet connnection and sends/receives packets
 * AudioControlEthernet manages streams and queues to and from ethernet : audio input/output objects
 * AudioOutputNet and AudioInputNet support 1..8 inputs or outputs
  
 * The underpinning UDP Packet protocol follows the VBAN standard
 * https://vb-audio.com/Services/support.htm#VBAN
 
 * Only tested on Teensy 4.1 using QNEthernet connections
 *
 * Richard Palmer 2024
 * Released under GNU Affero General Public License v3.0 or later
 * SPDX-License-Identifier: AGPL-3.0-or-later 
 */

#ifndef Audio_Net_h_
#define Audio_Net_h_

#include "stdio.h"  // for NULL
#include <string.h> // for memcpy
#include <queue>
#include "IPAddress.h"

#include "audio_vban.h"

#define USE_MDNS

#define UDP_MAX_DATA			1464	// 
#define SAMPLES_BUF				128		// Audio lib standard
//#define SERVICE_MAX_DATA 	255		// less than VBAN standard

#define AUDIO_PKT_TIME 3		// 2.9 mS
#define OK_PKT_TIME (AUDIO_PKT_TIME * 1000)	// allow subscription to streams that may not broadacst packets at full Audio Lib intervals 
#define DEAD_STREAM_TIME (AUDIO_PKT_TIME * 2000)	// consider a stream dead if it doesn't send any packets

#define EOQ									-1 // end of queue marker

/**************** default Queue and Structure sizing****************/
#define MAX_AUDIO_QUEUE 12		// high water mark in an audio queue 
#define AUDIO_QUEUE_HIGH_WATER (MAX_AUDIO_QUEUE -1)
#define MAX_UDP_STREAMS 		8 		// in or out
#define MAX_REM_HOSTS				8			// hostname to IP matches
#define MAX_SUBSCRIPTIONS		8			// may differ from STREAMS_IN
#define MAX_SERVICE_QUEUE 32

// assumes 16 bit samples
#define MAXCHANNELS 8			// currrently only 2 channels implemented
#define CHANS_2_PKTS	6		// for 6 or more channels, we need two VBAN output packets
#define BYTES_SAMPLE 2
#define AUDIO_BLOCK_SAMPLES 128
//#define BLOX_SEC 345		// 2.9mS bocks per second
#define SAMPLES_2 (AUDIO_BLOCK_SAMPLES * BYTES_SAMPLE) // samples in a stereo stream
#define TARGET_BCAST 255	// remoteHostID for broadcast streams and packets

#define STREAM_IN 		0	// for getStreamInfo
#define STREAM_OUT 		1
/**************** VBAN STREAMS ****************/
// only AUDIO and SERVICE sub-protocols are implemented
// Stream information for incoming and outgoing audio streams.
// hdr is for refereence as packet formats (channels/samples) may change dynamically
// For elements marked (VBAN) see VBAN specification or audio_vban.h
// streams and queues (AudioControlEthernet) constructed by control_ethernet
// separate instances for input and output streams
struct streamInfo
{ 
	vban_header hdr;									// 28 bytes (VBAN)
	IPAddress 	remoteIP;							// Remote host for input streams. Target for output streams 
	uint32_t 		lastPktTime = 0;			// mS stored on each received packet - stream deactivation not implemented
	int16_t 		hostIndx = EOQ;				// index into hostInfo table (streamsOut: unused)
  int16_t 		subscription = EOQ; 	// index into subscription table. Dump packets when EOQ (streamsOut: unused)
	int8_t			type;	// see pktType
	bool 				active = 0;						// this is a record with valid data
};

// host to IP matching - from incoming SERVICE : ID packets
struct hostInfo
{
	IPAddress remoteIP;
	uint32_t lastPinged = 0;
	char hostName[VBAN_HOSTNAME_LEN] = "*";
	bool active = false;
};

/**************** QUEUE PACKETS ****************/
// Uses std::queue
// All protocols are queued with the same packet structure 
#define QPKT_HDR_SIZE (VBAN_HDR_SIZE + 4)
struct alignas(int) queuePkt
{
	int16_t		streamIndx;
	uint16_t	samplesUsed;	// for split AUDIO packets. Data length for SERVICE
  vban_header hdr; // transmit from here | received packet.data()
	union 
	{
		uint8_t content[VBAN_MAX_DATA];			// VBAN standard
		int16_t content16[VBAN_MAX_DATA/2];	// easier access to audio samples
	} c;
};

// subscriptions may be made before the stream is present
// queue & pointer is assigned by subscriber
// housekeeping (control_ethernet::update() )regularly matches active streams to subscriptions
// if neither ipAddress or hostname is provided, any host's matching streamName will work
struct subscription {
	std::queue <queuePkt> *qPtr = (std::queue <queuePkt> *)nullptr;
	int				maxQ;										// current queue
	IPAddress	ipAddress;	
	char			streamName[VBAN_STREAM_NAME_LENGTH];
	char			hostName[VBAN_HOSTNAME_LEN] ="?"; 
	int8_t		protocol = EOQ;	// see format_SR
	int8_t		serviceType = EOQ; // format_nbc for Service/Text/Serial pkts
	int8_t		streamID = EOQ;
	bool			active = false; 
};

// pretty VBAN header for end-user information (constructed as needed)
// see getStreamInfo()
#define STREAM_NAME_LEN 16
struct stream_pretty{
	uint32_t	sampleRate;
	uint32_t	lastPktTime;
	uint32_t	pktsInQueue = 0;
	uint16_t	pktSamples;
	uint16_t 	channels;
	uint8_t 	protocol; 
	char			dataType[8];
	char 			streamName[VBAN_STREAM_NAME_LENGTH];
	IPAddress	ipAddress;
	char			hostName[VBAN_HOSTNAME_LEN];
	char 			codec[8];
	int16_t 	subscription = EOQ;	
	bool			active = 0; 
};

enum pktType  {PKT_NOT_CONSUMED, PKT_AUDIO, PKT_SERIAL, PKT_MIDI, PKT_TEXT, PKT_SERVICE, PKT_PING, PKT_CHAT};

//#define GET_FIRST_BLOCK -1 // getNextInQueue
#endif
