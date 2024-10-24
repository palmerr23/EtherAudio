/* Multi-channel Network MIDI input for Teensy Audio Library 
 * does NOT take update_responsibility
 * Changes to this file should possibly be mirrored in inputText_net.cpp
 * Richard Palmer (C) 2024
 *
 * Released under GNU Affero General Public License v3.0 or later
 * SPDX-License-Identifier: AGPL-3.0-or-later
 */


#ifndef _INPUT_MIDI_NET_HPP_
#define _INPUT_MIDI_NET_HPP_

#include <Arduino.h>
#include "inputMIDI_net.h"

void AudioInputMIDINet::begin(void)
{
	_currentPkt_I = 0; // initialize packet sequence
	_myStreamI = -1; 	// **** should be unsubscribed: -1

	inputBegun = true;
	
	Serial.printf("IN: inputMIDINet.begin() complete\n" );
}

//  There is no regular update() function

// subscribe to a stream from a (or any) host
// stream does not need to be active for subscription
// only 1 subscription per input_net object

// default registration
// register this object with subscriptions, hostname defaults to nullptr
int AudioInputMIDINet::subscribe(char * streamName, char * hostName)
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
		etherTran.subsIn[emptySlot].protocol = VBAN_MIDI_SHIFTED;
		etherTran.subsIn[emptySlot].active = true;
		strncpy(etherTran.subsIn[emptySlot].streamName, streamName, VBAN_STREAM_NAME_LENGTH-1);
		if(hostName != nullptr)
			strncpy(etherTran.subsIn[emptySlot].hostName, hostName, VBAN_HOSTNAME_LEN-1);
		Serial.printf("--Subscribed to stream '%s', host '%s', slot %i\n", streamName, etherTran.subsIn[emptySlot].hostName, emptySlot);
		_mySubI = emptySlot;
		return emptySlot;
		// other particulars (e.g IP) will be filled in later
	}
	else
		return EOQ;
}

// subscribe by name/IP
int AudioInputMIDINet::subscribe(char * streamName, IPAddress remoteIP)
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
		 etherTran.subsIn[emptySlot].protocol = VBAN_MIDI_SHIFTED;
		 strncpy(etherTran.subsIn[emptySlot].streamName, streamName, VBAN_STREAM_NAME_LENGTH-1);
		 etherTran.subsIn[emptySlot].ipAddress = remoteIP;
		 Serial.printf("--Subscribed to '%s', slot %i, IP ", streamName, emptySlot);
		 Serial.println(remoteIP);
		_mySubI = emptySlot;
		 return emptySlot;
	 }
	return EOQ;
}

/*
// Not for end-users - subscribe directly to a streamID
int AudioInputMIDINet::subscribe(int streamID) // forced subscribe directly to an audio stream	
{
	if(etherTran.streamsIn[streamID].active)
		return streamID;
	_myStreamI = streamID; 
	etherTran.streamsIn[streamID].subscription = ???
	etherTran.subsIn[streamID].qPtr = &_myQueueI;
	Serial.printf(" **** Direct subscribed %i\n", streamID);
	return streamID; 
} 
*/
void AudioInputMIDINet::unSubscribe(void)  // release the subscribed stream. 
{
	if (_myStreamI >= 0)
	{
		etherTran.streamsIn[_myStreamI].subscription = EOQ;
		etherTran.subsIn[_myStreamI].active = false;
	}
	_myStreamI = EOQ; 	
}

int AudioInputMIDINet::droppedFrames(bool reset)
{
	int temp;
	temp = framesDropped;
	if(reset)
		framesDropped = 0;
	return temp;
}

int AudioInputMIDINet::getPktsInQueue()
{
	return _myQueueI.size();
}
	
	
// debug print packet contents
void AudioInputMIDINet::print6pkt(queuePkt * pkt, int first, int last)
{
	int i;
	Serial.printf("  P6PKT: [%i]", first);
	for(i=0; i< 6; i++)
		Serial.printf("%c ", pkt->c.content[i]);
	Serial.printf(" ... [%i] ", last);
	for(i=last -6; i< last; i++)
		Serial.printf("%c ", pkt->c.content[i]);
	Serial.println();
}



void AudioInputMIDINet::printHosts()
{
	for(int i = 0; i < MAX_REM_HOSTS; i++)
	{
		if(etherTran.hostsIn[i].active)
		{
			Serial.printf("Host %i, '%s' ",i, etherTran.hostsIn[i].hostName);
			Serial.println (etherTran.hostsIn[i].remoteIP);
		}
	}
}


#endif