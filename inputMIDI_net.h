/* Multi-channel Network MIDI input object for Teensy Audio Library 
 * does NOT take update_responsibility
 * Changes to this file should possibly be mirrored in inputText_net.h
 *
 * Richard Palmer - 2024
 * Released under GNU Affero General Public License v3.0 or later
 * SPDX-License-Identifier: AGPL-3.0-or-later
 */

#pragma once

#include "Arduino.h"
//#include "AudioStream.h"
#include "Audio.h"
#include "audio_net.h"
#include "control_ethernet.h"

/*
 * This file handles high-level MIDI user interface and logical control functions 
 * There is no regular update() cycle associated with this object.
 * End user code should test and consume the incoming queue regularly to avoid lost data.
 */
 
#define DEFAULT_CHANNELS 1	// only one is supported
#define DEFAULT_INQ_SIZE	8

extern  AudioControlEtherTransport etherTran;

class AudioInputMIDINet : public AudioStream 
{
public:
	AudioInputMIDINet(int inCh = DEFAULT_CHANNELS) : AudioStream(0, NULL) 
	{ ; }
		
	friend class AudioControlEtherNet;
	friend class AudioControlEtherTransport;

	void begin(void);
	void update(void);

	int subscribe(char * name, char * hostName = nullptr); // use this for broadcast
	int subscribe(char * name, IPAddress remoteIP);
	void unSubscribe(void); // release the subscribed stream. Packets will not be queued.
	
	int droppedFrames(bool reset = true);	// get and reset the number of dropped frames

protected:	
	unsigned long getCurPktNo(void) { return _currentPkt_I;} 
	int getPktsInQueue();
	int itim; //debug
	void printHosts();

	int getMyStream(void) { return _myStreamI; } // get the ID of my subscribed stream
	//int subscribe(int streamID); // subscribe to a known audio stream. Not for end users
	std::queue <queuePkt> _myQueueI;

	bool update_responsibility = false;

private:
	uint16_t _inChans = 1;
	int _myStreamI = -1; 	// unsubscribed at start
	int _mySubI = -1;  		// subscription index


	
	// internal status and control 
	bool inputBegun = false;
	uint32_t _lastQFrameNum;
	uint32_t _currentPkt_I;
	uint32_t framesDropped = 0;
	uint32_t didNotTransmit = 0;
	int _queueLowWater = 0; // net quality & delay. Increase if poor qual

	int npiq;
	uint32_t lastUpdate; 
	int _currentBuffer = 0; // may take several calls to fill the packet
	int qUsedSamples = 0;		// 
	 
	//debug
	void print6pkt(queuePkt * pkt, int first, int last);
	//void print3buf(int indx, int first, int last);
};


