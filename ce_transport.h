/* 
 * AudioControlEtherTransport 
 * Underlying ethernet management object for Teensy Audio Library using the QNEthernet library  https://github.com/ssilverman/QNEthernet
 *
 * This file should not be included in user code. Include control_ethernet.h instead.
 * 
 * The network or master node need to provide a DHCP service. 
 *
 * This file handles low level communications management and all Datagram traffic
 * Queued VBAN Audio UDP packets will be handled by input_net objects and sourced by output_net objects.
 * The input and output objects handle packet to audio buffer transformations. 
 * Input objects have individual packet queues. Output objects share a common queue.
 *
 * Service packets are managed by the service_net object which can send and receive VBAN SERVICE packets. VBAN_SERVICE_IDENTIFICATION (host identification) packets are managed here, and all other SERVICE packets are queued for user program attention. 

 * 
 * 2019-2024, Richard Palmer
 * Released under GNU Affero General Public License v3.0 or later
 * SPDX-License-Identifier: AGPL-3.0-or-later
 */

#pragma once

#include "Audio.h"
#include "audio_net.h"
#include "audio_vban.h"
#include "control_ethernet.h"
#include "IPAddress.h"

#define CE_DEBUG

#define CTRL_ETHERNET_DO_LOOP_IN_YIELD // ethernet regular updating hooks into yield() and delay(), no need for explicit calls to xxx.updateNet() in mainline code

#define DISCONNECT_RETRY		1000
#define HOUSEKEEPING_EVERY	5000			// 500 in PROD. resolve new streams and hosts
#define DHCP_TIMEOUT 				15000			// 15 secs
#define QN_PKT_QUEUE				12				// QNE queue length for incoming packets

/*************** Ethernet connections, sockets and UDP datagrams **************/
class AudioControlEtherTransport 
{ 
public: 
	AudioControlEtherTransport(void) { } 

	friend class AudioControlEthernet;
	/*
	// do not need these - AudioControlEthernet bridges between these classes and this one
	friend class AudioOutputNet; // work with others sharing this control
	friend class AudioInputNet;		// may not need these - AudioControlEthernet bridges between
	friend class AudioOutputServiceNet;
  friend class AudioInputServiceNet;
*/
 // end user functions
	bool begin();	

	// The functions and variables below are not for end user access. 
	// They are kept public for access from lambda code udpdateNet()
	bool etherStart(void); // in begin() or after cable diconnection 

	//static void updateNet(void); // ersatz update() called as lambda type function attached to EventResponder
	// *** Anything called or used by lambda updateNet() needs to be public ***
	
	// Ethernet link and identity management
	bool linkIsUp(void);
	IPAddress getMyBroadcastIP(void);
	bool etherTranBegun = false;
	
private: // not accessed by updateNet() or functions called from there
	void updateIP(void);
	IPAddress _myIP = {0,0,0,0};
	IPAddress _myBroadcastIP = {0,0,0,255};
	uint8_t _myMAC[6]; // set by LWIP
	uint16_t _udpPort; 

	// registered hosts
public:
	int getHostIDfromIP(IPAddress ip); 	//return remoteHost ID from IP address 
	IPAddress getHostIPfromID(int id);
	const char *getHostNamefromID(int id);
	IPAddress getHostIPfromName(char * hostName);
	const char *getHostNameFromIP(IPAddress ip);
	
	// VBAN PING
	int processIncomingPing(IPAddress remoteIP, vban_header vbh); // process incoming PING response
	void pingUnknownHosts(); // Ping all unknkown hosts in sequence
	void sendPing(IPAddress remoteIP, bool request = true);
	int addHost(IPAddress remoteIP);
	void setColour(uint32_t colour); // ***BGR*** Voicemeeter chat: 24-bit background colour	
	uint32_t _colour = 0x0000C0; // Voicemeeter chat background 24-bit **BGR** (RED)	
private:
	uint32_t _pings = 1;	// each ping needs to have a distinct frame number
	char _VBANhostName[VBAN_HOSTNAME_LEN] = "TeensyHost";
	char _VBANuserName[VBAN_HOSTNAME_LEN] = "TeensyUser";
	char _VBANappName[VBAN_HOSTNAME_LEN] = "TeensyApp";
	char _FQDN[VBAN_HOSTNAME_LEN];

	// ********* UDP QUEUEING ************
public:
	bool initQueues();		// queue management	
	bool _initializedQ = false;

public:
// ***** Audio streams, hosts and subscriptions ***********
	pktType packetTest(vban_header hdr); // incoming packet triage
	
	hostInfo			hostsIn[MAX_REM_HOSTS];
	subscription 	subsIn[MAX_SUBSCRIPTIONS];
	streamInfo		streamsIn[MAX_UDP_STREAMS];	
	streamInfo	streamsOut[MAX_UDP_STREAMS]; 				// output streams don't need hosts or subs, just a queue
	std::queue <queuePkt> *qpOut[MAX_UDP_STREAMS]; // Set by output::subscribe(). Queues are owned by outputs.
	int VBpktsProc;
	int udpDroppedPkts;

// ********* stream and host registration and  management *********
public:
	void updateHostStreams(int hostID);
	void updateStreamSubscription(int streamID);
	void setStreamName_O(char * sName, int stream);	// private - user levelversion is in the output object
	int getRegisterStreamId(const uint8_t * pkt, IPAddress remoteIP,pktType type = PKT_AUDIO);
	void registerStreamInPkt(const uint8_t * pkt, IPAddress remoteIP, int slot, bool isNew, pktType type = PKT_AUDIO);
	int getStreamFromSub(int sub);
	void updateActiveStreams();
	void updateSubscriptions(void); // 
	void updateStreamInfo(int StreamID, streamInfo spkt);
private:	
	int activeUDPstreams_I; // counts of registered incoming and outgoing audio streams

// ********  queues ************
public:
  int queuePacket(pktType type = PKT_AUDIO); // called by lambda updateNet()
  bool addPacketToQueue(int inStream, pktType type);	
	void sendPkts(); 

#ifdef CTRL_ETHERNET_DO_LOOP_IN_YIELD
	void attachLoopToYield(AudioControlEtherTransport * me);
#endif

public:
	bool printMe; // debug for ce_transport, ce_transport_queues and control_ethernet
	void printHosts();
private:
	void printHdr(vban_header *hdr);
};