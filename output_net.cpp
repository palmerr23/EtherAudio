/* Multi-channel Ethernet output for Teensy Audio Library 
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
#ifndef _OUTPUT_NET_HPP_
#define _OUTPUT_NET_HPP_

#include <Arduino.h>
#include "output_net.h"




void AudioOutputNet::begin(void)
{
	if(outputBegun)
		return;
	_nextFrame = 0; // initialize packet sequence
	didNotTransmit = 0;
	// make sure local audio block pointers aren't pointing
	for (int i = 0; i < MAXCHANNELS; i++)
	{		
		block[i] = NULL;

	}

	//updateStreamMsgCntr = 371; // set up streamInfo counter
	outputBegun = true;

  Serial.println("ON: outputNet.begin() complete");

}

/******************** update **************************/
// place any avaiable incoming AudioStream buffers into the queue
// we won't transmit zero packets if there are none provided by AudioStream
void AudioOutputNet::update(void)
{
	static int otim = 0;
#ifdef ON_DEBUG
	printMe = otim % 500 == 0 &&  millis() > 4000;
	otim++;
#else
		printMe = false;
#endif
	
	if(!etherTran.linkIsUp())
		return;
	
	if(_myStreamO == EOQ || !outputBegun) // uninitialised / unsubscribed
	{
#ifdef ON_DEBUG
		//if(printMe) Serial.printf("O Down %i %i\n", _myStreamO, outputBegun);
#endif
		otim = 0;
		return;
	}
	//if(printMe) Serial.print("^^^^O: ");
	for(int i = 0; i < _outChans; i++)
	{
#ifdef ON_DEBUG
		//if(printMe) Serial.print(" | ");
#endif
		block[i] = receiveReadOnly(i);
		
	}

	queueBlocks();
	
	//if(printMe) Serial.printf("O: Queued, len %i\n" ,_myQueueO.size());
	for(int i = 0; i < _outChans; i++)
	{ 
		if(block[i]) // will be null if no patchCord connected
			release(block[i]);
	}
		//if(printMe) Serial.printf("O: Released, Audiomem %i\n", AudioMemoryUsage());
}

// queue output blocks
// split streams with more than CHANS_2_PKTS into two equal packets
bool AudioOutputNet::queueBlocks(void)
{
	if(_myStreamO == EOQ) // just to be safe
		return false;
	if(_myQueueO.size() > MAX_AUDIO_QUEUE)
	{
#ifdef ON_DEBUG
		if(printMe) Serial.printf("Dropped outgoing audio block");
#endif
		return false;
	}
	
	queuePkt pkt;
	int samplesProc = 0;
	// pack blocks into one or two VBAN packets
	int samplesPkt = SAMPLES_BUF;
	if(_outChans > CHANS_2_PKTS)
		samplesPkt = SAMPLES_BUF/2;
	
	// hdr VBAN flag is set
	pkt.hdr.format_SR = OK_VBAN_AUDIO_PROTO;
	pkt.hdr.format_nbs = samplesPkt - 1;
	pkt.hdr.format_nbc = _outChans - 1;
	pkt.hdr.format_bit = OK_VBAN_FMT;	
	strncpy(pkt.hdr.streamname, 	etherTran.streamsOut[_myStreamO].hdr.streamname, VBAN_STREAM_NAME_LENGTH-1);

	//if(printMe) printHdr(&pkt.hdr);
	//if(printMe) Serial.printf("OUT chans %i, block ptrs 0x%04x 0x%04x\n", _outChans, block[0], block[1]);

	int16_t *dat = pkt.c.content16;
	while(samplesProc < SAMPLES_BUF) // lots of channels --> 2 packets
	{
		for(int j = 0; j < samplesPkt; j++)
		{
			for(int i = 0; i < _outChans; i++)
			{
				if(block[i] == nullptr)
				{
					*(dat + j * _outChans + i) = 0;
				}
				else
				{		
					int16_t *bdp = 	block[i]->data;
					*(dat + j * _outChans + i) = *(bdp + samplesProc);
				}
			}
			samplesProc++;
		}
		//queue frame for transmit
		//if(printMe)	printSamples(pkt.c.content16, samplesPkt, _outChans);
		pkt.hdr.nuFrame = _nextFrame;
		cli(); // may not be required - already in software interrupt
			_myQueueO.push(pkt);
		sei();
		_nextFrame++;

	} // while
	return true;
}

/*
int subscribe(char *streamName, char *hostName) {}
 is not yet implemented as there is no subscription structure for outputs, so belated hostName to IP resolution can't be effected.
 subsOut and HostsOut arrays could be added to resolve this issue.
 updateActiveStreams() should process these.
*/

int AudioOutputNet::subscribe(char * sName, IPAddress remoteIP)
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
		 etherTran.streamsOut[emptySlot].hdr.format_SR = OK_VBAN_AUDIO_PROTO;
		 etherTran.streamsOut[emptySlot].active = true;
		 #ifdef ON_DEBUG
		 Serial.printf("-~~~~-Subscribed Audio out to '%s', slot %i, IP ", sName, emptySlot);
		 Serial.println(remoteIP);
		 #endif
		 return emptySlot;
	}
	return EOQ;
}


int AudioOutputNet::missedTransmit(bool reset)
{
	int temp;
	temp = didNotTransmit;
	if(reset)
		didNotTransmit = 0;
	return temp;
}

void AudioOutputNet::printHdr(vban_header *hdr)
{
#ifdef ON_DEBUG
			Serial.printf("ON: Hdr: '%c' proto+rate 0x%02X, samples %i, chans %i fmt_bit 0x%02X, '%s' fr %i\n", (char)hdr->vban, hdr->format_SR, hdr->format_nbs +1, hdr->format_nbc +1, hdr->format_bit, hdr->streamname, hdr->nuFrame);
#endif
}

void AudioOutputNet::printSamples(int16_t *buff, int samps, int chans)
{
#ifdef ON_DEBUG
		int i;	
		int bl = samps * chans;
		Serial.printf("  P_SAMP samps %i, chans %i: ", samps, chans);
		for(i=0; i < 6; i++)
			Serial.printf("%i ", buff[i]);
		Serial.printf(" ... [%i] ", bl);
		for(i=bl -6; i < bl; i++)
			Serial.printf("%i ", buff[i]);
		Serial.println();	
#endif
}


#endif