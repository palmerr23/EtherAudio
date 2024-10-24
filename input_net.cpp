/* Multi-channel Network input for Teensy Audio Library 
 * does NOT take update_responsibility
 * Richard Palmer (C) 2024
 *
 * Released under GNU Affero General Public License v3.0 or later
 * SPDX-License-Identifier: AGPL-3.0-or-later
 */


#ifndef _INPUT_NET_HPP_
#define _INPUT_NET_HPP_

#include <Arduino.h>
#include "input_net.h"



void AudioInputNet::begin(void)
{
	_myStreamI = -1; 	// **** should be unsubscribed: -1

	// allocate enough audio blocks. If any fails, allocate none
	// only release them if stopping 
	int i, j;
	for (i = 0; i < _inChans; i++) 
	{

#ifdef MALLOC_BUFS	
		new_block[i] = (audio_block_t*)malloc(sizeof(audio_block_t));
#else //can't allocate() in constructor
		new_block[i] = allocate();
#endif
		if (new_block[i] == nullptr) 
		{
			for (j=0; j < i; j++) 
			{
#ifdef MALLOC_BUFS
				free(new_block[j]);
#else
				release(new_block[j]);
#endif
			}	
			Serial.println("Input begin: Can't allocate audio buffers");				
			break;
		}
		memset(new_block[i]->data, 0, AUDIO_BLOCK_SAMPLES * 2);
	}
	inputBegun = true;
	
	Serial.printf("IN: inputNet.begin() complete, allocated OK %i\n", new_block[0] != nullptr );
}


//  update() will not be called if there are no connected patchCords
//  Only transmit if queue has more than low water mark packets.

void AudioInputNet::update(void)
{
 static bool printMe = false;
	itim++;
#ifdef IN_DEBUG
	printMe = itim % 500 == 0 &&  millis() > 4000;
	itim++;
#endif
	
	if(millis() - lastUpdate > 4)
		Serial.printf("*** Missed update %i\n", _myStreamI);
	lastUpdate = millis();
	
	if(!etherTran.linkIsUp()) //  uninitialised or disconnected
		return;

	if(_myStreamI == EOQ) // unsubscribed - check for update
	{
		int temp = etherTran.getStreamFromSub(_mySubI);
		if(temp == EOQ)
		{
	//		if(printMe) Serial.printf("*** In Upd no stream %i, %i\n", _myStreamI, inputBegun);
			return;
		}
		_myStreamI = temp;
	}
	int i, j;	
	//if(printMe) Serial.printf("![%i,%i]", _myStreamI, _mySubI);
	
	if(etherTran.streamsIn[_myStreamI].active == false)
	{ 
		if(printMe) Serial.println("*** Inactive stream");
		return;
	}

	if(_myQueueI.size() == 0) // no packets to process
	{
		npiq++;
#ifdef IN_DEBUG
		if(printMe){Serial.printf("*** In upd NPIQ %i of 500\n", npiq); npiq = 0;}
#endif
		return;
	}

	// while the current buffer set is not filled:
	// Move samples into audio buffers, using new packets if needed.
	// It may take up to 3 packets to fill the buffers if the initial one has only a few samples left (min observed samples/pkt = 89).
	int usedPkts = 1;
	queuePkt * pkt;
	while((_currentBuffer < AUDIO_BLOCK_SAMPLES) && (_myQueueI.size() > 0))
	{
		pkt = (queuePkt *)&_myQueueI.front();
		int channels = pkt->hdr.format_nbc + 1;
		int samples = pkt->hdr.format_nbs + 1;
		int available = samples - qUsedSamples;
		int needed = AUDIO_BLOCK_SAMPLES - _currentBuffer;
		int copyThisBlock = min(available, needed);


		if(pkt->hdr.nuFrame != (_lastQFrameNum + 1)) // dropped packet
		{
			framesDropped++;
			if(framesDropped % 200 == 0)
#ifdef IN_DEBUG
				Serial.printf("++++++ In dropped frame %i, %i, q size %i\n", pkt->hdr.nuFrame, _lastQFrameNum, _myQueueI.size());
#else
					;
#endif
		}
		
	//if(printMe) Serial.printf("!FC curBuf %i,used %3i,  need %3i, have %3i(copying %3i), chans %i [pkt %i], qSiz %i, frame %i\n", _currentBuffer, qUsedSamples, needed, available,  copyThisBlock, _inChans, channels, _myQueueI.size(), pkt->hdr.nuFrame);

		//if(printMe) print6pkt(pkt, qUsedSamples, (qUsedSamples + copyThisBlock) * channels);
	
		for (j = 0; j < copyThisBlock; j++)	// does the order matter? Any prefetch should be better this way
		{
			for (i = 0; i < _inChans; i++)
				if(i < channels) 
					new_block[i]->data[j + _currentBuffer] = pkt->c.content16[(j + qUsedSamples) * channels + i]; // 16-bit samples
				else
					new_block[i]->data[j + _currentBuffer] = 0; // not enough incoming channels to supply all the 
		}	
		//if(printMe) print3buf(0, _currentBuffer, _currentBuffer + copyThisBlock);
		
		_currentBuffer +=	copyThisBlock;
		
		if(available > needed) // More than enough data in this packet. Use the rest for next buffer set.
		{
			qUsedSamples += copyThisBlock;	
			if(_currentBuffer != AUDIO_BLOCK_SAMPLES) Serial.println("*** I upd: Full buffer not full");
			// leave _lastQFrameNum alone: coming back to this packet next time
				break; // buffer is full, so straight to transmit
		}

		if (available <= needed) // All samples used - free the packet
		{
			if(available == needed && _currentBuffer != AUDIO_BLOCK_SAMPLES) Serial.println("*** Full buffer not full _B");
			qUsedSamples = 0;
			_lastQFrameNum = pkt->hdr.nuFrame; // frame sequence check
			cli(); // may not be required - already inside software interrupt
				_myQueueI.pop(); // free used queue packet
			sei();
		}

		if (available < needed) //  need to get another packet
		{			
			usedPkts++;
		}
	}	// while - fill buffer set 
	
	
	// transmit full buffers, or come back next time and finish (out of packet warning)
	if (_currentBuffer ==  AUDIO_BLOCK_SAMPLES)  
	{		
		//if(printMe) Serial.printf(" !TX %i, used %i pkts, chans %i\n", _myStreamI, usedPkts, _inChans);
		// if we got 1 block, all  are filled
		for (i = 0; i < _inChans; i++) 
		{
			transmit(new_block[i], i);
		}
		_currentBuffer = 0;
	}
	else{
		didNotTransmit++;
#ifdef IN_DEBUG
		if(printMe) Serial.println("------- !In_update TX! Ran out of queued packets.");	
#endif
		//etherTran.cleanAQ_I(); 
	}
	//if(printMe)	Serial.printf("In pkts %i in Q %i\n",  _mySubI, _myQueueI.size());
	return;
}

// subscribe to a stream from a (or any) host
// stream does not need to be active for subscription
// only 1 subscription per input_net object

// default registration
// register this object with subscriptions, hostname defaults to nullptr
int AudioInputNet::subscribe(char * streamName, char * hostName)
{
	if(_myStreamI != EOQ) // already subscribed
		return _myStreamI;
	int i;
	int emptySlot = EOQ;
	for (i = 0; i < MAX_SUBSCRIPTIONS; i++)
	{
		if(&_myQueueI == etherTran.subsIn[i].qPtr) // redundant(?) saftey check
			return i;
		if(etherTran.subsIn[i].qPtr == nullptr && emptySlot == EOQ) // first empty slot
			emptySlot = i;
	}
	if(emptySlot != EOQ) // there's space
	{
		// _myStreamI will be matched later
		etherTran.subsIn[emptySlot].qPtr = &_myQueueI;
		etherTran.subsIn[emptySlot].protocol = OK_VBAN_AUDIO_PROTO; // only accept 44.1 PCM16
		etherTran.subsIn[emptySlot].active = true;
		strncpy(etherTran.subsIn[emptySlot].streamName, streamName, VBAN_STREAM_NAME_LENGTH-1);
		if(hostName != nullptr)
			strncpy(etherTran.subsIn[emptySlot].hostName, hostName, VBAN_HOSTNAME_LEN-1);
#ifdef IN_DEBUG
		Serial.printf("--Subscribed Audio In to stream '%s', host '%s', slot %i\n", streamName, etherTran.subsIn[emptySlot].hostName, emptySlot);
#endif
		_mySubI = emptySlot;
		return emptySlot;
		// other particulars (e.g IP) will be filled in later
	}
	else
		return EOQ;
}

// subscribe by name/IP
int AudioInputNet::subscribe(char * streamName, IPAddress remoteIP)
{
	if(_myStreamI != EOQ) // already subscribed
			return _myStreamI;
	int i;
	int emptySlot = EOQ;
	for (i = 0; i < MAX_SUBSCRIPTIONS; i++)
	{
		if(&_myQueueI == etherTran.subsIn[i].qPtr) // redundant(?) saftey check
			return i;
		if(etherTran.subsIn[i].qPtr == nullptr && emptySlot == EOQ) // first empty slot
			emptySlot = i;
	}
	if(emptySlot != EOQ) // there's space
	 {
		 etherTran.subsIn[emptySlot].qPtr = &_myQueueI;
		 etherTran.subsIn[emptySlot].active = true;
		 etherTran.subsIn[emptySlot].protocol = OK_VBAN_AUDIO_PROTO;
		 strncpy(etherTran.subsIn[emptySlot].streamName, streamName, VBAN_STREAM_NAME_LENGTH-1);
		 strcpy(etherTran.subsIn[emptySlot].streamName, "?");
		 etherTran.subsIn[emptySlot].ipAddress = remoteIP;
		 #ifdef IN_DEBUG
		 Serial.printf("--Subscribed Audio In to '%s', slot %i, IP ", streamName, emptySlot);
		 Serial.println(remoteIP);
		 #endif
		_mySubI = emptySlot;
		 return emptySlot;
	 }
	return EOQ;
}


void AudioInputNet::unSubscribe(void)  // release the subscribed stream. 
{
	if (_myStreamI >= 0)
	{
		etherTran.streamsIn[_myStreamI].subscription = EOQ;
		etherTran.subsIn[_myStreamI].active = false;
	}
	_myStreamI = EOQ; 	
}

int AudioInputNet::droppedFrames(bool reset)
{
	int temp;
	temp = framesDropped;
	if(reset)
		framesDropped = 0;
	return temp;
}

int AudioInputNet::getPktsInQueue()
{
	return _myQueueI.size();
}
	
int AudioInputNet::missedTransmit(bool reset)
{
	int temp;
	temp = didNotTransmit;
	if(reset)
		didNotTransmit = 0;
	return temp;
}

	
	
// debug print packet contents

void AudioInputNet::print6pkt(queuePkt * pkt, int first, int last)
{
#ifdef IN_DEBUG
	int i;
	Serial.printf("  P6PKT: [%i]", first);
	for(i=0; i< 6; i++)
		Serial.printf("%i ", pkt->c.content16[i]);
	Serial.printf(" ... [%i] ", last);
	for(i=last -6; i< last; i++)
		Serial.printf("%i ", pkt->c.content16[i]);
	Serial.println();
#endif
}

// debug print buffer contents
void AudioInputNet::print3buf(int indx, int first, int last)
{
#ifdef IN_DEBUG
	int i;
	Serial.printf("  P3BUF %i: [%i]", indx, first);
	for(i=0; i< 3; i++)
		Serial.printf("%i ", new_block[indx]->data[i]);
	Serial.printf(" ... [%i] ", last);
	for(i=last -3; i< last; i++)
		Serial.printf("%i ", new_block[indx]->data[i]);
	Serial.println();
#endif
}

#endif