/* 
 * AudioControlEthernet 
 * Ethernet management object for Teensy Audio Library using the QNEthernet library  https://github.com/ssilverman/QNEthernet
 *
 * This file should be included and instanced for all applications using Ethernet Audio.
 *
 * This file handles user-facing ethernet management functions (hostname, etc), relying on ce_transport for handling ethernet hardware, UDP packets, subscriptions and queuing. 
 *
 * Only one object can access an input or output stream. Multiple connections to input objects may be made in the usual manner.
 *
 * 2019-2024, Richard Palmer
 * Released under GNU Affero General Public License v3.0 or later
 * SPDX-License-Identifier: AGPL-3.0-or-later
 */

#pragma once

#include "Audio.h"
#include "AudioControl.h"
#include "audio_net.h"
#include "audio_vban.h"
#include "ce_transport.h"

// debug shared with ce_transport

extern  AudioControlEtherTransport etherTran; // handles all stream and subscription traffic

/*************** Ethernet connections, sockets and UDP datagrams **************/
class AudioControlEthernet : public AudioControl//, public AudioStream
{
public: // AudioStream is input: must we transmit()?
	AudioControlEthernet(void) { }  	
	friend class AudioOutputNet; // work with others sharing this control
	friend class AudioInputNet;		// may not need these - AudioControlEthernet bridges between
	friend class AudioControlEtherTransport;

 // Returns a string containing the library version number.
  static const char *libraryVersion() { return "0.1.0-snapshot";  }
	bool begin();	
	bool enable()	{ return true; }

// ethernet and UDP	control functions 
	void setHostName(char * hostName); // all three are visible in Voicemeeter VBAN Info
	void setUserName(char * userName);
	void setAppName(char * appName);
	void setPort(uint16_t cPort); // reset the UDP listen port for comms
	void setColour(uint32_t colour); // ***BGR*** 24-bit chat bg colour

// ***** for debugging, but not required for normal usercode ****
// Ethernet
	bool linkIsUp(void); // true for active link
	int getHardwareStatus(void); 
	IPAddress getMyIP(void);
	IPAddress getBroadcastIP(void);
	void announce(void);	// broadcast this host's details (VBAN PING)


// ***** Ethernet streams, hosts and subscriptions ***********
	stream_pretty getStreamInfo(int id, int direction = STREAM_IN);  // end user call -input or output streams
	streamInfo getStream(int id) { return etherTran.streamsIn[id]; }; // stream info item - raw
	hostInfo getHost(int id); // end user call
	subscription getSubInfo(int id) { return etherTran.subsIn[id]; }
	void printHosts();
	int droppedPkts(bool reset = true);	// get and reset the number of dropped frames
	int getActiveStreams() ; // number of active strams


private:
	// audio_control.h - some skeletons are required - they do nothing here
	bool volume(float n) { return true;}
	bool disable(void) { return true;}
	bool inputLevel(float volume){ return true;}
	bool inputSelect(int n) { return true;}
};

