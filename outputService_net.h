/* Multi-channel Network TEXT output for Teensy Audio Library 
 * does NOT take update_responsibility
 * Richard Palmer - 2024
 * Released under GNU Affero General Public License v3.0 or later
 * SPDX-License-Identifier: AGPL-3.0-or-later
 */

#ifndef output_service_Net_h_
#define output_service_Net_h_

#include "Arduino.h"
#include "Audio.h"
#include "audio_net.h"
#include "control_ethernet.h"

#define OS_DEBUG

/*
 * This file  handles logical VBAN text output and control functions
 * There is no subscription process - packets are simply broadcast or sent to a specific IP address 
 */

#define DEFAULT_CHANNELS 2

class AudioOutputServiceNet 
{
public:
	AudioOutputServiceNet() { ;	}
	
	friend class AudioControlEthernet; // may not be required
	friend class AudioControlEtherTransport;
	
	void begin(void);
	bool send(uint8_t *data, int length, char *streamName, uint8_t sType, IPAddress remoteIP = IPAddress((uint32_t)(0))); // bool extend?
	int subscribe(char *sName, uint8_t sType, IPAddress remoteIP = IPAddress((uint32_t)0)); // default to broadcast IP


	int missedTransmit(bool reset = true);	// get (and reset) the number of missed transmit buffer on update

protected:
	std::queue <queuePkt> _myQueueO;
	int _myStreamO = EOQ; // valid streamID is 0..255

private:
	bool outputBegun = false;
	char _myStreamName[VBAN_STREAM_NAME_LENGTH] = "*";
	uint32_t didNotTransmit = 0;
	uint32_t _nextFrame = 0; 
	uint8_t _outChans;

	// debug 
	bool printMe;
	void printHdr(vban_header *hdr);
	void printContent(uint8_t *buff, int len);
};
#endif
