/* Multi-channel Ethernet TEXT output for Teensy Audio Library 
 * does NOT take update_responsibility
 * Richard Palmer - 2024
 * Released under GNU Affero General Public License v3.0 or later
 * SPDX-License-Identifier: AGPL-3.0-or-later
 */

/*  
 * Take incoming audio buffers, construct VBAN packets and queue them for output
 * Multiple output objects share the packet queue.
 * All direct I/O handling is in control_ethernet
*/
#ifndef _OUTPUT_SERVICE_HPP_
#define _OUTPUT_SERVICE_HPP_

#include <Arduino.h>
#include "outputService_net.h"

void AudioOutputServiceNet::begin(void)
{
	if(outputBegun)
		return;
	_nextFrame = 0; // initialize packet sequence
	didNotTransmit = 0;

	outputBegun = true;
#ifdef OS_DEBUG
  Serial.println("ON: outputServiceNet.begin() complete");
#endif
}

/* ******* There is no regular update() function as this is not an AudioStream object ********
 * all housekeeping is either in ce_transport::updateNet() or in function calls here
 */

// queue output packet - transmitted by ce_transport

// bool extend for longer data?
bool AudioOutputServiceNet::send(uint8_t *data, int length, char* streamName, uint8_t sType, IPAddress remoteIP)
{
	static queuePkt pkt; // buffer for outgoing packet
	static int otim = 0;
#ifdef OS_DEBUG
	printMe = otim % 50 == 0 &&  millis() > 4000;
	otim++;
#else
			printMe = false;
#endif
#ifdef OS_DEBUG
	if(printMe) 
		Serial.printf("OS_send: stream Out %i len %i, sType %i, '%c' ", _myStreamO, length, sType, (char)*data);	

#endif
	if(length <= 0 || data == nullptr)
		return false;
	
	if(_myStreamO == EOQ || !outputBegun) // uninitialised / unsubscribed
	{	
		otim = 0;
		return  false;
	}

	if(remoteIP == IPAddress((uint32_t)(0)))
		remoteIP = etherTran.getMyBroadcastIP();
	
	if(_myQueueO.size() > MAX_AUDIO_QUEUE)
	{
#ifdef OS_DEBUG
		if(printMe) Serial.println("OS_send: Q overflow, dropped outgoing Service block");
#endif
		return false;
	}

	// hdr VBAN flag is already set
	pkt.hdr.format_SR = VBAN_SERVICE_SHIFTED;
	pkt.hdr.format_nbs = 0;
	pkt.hdr.format_nbc = sType;
	pkt.hdr.format_bit = 0;	
	pkt.hdr.nuFrame = 1; // should probably increment, but for each specific stream
	pkt.samplesUsed = length; 
	strncpy(pkt.hdr.streamname, (streamName) ? streamName : "", VBAN_STREAM_NAME_LENGTH);

	uint8_t *dat = (uint8_t *)&(pkt.c.content[0]);
	//Serial.printf("pkt-dat offset %i [%i], pkt-hdr offset %i\n", dat - (uint8_t *)&pkt, (uint8_t *)&pkt.c.content[0] - (uint8_t *)&pkt,(uint8_t *)&pkt.hdr - (uint8_t *)&pkt);
	memcpy((void*)dat, (void*)data, length); //&(pkt.c.content[0]

	cli(); 
		_myQueueO.push(pkt); // not during update() processing
	sei();
		//_nextFrame++;
#ifdef OS_DEBUG
	if(printMe) Serial.printf("Pushed packet, Qlen %i, ", _myQueueO.size());
	//printHdr(&pkt.hdr);
	//Serial.printf("OS_send: queued len %i, SR 0x%02X, NBC %i, sType %i, len %i. '%c%c%c'\n", length, pkt.hdr.format_SR, pkt.hdr.format_nbc, sType, pkt.samplesUsed, (char)pkt.c.content[0], (char)pkt.c.content[1],(char)pkt.c.content[2]);	
	
#endif
	return true;
}


int AudioOutputServiceNet::subscribe(char * sName, uint8_t sType, IPAddress remoteIP)
{
	if(_myStreamO != EOQ) // already subscribed
			return _myStreamO;
	if(!remoteIP)
		remoteIP = etherTran.getMyBroadcastIP();
	int i;
	int emptySlot = EOQ;
	for (i = 0; i < MAX_UDP_STREAMS; i++)
	{
		if(&_myQueueO ==  etherTran.qpOut[i]) // redundant(?) saftey check
			return i;
		if(etherTran.qpOut[i] == nullptr && emptySlot == EOQ) // first empty slot
			emptySlot = i;
	}
	if(emptySlot != EOQ) // there's space
	{
		 _myStreamO = emptySlot;
		 etherTran.qpOut[emptySlot] = &_myQueueO;
		 strncpy(etherTran.streamsOut[emptySlot].hdr.streamname, sName, VBAN_STREAM_NAME_LENGTH-1);
		 strncpy(_myStreamName, sName, VBAN_STREAM_NAME_LENGTH-1);
		 etherTran.streamsOut[emptySlot].remoteIP = remoteIP;
		 etherTran.streamsOut[emptySlot].hdr.format_SR = VBAN_SERVICE_SHIFTED;
		 etherTran.streamsOut[emptySlot].hdr.format_nbc = sType;
		 etherTran.streamsOut[emptySlot].active = true;
#ifdef OS_DEBUG
		 Serial.printf("Subscribed SERVICE OUT to '%s', slot %i, proto 0x%02X, sType 0x%02X, IP ", sName, emptySlot, etherTran.streamsOut[emptySlot].hdr.format_SR, sType);
		 Serial.println(remoteIP);
#endif
		 return emptySlot;
	}
	return EOQ;
}

int AudioOutputServiceNet::missedTransmit(bool reset)
{
	int temp;
	temp = didNotTransmit;
	if(reset)
		didNotTransmit = 0;
	return temp;
}

void AudioOutputServiceNet::printHdr(vban_header *hdr)
{
#ifdef OS_DEBUG

	Serial.printf("Hdr: '%c' SR 0x%02X, nbs 0x%02X, nbc 0x%02X, bit 0x%02X, stream '%s' frame %i\n", (char)hdr->vban, hdr->format_SR, hdr->format_nbs, hdr->format_nbc, hdr->format_bit, hdr->streamname, hdr->nuFrame);

#endif
}

void AudioOutputServiceNet::printContent(uint8_t *buff, int len)
{
#ifdef OS_DEBUG
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