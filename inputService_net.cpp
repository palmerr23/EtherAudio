/* Multi-channel Network TEXT input for Teensy Audio Library 
 * does NOT take update_responsibility
 * Changes to this file should possibly be mirrored in inputMIDI_net.cpp
 * Richard Palmer (C) 2024
 *
 * Released under GNU Affero General Public License v3.0 or later
 * SPDX-License-Identifier: AGPL-3.0-or-later
 */


#ifndef _INPUT_SERVICE_NET_HPP_
#define _INPUT_SERVICE_NET_HPP_

#include <Arduino.h>
#include "inputService_net.h"

void AudioInputServiceNet::begin(void)
{
	_currentPkt_I = 0; // initialize packet sequence

	inputBegun = true;
#ifdef IS_DEBUG
	Serial.printf("IN: inputServiceNet.begin() complete\n" );
#endif
}

/* ******* There is no regular update() function as this is not an AudioStream object ********
 * all housekeeping is either in ce_transport::updateNet() or in function calls here
 */

// number of queued packects
bool AudioInputServiceNet::available(void) 
{
	if(etherTran.subsIn[_mySubI].streamID == EOQ) // don't provide data until subscription is active 
		return false;
		
	return _myQueueI.size(); 
}

// without check - why?
int AudioInputServiceNet::getPktsInQueue()
{
	//Serial.printf("IS: gPIQ Queued %i\n", _myQueueI.size());
	return _myQueueI.size();
}

// assumes available(), size of next packet data (excluding headers)
int AudioInputServiceNet::dataSize(void)
{
		return (_myQueueI.size() == 0) ? 0 : _myQueueI.front().samplesUsed; 
}

queuePkt AudioInputServiceNet::getPkt()
{
	static queuePkt _pkt;	// buffer for popped incoming packet
	
	//Serial.printf("+>+>+>++ Getting pkt, qptr %X\n", _myQueueI);
	if(_myQueueI.size() == 0) // don't provide data until subscription is active 
		return _pkt; // Return rubbish. You should have checked getPktsInQueue() first!
		
	//Serial.printf("+++++ Getting pkt, qptr %X\n", _myQueueI);
	// extract data length from packet size
	cli(); // not called by update()
	_pkt = _myQueueI.front();
	//	memcpy((void*)&_pkt, (void*)&(_myQueueI.front()), sizeof(_pkt)); // whole packet
		_myQueueI.pop();
	sei();
#ifdef IS_DEBUG
//		Serial.printf("Got a Service Pkt '%c', qptr %X\n", _pkt.c.content[0], _myQueueI);
	#endif
	return _pkt;
}

//  There is no update() function 

// subscribe to a stream from a (or any) host
// stream does not need to be active for subscription
// only 1 subscription per input_net object

// default registration
// register this object with subscriptions, hostname defaults to nullptr
int AudioInputServiceNet::subscribe(char * streamName, uint8_t sType, char * hostName)
{
	if(_myStreamI != EOQ) // already subscribed
		return _myStreamI;
	int i;
	int emptySlot = EOQ;
	for (i = 0; i < MAX_SUBSCRIPTIONS; i++)
	{
		if(&_myQueueI == etherTran.subsIn[i].qPtr) // already subscribed
			return i;
			
		if(etherTran.subsIn[i].qPtr == nullptr && emptySlot == EOQ) // first empty slot
			emptySlot = i;
	}
	if(emptySlot != EOQ) // there's space
	{
		// _myStreamI will be matched later
		etherTran.subsIn[emptySlot].qPtr = &_myQueueI;
		etherTran.subsIn[emptySlot].protocol = VBAN_SERVICE_SHIFTED;
		etherTran.subsIn[emptySlot].serviceType = sType;
		etherTran.subsIn[emptySlot].active = true;
		strncpy(etherTran.subsIn[emptySlot].streamName, streamName, VBAN_STREAM_NAME_LENGTH-1);
		if(hostName != nullptr)
			strncpy(etherTran.subsIn[emptySlot].hostName, hostName, VBAN_HOSTNAME_LEN-1);
#ifdef IS_DEBUG
		Serial.printf("--Subscribed to Service In stream  '%s', host '%s', slot %i, queue 0x%04X\n", streamName, etherTran.subsIn[emptySlot].hostName, emptySlot,etherTran.subsIn[emptySlot].qPtr );
#endif
		_mySubI = emptySlot;
		return emptySlot;
		// other particulars (e.g IP) will be filled in later
	}
	else
		return EOQ;
}

// subscribe by name/IP
int AudioInputServiceNet::subscribe(char * streamName, uint8_t sType, IPAddress remoteIP)
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
		 etherTran.subsIn[emptySlot].protocol = VBAN_SERVICE_SHIFTED;
		 etherTran.subsIn[emptySlot].serviceType = sType;
		 strncpy(etherTran.subsIn[emptySlot].streamName, streamName, VBAN_STREAM_NAME_LENGTH-1);
		 etherTran.subsIn[emptySlot].ipAddress = remoteIP;
#ifdef IS_DEBUG
		 Serial.printf("Subscribed Service In to '%s', slot %i, IP ", streamName, emptySlot);
		 Serial.println(remoteIP);
#endif
		_mySubI = emptySlot;
		 return emptySlot;
	 }
	return EOQ;
}


void AudioInputServiceNet::unSubscribe(void)  // release the subscribed stream. 
{
	if (_myStreamI >= 0)
	{
		etherTran.streamsIn[_myStreamI].subscription = EOQ;
		etherTran.subsIn[_myStreamI].active = false;
	}
	_myStreamI = EOQ; 	
}

int AudioInputServiceNet::droppedFrames(bool reset)
{
	int temp;
	temp = framesDropped;
	if(reset)
		framesDropped = 0;
	return temp;
}



void AudioInputServiceNet::printHdr(vban_header *hdr)
{
#ifdef IS_DEBUG
	Serial.printf("ON: Hdr: '%c' proto+rate 0x%02X, samples %i, chans %i fmt_bit 0x%02X, '%s' fr %i\n", (char)hdr->vban, hdr->format_SR, hdr->format_nbs +1, hdr->format_nbc +1, hdr->format_bit, hdr->streamname, hdr->nuFrame);
#endif
}

void AudioInputServiceNet::printContent(uint8_t *buff, int len)
{
#ifdef IS_DEBUG
	int i;	
	Serial.printf("Service pkt content len %i: ", len);
	for(i=0; i < 6; i++)
		Serial.printf("%02X ", buff[i]);
	Serial.printf(" ... [%i] ", len);
	for(i=len -6; i < len; i++)
		Serial.printf("%02X ", buff[i]);
	Serial.println();	
#endif
}

#endif