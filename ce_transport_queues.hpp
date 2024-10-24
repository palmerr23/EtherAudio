/*
 * Ethernet (UDP) Network Control object for Teensy Audio Library
 * Handles all Network-side queue management
 * All audio queue manipulation must be with **interrupts off**
 *
 * Interrupts need to be off whenever queue management functions are called from updateNet()
 *
 * Sept 2024 Richard Palmer
 * Released under GNU Affero General Public License v3.0 or later
 * SPDX-License-Identifier: AGPL-3.0-or-later
 */

#ifndef _CONTROLQ_NET_HPP_
#define _CONTROLQ_NET_HPP_

// debug shared with ce_transport

// Set up output packet queues
bool AudioControlEtherTransport::initQueues()
{ 
	if (_initializedQ) 	return true;
	for(int i = 0; i < MAX_UDP_STREAMS; i++)
		qpOut[i] = (std::queue <queuePkt> *)nullptr;
	_initializedQ = true;
	//Serial.println("Queues initialized");
	return true;
}

/****************  Incoming VBAN  packet queuing  ******************/
/*** Audio ****/
// Called by updateNet() - packet is already parsed.
// Update or register stream
// Queue the packet if subscribed and there is queue space, dump otherwise
int AudioControlEtherTransport::queuePacket(pktType type) 
{

	//int pktSize = udp.size();
	const uint8_t *UDPdata = udp.data();
	
	vban_header hdr;	
	memcpy((void*)&hdr, (void*)UDPdata, sizeof(vban_header));
	/*
	uint8_t pktProto, pktFmt;
	pktProto = hdr.format_SR;
	pktFmt = hdr.format_bit;
*/
	int streamID = etherTran.getRegisterStreamId(UDPdata, udp.remoteIP(), type); // register all consumable VBAN streams
#ifdef CE_DEBUG
	//if(type != PKT_AUDIO) Serial.printf("qp: Queue non-audio packet type %i, stream %i, active %i, subs %i, siz %i\n", type, streamID, etherTran.streamsIn[streamID].active, etherTran.streamsIn[streamID].subscription, udp.size() - VBAN_HDR_SIZE);
#endif
	if (streamID < 0) // something went wrong with the registration or it's an output stream
		return 0; 			// dump the packet
	
	etherTran.streamsIn[streamID].lastPktTime = millis(); // register last packet time, even if we don't queue it
	
	//if(etherTran.printMe) Serial.printf("QP: Add %i?\n", streamID);
	bool success = false;	
	if(etherTran.streamsIn[streamID].subscription >= 0)
	{
		//if(etherTran.printMe) Serial.println("  Yes");
		success = etherTran.addPacketToQueue(streamID, type);
	}
#ifdef CE_DEBUG
	if(!success && etherTran.printMe) Serial.printf("**** QpktA: Blk not queued, strm %i, subs %i\n", streamID, etherTran.streamsIn[streamID].subscription);
#endif
		
	if(etherTran.printMe)
	{
		//Serial.printf("ID %i, pkts %i, qpkts %i\n", streamID, pkts, qpkts);
		//Serial.printf("Hdr, proto 0x%02X [0x%02X], fmt 0x%02X [0x%02X], ch %i\n", pktProto, OK_VBAN_AUDIO_PROTO, pktFmt, OK_VBAN_FMT, hdr.format_nbc);
	}
	
	// if hostname not known, flag PING to remote IP. 
	// Careful not to flood requests if remote host is unresponsive.
	return 0;
}


// queue packet  
// Only SUBSCRIBED streams are queued
// For now, only AUDIO (44.1kHz, PCM16), SERVICE (not PING) packets are queued

bool AudioControlEtherTransport::addPacketToQueue(int inStream, pktType type)
{
	static uint32_t qPktsDropped = 0;
	qpkts++;
	//bool etherTran.printMe = (pkts % 500 == 200) && millis() > 4000;
	
	queuePkt qPkt;
	const uint8_t *packet = udp.data();

	vban_header *header = (vban_header *)packet;
	const unsigned char *data = packet;
	
	streamsIn[inStream].lastPktTime = millis();  // register packet time, even if we can't queue it
	
	std::queue <queuePkt> *qPtr = etherTran.subsIn[etherTran.streamsIn[inStream].subscription].qPtr;
#ifdef CE_DEBUG
	int sub = streamsIn[inStream].subscription;
#endif
	if(qPtr == nullptr)
	{
		if(etherTran.printMe || type != PKT_AUDIO)
		{
#ifdef CE_DEBUG
			Serial.printf("Bad subscription stream %i, sub %i, IP ", inStream, sub);
			Serial.println(etherTran.subsIn[sub].ipAddress);
#endif
		}
		return 0;
	}

	static int dumped = 0;
	cli();
		int siz = qPtr->size(); // near enough. Update() may consume 1 or 2 packets before the push() below
	sei();
	//Serial.printf("**** AddPkt2Q UDP packet, stream %i, type %i, Qlen %i, dumped %i, qptr %X\n", inStream, type, qPtr->size(), dumped, qPtr);
	if(siz >= MAX_AUDIO_QUEUE) // dump the packet
	{
		dumped++;
		if(etherTran.printMe) {
#ifdef CE_DEBUG
			Serial.printf("**** AddPkt2Q dumping UDP packet, stream %i,type %i,  AQ len %i, since last time %i\n", inStream, type,  qPtr->size(), dumped);
#endif
			dumped = 0;
		}
		return 0;
	}

	uint32_t streamLastFrame = etherTran.streamsIn[inStream].hdr.nuFrame;
	int channels, samples, dataSize;
	
	qPkt.streamIndx = inStream;
	
	if(type == PKT_AUDIO)
	{
		channels = header->format_nbc + 1;
		samples  = header->format_nbs + 1;
		dataSize = VBAN_HDR_SIZE + samples * channels * BYTES_SAMPLE; 
		qPkt.samplesUsed = 0; // for input object.
	}
	else
	{
		channels = 1;
		samples  = udp.size(); // header + data
		dataSize = samples; 
		qPkt.samplesUsed = samples - VBAN_HDR_SIZE; // just the content size in bytes
#ifdef CE_DEBUG
		//Serial.printf("APQ non A: stream %i, sub %i, type %i, pkt len %i, ptr 0x%04X\n", inStream, sub, type, dataSize, qPtr);
#endif
	}

	memcpy((void*)&qPkt.hdr, (void*)data, dataSize); // byte by byte copy
	//Serial.printf("APQ Queued packet stream %i, fc '%c'\n", inStream, qPkt.c.content[0]);
	//if(etherTran.printMe)Serial.printf("APQ Queued packet stream %i, chans %i, samples %i\n", inStream, channels, samples);
	
	cli();
		qPtr->push(qPkt); // queue it
	sei();
	
	if(type != PKT_AUDIO && 0) 
#ifdef CE_DEBUG
		Serial.printf("**** AddPkt2Q not Audio stream %i, sub %i, pushed %i, pkts qd %i\n", inStream, etherTran.streamsIn[inStream].subscription, dataSize, qPtr->size());
#endif
	
	dumped = 0;
		
	if(qPkt.hdr.nuFrame != (streamLastFrame + 1)) // dropped packet
	{
		qPktsDropped += qPkt.hdr.nuFrame - streamLastFrame;
#ifdef CE_DEBUG
		if(etherTran.printMe) Serial.printf("***** AddPkt2Q dropped frame, tot %i [%i - %i], udp q len %i\n", qPktsDropped, streamLastFrame, qPkt.hdr.nuFrame, qPtr->size() );
#endif
	}
	etherTran.streamsIn[inStream].hdr.nuFrame = qPkt.hdr.nuFrame; // also done  in getRegisterStreamId --> getRegisterStreamId
	return true;
}


// incoming packet to streamsIn matching 
// streamName and IPAddress is definitive - hostname may not (yet) be known
int AudioControlEtherTransport::getRegisterStreamId(const uint8_t * packet, IPAddress remoteIP, pktType type)
{
	//IPAddress remoteIP = remote;
	// search through the registered sreamInfo array for matching hdr.streamname and RemoteIP
	int freeSlot = EOQ;
	int inactiveSlot = EOQ;
	//void *pdata = packet;
	vban_header hdr;
	
	//bool etherTran.printMe = /*Serial && */(qpkts % 1000 == 23) && millis() > 5000; // debug	

	memcpy((void*)&hdr, (void *)packet, sizeof(vban_header));
	
	// only register consumable (44.1, PCM, INT16, AUDIO) and SERVICE (not ID) streams
	//uint8_t proto = hdr.format_SR & VBAN_PROTOCOL_MASK;

	if(type == PKT_AUDIO)
		if (hdr.format_SR == OK_VBAN_AUDIO_PROTO  && hdr.format_bit != OK_VBAN_FMT)
		{
#ifdef CE_DEBUG
			if(printMe) Serial.printf("******* gRS bad audio\n");
#endif
			return EOQ;
		}
		
	if(type == PKT_TEXT)
	{
#ifdef CE_DEBUG
		if(printMe) Serial.printf("******* gRS TEXT packet\n");
#endif
		return EOQ;
	}

	// don't queue ID packets
	if((type == PKT_SERVICE) && (hdr.format_SR == VBAN_SERVICE_ID))
	{
#ifdef CE_DEBUG
		if(printMe) Serial.printf("******* gRS bad Service\n");
#endif
		return EOQ;
	}

	for(int i = 0; i < MAX_UDP_STREAMS; i++)
	{		
		if(etherTran.streamsIn[i].active)
		{  // existing stream
			if(strcmp(hdr.streamname, etherTran.streamsIn[i].hdr.streamname) == 0 && remoteIP == etherTran.streamsIn[i].remoteIP)
			{
				//if(etherTran.printMe) Serial.printf("SR: Found stream %i\n", i);
				// header info may change dynamically (i.e. channels or sampleRate)
				etherTran.registerStreamInPkt(packet, remoteIP, freeSlot, false, type);
				//streamsIn[i].lastPktTime = millis();
				//register
				return i; // found
			}
		}
		else // inactive
		{
			if(freeSlot == EOQ) 
			{
#ifdef CE_DEBUG
				if(printMe) Serial.printf("SR: New %i\n", i);
#endif
					freeSlot = i; // find an empty slot
			}
			else
					if(etherTran.streamsIn[i].lastPktTime < (millis() - DEAD_STREAM_TIME) && inactiveSlot == EOQ) // an input stream with no recent packets
					{
#ifdef CE_DEBUG
						if(printMe) Serial.printf("SR: Inactive %i\n", i);
#endif
						inactiveSlot = i;
					}
		}
	}

	// new stream into a free slot or overwrite an inactive stream
	if (freeSlot >= 0)
	{
		//fill stuff in - copy header to .hdr
		// test for "processablility" and set rest of streams[freeSlot]
		etherTran.registerStreamInPkt(packet, remoteIP, freeSlot, true, type);
		return freeSlot;
	}

	if (inactiveSlot >= 0)
	{
		//fill stuff in - copy header to .hdr
		// test for "processablility" and set rest of streams[inactiveSlot]
		etherTran.registerStreamInPkt(packet, remoteIP, inactiveSlot, true, type); 
		return inactiveSlot;
	}
	
	return EOQ;		// no space left, can't find or register new
}


// name and channels are not stored in the header for input streams
// header info may change dynamically
// **** isNew processing is not yet implemented - may not be required as done in main line???
void AudioControlEtherTransport::registerStreamInPkt(const uint8_t * pdata, IPAddress remoteIP, int slot, bool isNew, pktType type)
{
	if(slot >= MAX_UDP_STREAMS)
		return;

	memcpy((void *)&streamsIn[slot].hdr, (void *)pdata, sizeof(vban_header));
	streamsIn[slot].remoteIP = remoteIP;
	streamsIn[slot].lastPktTime = millis();
	streamsIn[slot].type = type;
	streamsIn[slot].active = true;
}

void	AudioControlEtherTransport::updateActiveStreams()
{
	int count = 0;
	//Serial.println("Update Active streams");
	for(int i = 0; i < MAX_UDP_STREAMS; i++)
	{
		if(streamsIn[i].active)
		{
			count++;
			if(streamsIn[i].subscription == EOQ)
				updateStreamSubscription(i);	
		}
	}
	activeUDPstreams_I = count;
}


// update references from hostsIn to streamsIn/streamsOut 
// activate / update waiting subscriptions
void AudioControlEtherTransport::updateHostStreams(int hostID)
{ 
	int i, j;
	//Serial.printf("Updating hostStream subscriptions: host %i\n", hostID);
	IPAddress remoteIP = hostsIn[hostID].remoteIP;

	// StreamsIn
	for(j = 0; j < MAX_SUBSCRIPTIONS; j++) // find matching subscriptions, update streams->hosts
	{
		// match streamName and IP for active streams. Update subscription if both active and streams
		if(1)
		{
		//	Serial.printf("~~~~Testing subs %i: streams: " , j);
			for(i = 0; i < MAX_UDP_STREAMS; i++)
			{
				if(streamsIn[i].active)
				{
					//Serial.printf("%i: '%s' '%s', " , i, subsIn[j].streamName, streamsIn[i].hdr.streamname);
					if ((streamsIn[i].remoteIP == remoteIP))		// 
					{
						streamsIn[i].hostIndx = hostID;
						if(subsIn[j].active && strncmp(subsIn[j].streamName, streamsIn[i].hdr.streamname, VBAN_STREAM_NAME_LENGTH) == 0 && subsIn[j].protocol == (streamsIn[i].hdr.format_SR & VBAN_PROTOCOL_MASK))
							streamsIn[i].subscription = j;
					 //Serial.printf("~~~~~~found streamIn %i, '%s', host'%s'\n", i, subsIn[j].streamName, hostsIn[hostID].hostName);
					}
				}
			}
		}
	}
	//Serial.println();
	
	//streamsOut 
	for(i = 0; i < MAX_UDP_STREAMS; i++)
	{
		if(streamsOut[i].active)
		{				
			if (streamsOut[i].remoteIP == remoteIP)
			{
				streamsOut[i].hostIndx = hostID;
			  //Serial.printf("~~~~~~found streamOut %i, host'%s'\n", i, hostsIn[hostID].hostName);
			}
		}
	}
}
// activate subscriptions from new stream to known host
// activate waiting subscriptions
void AudioControlEtherTransport::updateStreamSubscription(int streamID)
{ 
	int i, j;
	//Serial.printf("~~~~ Updating new StreamIn %i to host / subscription\n", streamID);

	if(!streamsIn[streamID].active)
		return;
 
	// match subscription host name to stream hostname, where inStream remoteIP = hostIP
	for(j = 0; j < MAX_SUBSCRIPTIONS; j++) // find matching subscriptions, update streams->hosts
	{
		// match streamName, IP and protocol for active streams. Update subscription if  active		
		//Serial.printf("~~~~Testing hosts %i: streams: " , j);
		for(i = 0; i < MAX_REM_HOSTS; i++)
		{
			if(hostsIn[i].active && subsIn[j].active)
			{
				//Serial.printf("j:i %i:%i '%s' =?= '%s';   " , j, i, subsIn[j].hostName, hostsIn[i].hostName);
				if(strcmp(subsIn[j].hostName, hostsIn[i].hostName) == 0 && subsIn[j].protocol == (streamsIn[streamID].hdr.format_SR & VBAN_PROTOCOL_MASK))
				{
					streamsIn[streamID].subscription = j;	
					subsIn[j].streamID = streamID;
#ifdef CE_DEBUG
					Serial.printf("~~~~ updateStrSub: found streamIn %i: sub %i '%s', host %i '%s', proto 0x%2X\n", streamID, j, subsIn[j].hostName, i, hostsIn[i].hostName, subsIn[j].protocol);
#endif
				}
			}
		}
	}
}

/**** housekeeping ***/

// Ensure all active subscriptions are mapped to active streams and vice versa
// ******Could also age streams 
void AudioControlEtherTransport::updateSubscriptions(void)
{
	//Serial.print("~~~~~~ updSubs: ");
	int i, j;
	for(j = 0 ; j < MAX_UDP_STREAMS; j++)
	{
		//if(streamsIn[j].subscription != EOQ) // already subscribed
		//		break;
		//streamsIn[j].subscription = EOQ;
		for (i = 0; i < MAX_SUBSCRIPTIONS; i++)
		{
			// stream names and protocols must match
			if(streamsIn[j].active && subsIn[i].active && (strcmp(streamsIn[j].hdr.streamname, subsIn[i].streamName) == 0) && (streamsIn[j].hdr.format_SR == subsIn[i].protocol))
			{
				// plus: IP address match or hostname match or IP address == any {0.0.0.0}
				if(streamsIn[j].remoteIP == subsIn[i].ipAddress || streamsIn[j].remoteIP == getHostIPfromName(subsIn[i].hostName) || subsIn[i].ipAddress == IPAddress((uint32_t)0))
				{
					streamsIn[j].subscription = i;
					subsIn[i].streamID = j;
					//Serial.printf("~~~~~~ updSubs: matched sub %i with stream %i", i, j);
					break;
				}
				// Or: promiscuous mode - no IP or hostname in subscription
				if(subsIn[j].ipAddress == IPAddress((uint32_t)0) && subsIn[i].hostName[0] == '?')
				{
					streamsIn[j].subscription = i;
					subsIn[i].streamID = j;
					//Serial.printf("~~~~~~ updSubs: promiscuous matched sub %i with stream %i", i, j);
					break;
				}
			}
		}
	}
	//Serial.println();
}

const char * AudioControlEtherTransport::getHostNameFromIP(IPAddress ip)
{
	for(int i = 0; i < MAX_REM_HOSTS; i++)
	{
		if(hostsIn[i].active && hostsIn[i].remoteIP == ip)
			return hostsIn[i].hostName;
	}
		return "*";
}

#endif