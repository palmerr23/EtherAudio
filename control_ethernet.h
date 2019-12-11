/*
 * Ethernet (UDP) Network Control object for Teensy Audio Library
 *
 * This file handles low level communications management, interface setup and all Datagram traffic
 * Currently handles all Network queue management
 * 
 * Copyright 2019, Richard Palmer
 *
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

#ifndef control_ethernet_
#define control_ethernet_

//#define ETHERNET_LARGE_BUFFERS 	// see ethernet.cpp; w5100.cpp

#include "AudioControl.h"
#include "SPI.h" // just to be safe
#include "Ethernet.h"
//#include <w5100.h>
#include "EthernetUdp.h"
#include "AudioStream.h"
#include "Audio.h"
#include "audio_net.h"

// defaults - values should be changed in your code using the setXXX() functions
#ifndef WIZ_CS_PIN
	#define WIZ_CS_PIN 10
#endif
#ifndef ResetWIZ_PIN
	#define ResetWIZ_PIN 9
#endif

/*************** Ethernet connections, sockets and UDP datagrams **************/
class AudioControlEtherNet : public AudioControl, public AudioStream
{ 
public:
	AudioControlEtherNet(void) : AudioStream(0, NULL) { enable(); begin(); }
	void begin(void);	
	virtual void update(void);
	bool enable();				
	
	// control functions 	
	void setMyNet(IPAddress ip) ;	//sets this interface's Network address, needs to be followed by setMyID
	IPAddress getMyIP(void) { return _myIP; };
	void setMyID(uint8_t thisID);
	void setMyMAC(uint8_t *mac); // set the MAC address of this interface - using "DEAD BEEF" high bytes, as per common practice.

	void setListenlPort(unsigned int cPort); // reset the TCP listen port for comms.
	unsigned int getListenlPort(void); 		// debug only
	int getLinkStatus(void) {return myLinkOn;}; // true for active link
	
	uint8_t hostIPtoHostID(IPAddress ip); // return logical remoteHostID (last byte) from full IP address 
	IPAddress hostIDtoHostIP(uint8_t ID);
	
	// audio functions - some skeletons are required - they do nothing here
	bool volume(float n);	
	bool disable(void) ;
	bool inputLevel(float volume);  
	bool inputSelect(int n);
	
	friend class AudioOutputNet; // work with the UDP/IP audio objects and others sharing this control
	friend class AudioInputNet;
protected:
	static void isr(void);	
private:
	short registerObject() ; // register a new object for control queue management
	int sendUDPPacket(IPAddress targetIP, uint16_t thePort, uint8_t * buffer, size_t buflen);
	short subscribed(uint8_t host, uint8_t stream); // this stream is subscribed by how many?
	
	bool myLinkOn; 	// true if Ethernet.linkStatus == LINKON
	IPAddress _myIP;
	// An EthernetUDP instance to let us send and receive packets over UDP. Ethernet.h sets up the Ethernet instance.
	EthernetUDP Udp; 	

	//short knownStream(short thisHost, short thisHostStream); // do we know this stream? see getLocalStreamID
	uint8_t _myHostID = 1; // set ID portion of IP and MAC addresses and update them
	uint8_t _myMAC[6]= { 0xDE, 0xAD, 0xBE, 0xEF, 0xFE, 1 }; // last digit is same as _myHostID
	audio2Datagram UDPBuffer; // Ethernet read/write buffer
	controlDatagram Cbuffer;
	
/*************** Audio and control streams and queues **************/
public:
	short getActiveStreams(void) { return activeTCPstreams_I; }; // number of registered input streams
	short getLocalStreamID(short thisHost, short thisHostStream); // translate source remoteHostID/streamID into local StreamID 
	netAudioStream getStream(uint8_t id) { return streamsIn[id]; }; // stream info item
	
	// user mode communication - control blocks ( pktType >= 128)
	bool getQueuedUserControlMsg(controlQueue *buf);		// return a control message in buffer. User mode fucntion - kill queued block once read 
	bool queueControlBlk(uint8_t remoteHostID, uint8_t pktType, uint8_t * pkt, short pktLen); // pkt is usually a pointer to a struct
		
private:	
	int createStream_O(uint8_t channels);		// local stream
	int createStream_O(uint8_t channels, uint8_t targetID);
	int createStream_O(uint8_t channels, uint8_t targetID, uint8_t samples, uint8_t bps,uint8_t freq); // only first arg is mandatory, rest have defaults
	void setStreamName_O(char * sName, short stream);	// private - user levelversion is in the output object
	void registerNewStream(uint8_t remoteHostID, streamInfoPkt spkt); // new stream from Ethernet needs registering

	void sendAudioPkts(); 
	void sendCtrlPackets(volatile short * controlQ);
	void streamPkt(short StreamID); // queue a stream update packet

	netAudioStream streamsIn[MAX_TCP_STREAMS_IN], streamsOut[MAX_TCP_STREAMS_OUT];
	streamInfoPkt streamInfoPkts[MAX_TCP_STREAMS_OUT];
	//streamInfoPkt streamInfo; 

	short getLinkNewQblk_A(volatile short * queue); // 
	short getLinkNewQblk_C(volatile short * queue);
	bool addPacketToQueues(audio2Datagram * buffer, short thisHost, short thisHostStream);
	short getNextAudioQblk_I(uint8_t stream); // get the next audio input block queued for this stream, mark as read by me
	bool cleanAudioQueue(volatile short * thisQ);
	bool cleanControlQueue(volatile short * thisQ); // returns shortform OK test
	void freeQblk_A(volatile short * queue, short blockindex);
	void freeQblk_C(volatile short * queue, short blockindex);
	short countQ_C(short *q);
	short countQ_A(short *q);
	short countQ_A_S(short *q, short stream); // count for a specific local stream

	volatile short audioQ_I, audioQ_O, audioQ_Free, controlQ_I, controlQ_O, controlQ_Free;	
	audio2Queue Qblk_A[MAX_AUDIO_QUEUE];
	controlQueue Qblk_C[MAX_CONTROL_QUEUE]; 

	volatile short activeTCPstreams_O, activeTCPstreams_I; // counts of registered incoming (Stream info pkts) and outgoing (registered by objects) audio streams
	short activeObjects_C = 0;	// number of objects registered for control messages 

	void updateStreamInfo(uint8_t StreamID, streamInfoPkt spkt);	
	
	audio_block_t * myBlock[MAXCHANNELS]; 	// local pointers to each audio channel to be managed in a stream
	audio_block_t zeroBlockA; 				// a block of zero samples.
	audio2Datagram audioBuffer; 			// re-used for each input/output packet
	controlDatagram controlBuffer; 
	streamInfoPkt streamInfobuffer;
	
	//IPAddress _CTargetIP;	
	uint16_t _controlPort = UDP_AUDIO_PORT; // Listen port for ethernet

	//bool updateScheduled = false;			// no ISR so unable to be the update() master
	bool ethernetEnabled = false; 			// enable() code has completed
	bool ethernetUp_C = false; 				// enable completed & cable is connected
	bool ethernetUp_C_last = false; 		// flag to trigger a restart of UDP, on reconnection

	long aSent, cSent, aRecd, cRecd;
	long ce_reportCntr;
	
	void printIP(char text[], IPAddress buf);
};
#endif