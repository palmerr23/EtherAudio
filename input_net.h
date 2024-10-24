/* Multi-channel Network Audio input object for Teensy Audio Library 
 * does NOT take update_responsibility
 *
 * Richard Palmer - 2024
 * Released under GNU Affero General Public License v3.0 or later
 * SPDX-License-Identifier: AGPL-3.0-or-later
 */

#pragma once

#include "Arduino.h"
#include "AudioStream.h"
#include "Audio.h"
#include "audio_net.h"
#include "control_ethernet.h"

#define IN_DEBUG

//#define MALLOC_BUFS // allows begin() to be called by constructor

/*
 * This file just handles high-level block transfers and logical audio and control functions 
 * 
 */
 
#define DEFAULT_CHANNELS 2
// 6-8 audio channels need 2 packets per buffer
//#define DEFAULT_INQ_SIZE	8
//#define QUEUE_LOW_WATER		3 // don't process update if less.  3 packets may be needed to fill a buffer

extern  AudioControlEtherTransport etherTran; // handles all stream and subscription traffic

class AudioInputNet : public AudioStream 
{
public:
	AudioInputNet(int inCh = DEFAULT_CHANNELS) : AudioStream(0, NULL) 
	{ 
		_inChans = inCh;
	}
		
	friend class AudioControlEthernet; // may not be required
	friend class AudioControlEtherTransport;

	void begin(void);
	void update(void);

	int subscribe(char * name, char * hostName = nullptr); // use this for broadcast
	int subscribe(char * name, IPAddress remoteIP);
	void unSubscribe(void); // release the subscribed stream. Packets will not be queued.
	
	int droppedFrames(bool reset = true);	// get and reset the number of dropped frames
	int missedTransmit(bool reset = true); // failed to transmit - perhaps out of AudioMemory
protected:	
	//unsigned long getCurPktNo(void) { return _currentPkt_I;} // user side 
	int getPktsInQueue();
	int itim; //debug

	int getMyStream(void) { return _myStreamI; } // get the ID of my subscribed stream

	std::queue <queuePkt> _myQueueI;
	bool update_responsibility = false;

private:
	uint16_t _inChans;
	int _myStreamI = -1; 	// unsubscribed at start
	int _mySubI = -1;  		// subscription index

	// internal buffers and pointers
	audio_block_t * new_block[MAXCHANNELS];
	
	// internal status and control 
	bool inputBegun = false;


//	int _queueLowWater = QUEUE_LOW_WATER; // net quality & delay. Increase if poor qual
 
	int _currentBuffer = 0; // may take several calls to fill the packet
	int qUsedSamples = 0;		// 
	 
	//debug
	int npiq; //there were no packets to process in the queue
	uint32_t _lastQFrameNum;
	uint32_t framesDropped = 0;
	uint32_t didNotTransmit = 0;
	uint32_t lastUpdate;
	void print6pkt(queuePkt * pkt, int first, int last);
	void print3buf(int indx, int first, int last);
};


