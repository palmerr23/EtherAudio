/* Network input for Teensy Audio Library 
 * Richard Palmer - 2019
 * Modified from Frank Frank Bösing's SPDIF library
 * Copyright (c) 2015, Frank Bösing, f.boesing@gmx.de,
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

#ifndef input_AUDIO_NET_h_
#define input_AUDIO_NET_h_

#include "Arduino.h"
#include "AudioStream.h"
//#include "DMAChannel.h"
//#include "SPI.h" // just to be safe
#include "Ethernet.h"
#include <EthernetUdp.h>
#include "Audio.h"
#include "audio_net.h"
#include "control_ethernet.h"

/*
 * This file just handles high-level block transfers and logical audio and control functions 
 * control_ethernet does all the Ethernet setup work. They are mutual friend classes.
 * Audio data can be broadcast from a master to a number of slaves using an IP broadcast address, 
 * or simply transferred between two consenting hosts
 * local IP, MAC address are set up in control_ethernet (as they stay the same for each host)
 * port and target IP are unique to each class - port is different for control messages, targets may differ, depending on what's being done.
 *
 * For most applications a master node will handle management tasks (including providing a DHCP service...) 
 * and possibly be the only node allowed to broadcast packets (other than ICMP, etc).
 * 
 */
 
 /* 
 * This version is for 2 channel audio
 */
  
class AudioControlEtherNet; // need this friend class

class AudioInputNet : public AudioStream
{
public:
	AudioInputNet(void) : AudioStream(0, NULL) { begin(); }
	void begin(void);	
	virtual void update(void);

	void setControl(AudioControlEtherNet * cont) ;
	
	bool subscribeStream(short stream) ; // subscribe to an audio stream	
	void releaseStream(void) ; // release the subscribed stream. All-zero buffers will be generated.
	short getMyStream(void) ; // get the ID of my subscribed stream	

	unsigned long getCurPktNo(void) { return currentPacket_I;}; 
	
	// need friend class AudioControlEtherNet; // work with the WIZNET control class
protected:
	short getNextStreamIndex(short thisStream); //get the next active streamID
	static audio_block_t *local_block_I[MAXCHANNELS];

//  IO handling is by the Ethernet library
	static void isr(void);

private:
	bool inputBegun = false;
	AudioControlEtherNet * myControl_I ;
	short _myStream_I; // unsubscribed at start
	unsigned long currentPacket_I;
	short _myObjectID ; // registered ID for control queue data.

	// internal buffers and pointers
	audio_block_t zeroBlockI; // a block of zero samples.

	// internal status and control
	//short qq;
	short it_report_cntr;
	long blocksRecd_I, missingBlocks_I;
};
#endif
