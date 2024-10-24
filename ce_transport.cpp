/*
 * Ethernet (UDP) Network Control object for Teensy Audio Library
 * This file handles low level communications management, interface setup and all Datagram traffic
 * Handles all Network-side queue management (ce_transport_queues.hpp)
 * All audio queue manipulation must be with **interrupts off**
 *
 * Relies on QNEthernet library for Teensy 4.1 Shawn Silverman
 *
 * Sept 2024 Richard Palmer
 * Released under GNU Affero General Public License v3.0 or later
 * SPDX-License-Identifier: AGPL-3.0-or-later
 */


#include <Arduino.h>
#include "control_ethernet.h"
#include "ce_transport.h"
#include <QNEthernet.h>


using namespace qindesign::network;

AudioControlEtherTransport etherTran; 

#if defined CTRL_ETHERNET_DO_LOOP_IN_YIELD
	#if defined(__has_include)
		#if __has_include(<EventResponder.h>)
		#define HAS_EVENT_RESPONDER
		#include <EventResponder.h>
							using yieldFunc_t = void(*)();
							void addYieldFunction(yieldFunc_t _yieldFunc) // pass a pointer to the function you want to be called from yield
							{
								static yieldFunc_t yieldFunc = _yieldFunc; // store the passed in function pointer
								static EventResponder er;                  // define a new EventResponder which will handle the calls.

								er.attach([](EventResponderRef r) {        // we can not directly attach our function to the responder since we need to retrigger the repsonder
									yieldFunc();                             // instead we attach an inline defined relay function as lambda expression
									r.triggerEvent();                        // the relay calls our function and afterwards triggers the responder to schedule the next call.
								});

								er.triggerEvent();                         // to start the call chain we need to trigger the responder once
							}
		#endif  // __has_include(<EventResponder.h>)
	#endif  // defined(__has_include)
#endif  // QNETHERNET_DO_LOOP_IN_YIELD

EthernetUDP udp{QN_PKT_QUEUE};

// debug
int pkts = 0;
int qpkts = 0;

#include "ce_transport_queues.hpp" // additional code


static void updateNet(void);

// Ethernet initialization
bool AudioControlEtherTransport::begin()
{
	udpDroppedPkts = 0;
	VBpktsProc = 0;
	printMe = false;
	if(!_udpPort)
	_udpPort = VBAN_UDP_PORT;

#ifdef CE_DEBUG
	Serial.print("CE: BEGIN ");
#endif
	initQueues(); // before packets start appearing
	
	// start the execution of updateNet() on yield() and delay()
	// see QNEthernet.cpp and https://github.com/luni64/TeensyHelpers
#ifdef CTRL_ETHERNET_DO_LOOP_IN_YIELD // otherwise, call updateNet ***often*** from mainline code
	#if defined(HAS_EVENT_RESPONDER)
			addYieldFunction(updateNet);
	#endif
#endif

	// **** do all other initialisation before starting ethernet - as process  may abort on failure
	etherTranBegun = etherStart(); // no further ethernet or packet processing if failed
#ifdef CE_DEBUG
	Serial.println("CE: begin() complete");
#endif
  return etherTranBegun;
} // begin


// should we process packets?
bool AudioControlEtherTransport::linkIsUp(void) 
{ 
	return (etherTran.etherTranBegun && Ethernet.linkState()); // begin() has been successful and still connected 
}

// start or restart network services
// *** active restart handling disabled for now ***
bool AudioControlEtherTransport::etherStart(void) 
{ 
	//static bool udpBegun = false; // static needed if restart calls this code
	//static bool MDNSbegun = false;
  //static bool etherBegun = false;
	bool myLinkOn; 
#ifdef CE_DEBUG
  Serial.println("----> EtherStart");
#endif

	if(!Ethernet.begin())
	{
		Serial.println("EtherStart: Failed to start Ethernet");
		myLinkOn = false;
		return false;
	}
	//else
	//	etherBegun = true;

	if (!Ethernet.waitForLocalIP(DHCP_TIMEOUT)) {
    printf("EtherStart: Failed to get IP address from DHCP\r\n");
    return false;
  }
	 Ethernet.macAddress(_myMAC); // reads rather than setting 
	 updateIP();

	Serial.println(_myIP);
	Serial.println(_myBroadcastIP);

  myLinkOn = Ethernet.linkState(); 
//  ethernetEnabled = myLinkOn;	
	if(!myLinkOn) 
	{
		Serial.println("EtherStart: Ethernet link is not yet active...");
		return false;
  }

#ifdef USE_MDNS
	strcpy(_FQDN, _VBANhostName);
	strcat(_FQDN, ".local");
	#ifdef CE_DEBUG
		Serial.printf("MDNS starting with hostname '%s'\n", _FQDN);
	#endif
	if(!MDNS.begin(_VBANhostName))
	{
		 Serial.println("ERROR: Starting mDNS.");
		 return false;
	} else {
    if (!MDNS.addService("_osc", "_udp", _udpPort)) {
      Serial.println("ERROR: Adding MDNS responder service.");
			return false;
    } else {
			//MDNSbegun = true;
	#ifdef CE_DEBUG
			Serial.printf("Started mDNS service:\r\n"
										"    Name: %s\r\n"
										"    Type: _osc._udp\r\n"
										"    Port: %u\r\n",
										_VBANhostName, _udpPort);
	#endif
    }
  }
	//Serial.printf("MDNS hostname is '%s'\n", MDNS.hostname() );
	//Serial.printf("MDNS is %srunning \n",((bool)MDNS) ? "" : "not ");
#endif

	// don't start listening to the UDP port until ready to process packets (see updateNet()
#ifdef CE_DEBUG
		Serial.printf("UDP will listen on port %i\n", etherTran._udpPort);
#endif
	if(!udp.begin(etherTran._udpPort))
	{
		Serial.println("Failed to start UDP listener");
	}
	//else
	//	udpBegun = true;
	myLinkOn = Ethernet.linkState(); 
#ifdef CE_DEBUG
	Serial.printf("CE: etherStart complete. Status = '%s'\n", (myLinkOn ) ? "Conn" : "Not Conn"); 
#endif
 return true;
}

void AudioControlEtherTransport::updateIP(void)
{
	_myIP = Ethernet.localIP();
	_myBroadcastIP = _myIP;
	_myBroadcastIP[3] = 255;
}
/**************** ERSATZ UPDATE ***********************/
/* There is no regular update() function as this is not an AudioStream object
 * DO NOT call yield() or delay() from here or any routine called from this function
 * updateNet() is called transparently from yield(), delay() and at end of each mainline code execution, see addYieldFunction() above
 * 	Process any incoming UDP packets. ***Blocks*** until all queued packets are processed
 * 	Periodically perform host, stream and subscription housekeeping
*/

static void updateNet(void) //AudioControlEtherTransport::
{
	//static uint32_t lastUpdateNet;
	static int udpDiscardedPackets = 0;
	static uint32_t lastHousekeeping;	

	static uint32_t lastLinkTestTime;
	//static uint32_t lastConnect = 0;	

	
	if(!etherTran.etherTranBegun) // no processing until after begin() completes
		return;
		
	etherTran.printMe = false; // 
	//etherTran.eprintMe = false;
#ifdef CE_DEBUG
	 static uint32_t ccc = 0;  
	 if((millis() - ccc) > 2000) 
	 {
		etherTran.printMe = true;
		ccc = millis();
	 }
#endif
		// don't start processing until packets	might arrive
	if(!etherTran.linkIsUp()) 
	{	
		return; // abort further processing while link is down
		// *** abort any attempt to actively reconnect for now
#ifdef CE_DEBUG
		if(etherTran.printMe) Serial.printf("No ethernet link begun %i, state %i\n", etherTran.etherTranBegun, Ethernet.linkState());
#endif
		// try to reconnect if last try was long enough ago
		if((millis() - lastLinkTestTime) > DISCONNECT_RETRY)
		{
			Serial.println("Ethernet: waiting for re-connection");
			// etherTran.linkIsActive = etherTran.etherStart();
			lastLinkTestTime = millis();
		}
	//	if(!etherTran.linkIsUp()) return;
	}
	else
		lastLinkTestTime = millis();

//if(etherTran.printMe) Serial.println("UN: process pkts");
	// dump packets from overlong input queues
	int udpQLen = udp.receiveQueueSize();
	while(udpQLen >= QN_PKT_QUEUE)
	{
#ifdef CE_DEBUG
		Serial.printf("********UDP queue is full %i, popping one\n", udpQLen);
#endif
		udp.parsePacket();
		udpQLen = udp.receiveQueueSize();
		udpDiscardedPackets++;
	}

	int updDP = udp.droppedReceiveCount();
	
	if(updDP > (etherTran.udpDroppedPkts + 50))
	{
#ifdef CE_DEBUG
		Serial.printf("****** UDP dropped another 50 pkts. Total discarded %i %i \n", updDP, etherTran.udpDroppedPkts);
#endif
		etherTran.udpDroppedPkts = updDP + 50;
	}

	int pktSize = udp.parsePacket();

	while(pktSize >= VBAN_HDR_SIZE) // queue any consumable VBAN packets
	{
		etherTran.VBpktsProc++;
	//	etherTran.printMe = (etherTran.VBpktsProc % 500 == 0) && (millis() > 4000);	
		
		const uint8_t *UDPdata = udp.data();
		
		vban_header hdr;	// ******************* could use pointer here
		memcpy((void*)&hdr, (void*)UDPdata, sizeof(vban_header));
		
		//const unsigned char *dptr;
	//	vban_ping vbp;
		pktType pktType = etherTran.packetTest(hdr);

		switch (pktType)
		{
			case PKT_AUDIO :
				etherTran.queuePacket(PKT_AUDIO);
				break;
			
			case PKT_SERVICE : // not yet implemented
#ifdef CE_DEBUG
				if(etherTran.printMe) Serial.println("UN: Q Service Packet");
#endif
				etherTran.queuePacket(PKT_SERVICE);
				break;
			
			case PKT_CHAT : // not yet implemented
#ifdef CE_DEBUG
				//Serial.println("UN: Q Chat Packet");
#endif
				//if(etherTran.printMe) 
					etherTran.queuePacket(PKT_CHAT); // string is NOT null-terminated.
				break;
				
			case PKT_PING : // handle immediately
#ifdef CE_DEBUG
				//if(etherTran.printMe) 
					Serial.println("UN: PING Pkt");
#endif
				etherTran.processIncomingPing(udp.remoteIP(), hdr);
				etherTran.updateSubscriptions();	// fix subscriptions by hostname
				//etherTran.printHosts();
				break;
				
			default : // PKT_NOT_CONSUMED
#ifdef CE_DEBUG
				Serial.printf("UN: Unknown pkt: Proto 0x%X\n", hdr.format_SR);	
#endif
				break;	
		}
		pktSize = udp.parsePacket(); // next waiting UDP packet
	}

	// ************* OUTPUT ALL  QUEUED PACKETS  ******************	
	etherTran.sendPkts(); 

		
	// regular housekeeping
	if(millis() - lastHousekeeping < HOUSEKEEPING_EVERY)
		return;
	lastHousekeeping = millis();
	
	//Serial.println("UN: Update Active Streams");
	etherTran.updateActiveStreams();
	etherTran.updateSubscriptions();
	
	// send PING for unknown remoteIP addresses - how to avoid pinging one dead host continuously queue? 
	etherTran.pingUnknownHosts();
	
		return; // done housekeeping for now

} // updateNet


// test for consumable VBAN AUDIO and SERVICE packets (to be queued)
// register all VBAN hosts
// all other packets will be not be processed further
pktType AudioControlEtherTransport::packetTest(vban_header hdr)
{
	if(hdr.vban != VBAN_FLAG)
		return PKT_NOT_CONSUMED;
	
	// register all hosts, even if not consuming packets
	if(getHostIDfromIP(udp.remoteIP()) == EOQ)
		addHost(udp.remoteIP());
	
	uint8_t proto = hdr.format_SR & VBAN_PROTOCOL_MASK;
	switch (proto)
	{	
		case VBAN_AUDIO_SHIFTED :
			if (hdr.format_SR == OK_VBAN_AUDIO_PROTO  && hdr.format_bit == OK_VBAN_FMT)
				return PKT_AUDIO;

		case VBAN_SERVICE_SHIFTED : 
			if (hdr.format_nbc == VBAN_SERVICE_ID)
			{
#ifdef CE_DEBUG	
				Serial.println("PT: Ping");
#endif
				return PKT_PING;
			}
			else
				if (hdr.format_nbc == VBAN_SERVICE_CHAT)
				{
#ifdef CE_DEBUG	
					if(etherTran.printMe) Serial.printf("PT: CHAT len = %i\n", udp.size() - VBAN_HDR_SIZE);
#endif
					return PKT_CHAT;
				}
#ifdef CE_DEBUG	
			if(etherTran.printMe) Serial.printf("PT: Service %i\n", hdr.format_nbc);
#endif
			return PKT_SERVICE;
			
			
			case VBAN_SERIAL_SHIFTED : // not implemented				
				if(hdr.format_bit == VBAN_MIDI_SHIFTED)
				{
#ifdef CE_DEBUG	
					Serial.println("PT: MIDI");
#endif
					return PKT_MIDI;
				}
#ifdef CE_DEBUG	
				Serial.println("PT: Serial");
#endif
				return PKT_SERIAL;
				
			case VBAN_TEXT_SHIFTED : // not implemented
#ifdef CE_DEBUG	
				Serial.println("PT: Text");
#endif
				return PKT_TEXT;
		
			default:
			return PKT_NOT_CONSUMED;
	}
	
}

void AudioControlEtherTransport::sendPkts() // Ethernet/UDP specific volatile int * queue, int actStr
{
	//queuePkt *pkt;
	std::queue <queuePkt> *qp;
	// loop through subscriptions and empty each queue
	for(int i = 0; i < MAX_UDP_STREAMS; i++)
	{
		if(streamsOut[i].active && qpOut[i] != nullptr)
		{
			qp = qpOut[i];
			if(qp->size() > 0) // just send one packet per cycle
			{
				uint8_t *pkt  = (uint8_t *)&(qp->front().hdr.vban); // only transmit the VBAN + content portion of the queued packet
				queuePkt *qqp = (queuePkt *)&(qp->front());

				// only send correct size packet
				int len;
				if(qqp->hdr.format_SR == OK_VBAN_AUDIO_PROTO)
					len = (qqp->hdr.format_nbs + 1) * (qqp->hdr.format_nbc + 1) * BYTES_SAMPLE + VBAN_HDR_SIZE;
					//len = sizeof(vbanPkt);
				else
				{
					len = qqp->samplesUsed + VBAN_HDR_SIZE;
				}
				
				if(udp.send(streamsOut[i].remoteIP, VBAN_UDP_PORT, pkt, len))
				{
					streamsOut[i].lastPktTime = millis();				
					qp->pop();		
				}
				else
#ifdef CE_DEBUG	
					if(printMe) Serial.println("^^^^Did not send");
#endif	
				return;
			}
		}
	}
} 

int AudioControlEtherTransport::getHostIDfromIP(IPAddress ip)
{
	for(int i = 0; i < MAX_REM_HOSTS; i++)
		if(streamsIn[i].remoteIP == ip)
			return i;	
	return EOQ;
}

const char* AudioControlEtherTransport::getHostNamefromID(int id)
{
	if (id >= MAX_REM_HOSTS || !hostsIn[id].active)
		return "*";
	return hostsIn[id].hostName;
}

IPAddress AudioControlEtherTransport::getHostIPfromName(char * hostName)
{
	for(int i = 0; i < MAX_REM_HOSTS; i++)
		if(strncmp(hostsIn[i].hostName, hostName, VBAN_HOSTNAME_LEN))
			return hostsIn[i].remoteIP;	
	return IPAddress((uint32_t)0);
}

// add new host IP
int AudioControlEtherTransport::addHost(IPAddress remoteIP)
{
	int empty = EOQ;
	for(int i = 0; i < MAX_REM_HOSTS; i++)
	{
		if(hostsIn[i].remoteIP == remoteIP)
			return i;
		if(empty == EOQ && !hostsIn[i].active)
			empty = i;
	}
	if(empty == EOQ) // no free slots
		return EOQ;
		
	hostsIn[empty].remoteIP = remoteIP;
	hostsIn[empty].active = true;
	strcpy(hostsIn[empty].hostName, "*");
	return empty;
}

// Incoming PING packet hostname to IP address update
// If it's a PING request, reply.
int AudioControlEtherTransport::processIncomingPing(IPAddress remoteIP, vban_header vbh)
{
	const uint8_t *dptr = udp.data() + VBAN_HDR_SIZE;
	vban_ping vbp;
	memcpy((void*)&vbp, (void*)dptr, sizeof(vban_ping));
	
	int i = addHost(remoteIP); // will just return index if already there
	if(i == EOQ)
		return EOQ;	

	// All PING packets have the host's information
#ifdef CE_DEBUG	
	Serial.printf("-- Registering host %i %s\n", i, vbp.HostName_ascii);
	Serial.printf("-- Colour 0x%04x\n", vbp.color_rgb);
#endif
	strncpy(hostsIn[i].hostName, vbp.HostName_ascii, VBAN_HOSTNAME_LEN);
	hostsIn[i].remoteIP = remoteIP;
	updateHostStreams(i);
	
	// if this is a ping request, reply	 ********* Add nuFrame ***********
	if(!vbh.format_nbs) 
		sendPing(remoteIP, false);
	
	return i;
}

// ping the first unmatched host that exceeds the timestamped limit
// only ping hosts with multiple active streams once
// LAST_PING_GAP == HOUSEKEEPING_EVERY * MAX_REM_HOSTS
void AudioControlEtherTransport::pingUnknownHosts()
{
	//Serial.printf("PingUnknown: ");
	for(int i = 0; i < MAX_REM_HOSTS; i++)
		if(hostsIn[i].active && hostsIn[i].hostName[0] == '*' ) 
		{
			hostsIn[i].lastPinged = millis();
			//Serial.printf("Pinging %i '%s' ", i, hostsIn[i].hostName);
			//Serial.println(hostsIn[i].remoteIP);
			sendPing(hostsIn[i].remoteIP);
			hostsIn[i].lastPinged = millis();
		}
}

void AudioControlEtherTransport::sendPing(IPAddress remoteIP, bool request)
{
  vban_header pingHdr;	// vban[4] defaults to {'V', 'B', 'A', 'N'}
	vban_ping pingBody;
	uint8_t pkt[sizeof(vban_header)+sizeof(vban_ping)];
	
	pingHdr.format_SR = 0x60;
	pingHdr.format_nbc = 0;
	pingHdr.format_nbs = (request) ? PING_REQUEST : PING_REPLY;
	pingHdr.format_bit = 0;
	pingBody.color_rgb = _colour ; // ***BGR*** 24-bit Voicemeeter BG chat color
	strcpy(pingHdr.streamname, "DummyStream");
	pingHdr.nuFrame = _pings; // distinct frame number	
	strncpy(pingBody.HostName_ascii, _VBANhostName, VBAN_HOSTNAME_LEN);
	strncpy(pingBody.UserName_utf8, _VBANuserName, VBAN_HOSTNAME_LEN);
	strncpy(pingBody.ApplicationName_ascii, _VBANappName, VBAN_HOSTNAME_LEN);
	// other required fields are defaults
	
	memcpy(&pkt, &pingHdr,sizeof(vban_header));
	memcpy(&pkt[sizeof(vban_header)], (const void *)&pingBody, sizeof(vban_ping));
	
	int pktSize = sizeof(vban_header)+sizeof(vban_ping);
	if(!(uint32_t)remoteIP)
		remoteIP = getMyBroadcastIP();

	udp.send(remoteIP, VBAN_UDP_PORT, pkt, pktSize);
	
	//Serial.printf("Sent ping [%i, len %i] = %i to ", _pings, pktSize, res);
	//Serial.println(remoteIP);
	_pings++;
}

IPAddress AudioControlEtherTransport::getMyBroadcastIP(void) 
{ 
	return _myBroadcastIP;
}


void AudioControlEtherTransport::setStreamName_O(char * sName, int stream)
{	// can only set the stream name output objects) access this via a function in output object
	strcpy(streamsOut[stream].hdr.streamname, sName);
	//strcpy(streamInfoPkts[stream].hdr.streamname, sName);
}

int AudioControlEtherTransport::getStreamFromSub(int sub)
{
	if(sub == EOQ)
		return EOQ;
	for(int i = 0; i < MAX_UDP_STREAMS; i++)
		if(streamsIn[i].active && streamsIn[i].subscription == sub)
			return i;
	return EOQ;
}

void AudioControlEtherTransport::setColour(uint32_t colour)
{
	_colour = colour;	// chat bg colour
}
/*
bool AudioControlEtherTransport::disable(void) 
{

	return true;
}
*/
void AudioControlEtherTransport::printHosts()
{
	bool found = false;

	for(int i = 0; i < MAX_REM_HOSTS; i++)
	{
		if(hostsIn[i].active)
		{
			found = true;
			Serial.printf("Host %i, '%s' ",i, etherTran.hostsIn[i].hostName);
			Serial.println (etherTran.hostsIn[i].remoteIP);
		}
	}
	if(!found)
		Serial.println("No active hosts");
}

void AudioControlEtherTransport::printHdr(vban_header *hdr)
{
#ifdef CE_DEBUG	
			Serial.printf("CET: Hdr: '%c' proto+rate 0x%02X, samples %i, chans %i fmt_bit 0x%02X, '%s' fr %i\n", (char)hdr->vban, hdr->format_SR, hdr->format_nbs +1, hdr->format_nbc +1, hdr->format_bit, hdr->streamname, hdr->nuFrame);
#endif
}

