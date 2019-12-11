/* Network output for Teensy Audio Library 
 * Richard Palmer - 2019
 * Modified from Frank Frank Bösing's SPDIF library
 * Copyright (c) 2015, Frank Bösing, f.boesing@gmx.de,
 * Thanks to KPC & Paul Stoffregen!
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice, development funding notice, and this permission
 * notice shall be included in all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 */

#ifndef output_Net_h_
#define output_Net_h_

#include "Arduino.h"
#include "AudioStream.h"
//#include "DMAChannel.h"
//#include "SPI.h" // just to be safe
#include "Ethernet.h"
#include <EthernetUdp.h>
#include "Audio.h"
#include "audio_net.h"
#include "control_ethernet.h"
//#include "memcpy_audio.h"


/*
 * This file just handles high-level block transfers and logical audio and control functions 
 * control_WIZNET does all the Ethernet setup work. They are mutual friend classes.
 * Audio data can be broadcast from a master to a number of slaves using an IP broadcast address, 
 * or simply transferred between two consenting hosts
 * local IP, MAC address are set up in control_WIZNET (as they stay the same for each host)
 * port and target IP are unique to each class - port is different for control messages, targets may differ, depending on what's being done.
 *
 * For most applications a master node will handle management tasks (including providing a DHCP service...) 
 * and possibly be the only node allowed to broadcast packets (other than ICMP, etc).
 * 
 */
 
 /* 
 * This version is for 2 channel audio
 */
#define NCHANNELS 2
  
class AudioControlEtherNet; // need this friend class

class AudioOutputNet : public AudioStream
{
public:
	AudioOutputNet(void) : AudioStream(2, inputQueueArray) { begin(); }
	void begin(void);	
	virtual void update(void);
	void setAudioTargetID(short targetID)  ; //sets the target HostID - last digit of IP address for the datagrams
	void setControl(AudioControlEtherNet * cont) ;
	void setStreamName(char * sName) ;
	short setStreamID(short id);
	unsigned long getCurPktNo(void) ;
	
	// need friend class AudioControlEtherNet; // work with the WIZNET control class
protected:
	audio_block_t *local_block_O[MAXCHANNELS];		
	audio_block_t * local_block_out[MAXCHANNELS];
	audio_block_t *inputQueueArray[2];

	static void isr(void);
private:
	bool outputBegun = false;
	AudioControlEtherNet * myControl_O;
	streamInfoPkt myStreamInfoO;
	bool ethernetUp_O;
	short aTargetID;	// default stream HostID for output blocks (audio and streamInfo)
	
	// status and control
	//bool AudioOutputNet::update_responsibility = false; // Can't take update responsibility as no regular interrupts	
	unsigned long currentPacket_O ; 
	short _myObjectID; // registered ID for control queue data.
	short myStreamID_O; // valid streamID is 0..255
	short updateStreamMsgCntr; //

	// debug variables
	short qq;
	short debugx;
};
#endif
