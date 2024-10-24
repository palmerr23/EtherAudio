/*
 * Ethernet (UDP) Network Control object for Teensy Audio Library
 * Handles all user-facing Network queue management
 * does NOT take update_responsibility
 *
 * All audio queue manipulation must be with **interrupts off**
 *
 * Based on QNEthernet library for Teensy 4.1 Shawn Silverman
 *
 * Sept 2024 Richard Palmer
 * Released under GNU Affero General Public License v3.0 or later
 * SPDX-License-Identifier: AGPL-3.0-or-later
 */

#include <Arduino.h>
#include "control_ethernet.h"
#include <QNEthernet.h>
#include "IPAddress.h"

// debug shared with ce_transport

using namespace qindesign::network;

//using namespace AudioControlEtherTransport;

char vban_sub_protocol_name [][8]{"AUDIO", "SERIAL", "TEXT", "SERVICE"};

uint32_t VBAN_AUDIO_SRList[VBAN_AUDIO_SR_MAXNUMBER]= {6000, 12000, 24000, 48000, 96000, 192000, 384000, 8000, 16000, 32000, 64000, 128000, 256000, 512000, 11025, 22050, 44100, 88200, 176400, 352800, 705600};

char VBAN_AUDIO_dataType_name[VBAN_AUDIO_TYPE_MAXNUMBER][8] = {"BYTE8", "INT16", "INT24", "INT32", "FLOAT32", "FLOAT64", "BITS12", "BITS10"};

char VBAN_AUDIO_CODEC_name[VBAN_AUDIO_CODEC_MAXNUMBER][5] = {"PCM", "VBCA", "VBCV"};

uint32_t VBAN_BPSList[VBAN_BPS_MAXNUMBER]= {0, 110, 150, 300, 600, 1200, 2400, 4800, 9600, 14400, 19200, 31250, 38400, 57600, 115200, 128000, 230400, 250000, 256000, 460800, 921600, 1000000, 1500000, 2000000, 3000000};


// Ethernet initialization
bool AudioControlEthernet::begin()
{
#ifdef CE_DEBUG
	Serial.println("Begin ControlEthernet");
#endif
	return etherTran.begin();
} // begin

/* ******* There is no regular update() function as this is not an AudioStream object ********
 * all housekeeping is either in ce_transport::updateNet() or in function calls here
 */

int AudioControlEthernet::getHardwareStatus(void) 
{ 
// disabled - causes immediate SPI call
	return Ethernet.hardwareStatus();
}

int AudioControlEthernet::droppedPkts(bool reset)
{
	static uint32_t resetAt = 0;
	int temp;
	temp = etherTran.udpDroppedPkts;
	if(reset)
		resetAt = etherTran.udpDroppedPkts;
	return temp - resetAt;
}

// end user host info
hostInfo AudioControlEthernet::getHost(int id) // end user call
{
	if (id >= MAX_REM_HOSTS)
	{
		hostInfo st;
		memset((void*)&st, 0, sizeof(st));
		return st;
	}
	return etherTran.hostsIn[id];
}

// return a pretty structure to end user
stream_pretty AudioControlEthernet::getStreamInfo(int id, int direction)
{
	streamInfo *sp;
	stream_pretty st;
	
	if (id >= MAX_UDP_STREAMS)
	{
		memset((void*)&st, 0, sizeof(st));
		return st;
	}
	
	if(direction == STREAM_IN)
		sp = &etherTran.streamsIn[id];
	else
		sp = &etherTran.streamsOut[id];
	
	st.active = sp->active;
	st.subscription = sp->subscription;
	st.pktsInQueue = 0;//etherTran.countQ_A(&audioQ_I); // ************ FIX ME
	strncpy(st.streamName, sp->hdr.streamname, VBAN_STREAM_NAME_LENGTH-1);
	st.sampleRate = VBAN_AUDIO_SRList[sp->hdr.format_SR & VBAN_SPEEDMASK];
	st.protocol = sp->hdr.format_SR & VBAN_PROTOCOL_MASK;
	strcpy(st.dataType, VBAN_AUDIO_dataType_name[sp->hdr.format_bit & VBAN_TYPE_MASK]);
	strcpy(st.codec, VBAN_AUDIO_CODEC_name[(sp->hdr.format_bit >> VBAN_PROTO_SHIFT) & VBAN_TYPE_MASK]);
	st.pktSamples = sp->hdr.format_nbs + 1;
	st.channels= sp->hdr.format_nbc + 1;
	st.lastPktTime = sp->lastPktTime;
	st.ipAddress = sp->remoteIP;
	if(sp->hostIndx == EOQ) // not matched to a host
		strcpy(st.hostName, "*");
	else
		strncpy(st.hostName, etherTran.hostsIn[sp->hostIndx].hostName, VBAN_HOSTNAME_LEN);
	//Serial.printf("GSI dir %i proto 0x%02X:0x%02X\n",direction,st.protocol,sp->hdr.format_SR);
	/*
	Serial.printf("GSI dir %i id %i: ", direction, id);
	Serial.println(sp->remoteIP);
	Serial.println(etherTran._myBroadcastIP);
		Serial.println(etherTran._myIP);

	Serial.println(etherTran.getMyBroadcastIP());		*/
	if(sp->remoteIP == etherTran._myBroadcastIP)
		strcpy(st.hostName, "*BROADCAST*");
		
	return st;
}

int AudioControlEthernet::getActiveStreams() 
{ 	
	return etherTran.activeUDPstreams_I; 
}

IPAddress AudioControlEthernet::getBroadcastIP(void) 
{ 
	return etherTran._myBroadcastIP;
}

IPAddress AudioControlEthernet::getMyIP(void) 
{ 
	etherTran.updateIP(); // may have changed, so refresh
	return etherTran._myIP; 
}

void AudioControlEthernet::setHostName(char * hostName)
{
	strncpy(etherTran._VBANhostName, hostName, VBAN_HOSTNAME_LEN-1);
}

void AudioControlEthernet::setUserName(char * userName) // no practical use
{
	strncpy(etherTran._VBANuserName, userName, VBAN_HOSTNAME_LEN-1); // user name field is longer, but who cares!
}

bool AudioControlEthernet::linkIsUp(void)
{
	return etherTran.linkIsUp(); // true for active link
} 

void AudioControlEthernet::setAppName(char * appName) // no practical use
{
	strncpy(etherTran._VBANappName, appName, VBAN_HOSTNAME_LEN-1); // user name field is longer, but who cares!
}

void AudioControlEthernet::announce(void)
{
	etherTran.sendPing(IPAddress((uint32_t)0),false);	// broadcast a PING REPLY
}

void AudioControlEthernet::setColour(uint32_t colour)
{
	etherTran.setColour(colour);	// chat bg colour
}


void AudioControlEthernet::setPort(uint16_t cPort) 
{		// set the UDP port for comms
		etherTran._udpPort = cPort;
		//udp.begin(cPort); // requires a restart to take effect
}

void AudioControlEthernet::printHosts()
{
	etherTran.printHosts();
}
