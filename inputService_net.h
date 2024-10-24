/* AudioInputServiceNet 
 * Multi-channel Text and structured data input object for Teensy Audio Library 
 *
 * It does not use AudioStream, so there is no regular update() cycle associated with this object.
 * End user code should test and consume the incoming queue regularly to avoid lost data.
 *
 * Richard Palmer - 2024
 * Released under GNU Affero General Public License v3.0 or later
 * SPDX-License-Identifier: AGPL-3.0-or-later
 */

#pragma once

#include "Arduino.h"
#include "Audio.h"
#include "audio_net.h"
#include "control_ethernet.h"

//#define IS_DEBUG


extern  AudioControlEtherTransport etherTran; // handles all stream and subscription traffic

class AudioInputServiceNet 
{
public:
	AudioInputServiceNet(void) { ; 	}
		
	friend class AudioControlEthernet; // may not be required
	friend class AudioControlEtherTransport;

	void begin(void);
	
  // handling queued packets
	bool available(void); 		// number of queued packects
	int dataSize(void); 			// assumes available(), size packet data (excluding headers)
	int getPktsInQueue();			// number of queued packets, works like available()
	queuePkt getPkt();				// return the next queued packet and pop from the queue
	int droppedFrames(bool reset = true);	// get and optionally reset the number of frames that were not queued (overrun)

	// VBAN stream subscription
	int subscribe(char * name, uint8_t sType, char * hostName = nullptr); // use this for broadcast
	int subscribe(char * name, uint8_t sType, IPAddress remoteIP);
	void unSubscribe(void); // release the subscribed stream. Packets will not be queued.

private:
	unsigned long getCurPktNo(void) { return _currentPkt_I;} 
	int getMyStream(void) { return _myStreamI; } // get the ID of my subscribed stream
	
	int itim; //debug
	std::queue <queuePkt> _myQueueI;
	

	uint16_t _inChans = 1; // only one is supported
	int _myStreamI = EOQ; 	// unsubscribed at start
	int _mySubI = EOQ;  		// subscription index

	// internal buffers and pointers

	// internal status and control 
	bool inputBegun = false;
	uint32_t _lastQFrameNum;
	uint32_t _currentPkt_I;
	uint32_t framesDropped = 0;
	uint32_t didNotTransmit = 0;
//	int _queueLowWater = 0; 

	int npiq;
	uint32_t lastUpdate; 
	int _currentBuffer = 0; // may take several calls to fill the packet
	int qUsedSamples = 0;		// 
	 
	//debug
	void printHdr(vban_header *hdr);
	void printContent(uint8_t *buff, int len);
};


