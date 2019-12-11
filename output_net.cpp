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

 // (RP)
 // ****** This object does not accept update_responsibility - ONE device that does needs to be in your sketch (e.g. a DAC output) ****

 // All IO handling is in the Ethernet, EthernetUDP & SPI Libraries 
 // WIZNET handles registration of hosts, other control (not audio related) messaging through separate UDP port
 // This version handles 2 channels, 128 sample blocks. 
 // 8 channel version may need to split 128 sample blocks into two datagrams to avoid exceeding MTU of 1500 bytes

#include <Arduino.h>
#include "output_net.h"

#define OUT_ETH_SERIAL_DEBUG 0 // prod == 0
int ot_report_cntr = 0;
#define OT_REPORT_EVERY 2000 // ignored in production

extern short bloxx;	// control_ethernet

void AudioOutputNet::begin(void)
{
	currentPacket_O = 0; // initialize packet sequence
	//update_responsibility = update_setup(); No ISR so do not accept update_responsibility

	// make sure local audio block pointers aren't pointing
	for (short i = 0; i < MAXCHANNELS; i++)
	{		
		local_block_O[i] = NULL;
		local_block_out[i] = NULL;
	}
	// can't do much until myControl_O is known (may not be set until later) so get real values in update()
	myControl_O = NULL;
	_myObjectID = -1; 	
	myStreamID_O = -1;
	currentPacket_O = 0;
	ethernetUp_O = false;
	
	aTargetID = TARGET_BCAST;	// broadcast packets by default
	updateStreamMsgCntr = UPDATE_MSGS_EVERY; // set up streamInfo counter
	outputBegun = true;
#if OUT_ETH_SERIAL_DEBUG > 0
  Serial.println("outputNet.begin() complete");
#endif
}

/******************** update **************************/
void AudioOutputNet::update(void)
{
	// if ethernet isn't up, simply release the input blocks
	// check if we have an active stream once ethernet is up
	// place input blocks in ethernet_control audio output queue
	// process any request/status messages for my object
	// send stream info block regularly
	if (!outputBegun || myControl_O == NULL) 	// no processing until begin() has completed
		return;									// also need ethernet control defined and enabled
 
	if (!(myControl_O->ethernetEnabled)) // need sequential tests to avoid NULL pointer issues		
		return;
		
#if OUT_ETH_SERIAL_DEBUG > 5
		ot_report_cntr++;
#if OUT_ETH_SERIAL_DEBUG < 15
	if((ot_report_cntr % OT_REPORT_EVERY) == 0)
#endif
	{
		Serial.print("[O ");
		Serial.print(AudioMemoryUsage());
	}
#endif

	if(_myObjectID < 0)					// register me for control messages
		_myObjectID = myControl_O->registerObject();
	
	if (myStreamID_O < 0)		// set up stream after myControl_O is known
	{
		myStreamID_O = myControl_O->createStream_O(NCHANNELS, aTargetID); 
		// stream name set by user code later
#if OUT_ETH_SERIAL_DEBUG > 5
		Serial.print("[Stream ");Serial.print(myStreamID_O);Serial.print(" created]");
#endif
	}	

	// get blocks from both inputs 
	local_block_O[0] = receiveReadOnly(0); // input 0 = left channel
	local_block_O[1] = receiveReadOnly(1); // blocks will be released when sent or discarded
	bloxx +=2;

	// not attached to the Ethernet control, or link disconnected: just dump the data and return

	ethernetUp_O  = myControl_O->myLinkOn;

	if (!ethernetUp_O)
	{
		if(local_block_O[0] != NULL)
		{
			release(local_block_O[0]);
			local_block_O[0] = NULL;
			bloxx--;
		}
		if(local_block_O[1] != NULL)
		{
			release(local_block_O[1]);	
			local_block_O[1] = NULL;
			bloxx--;		
		}
		return;				// ************* exit processing here if not ready to transmit	
	}

//Serial.print(" *");
	// create new buffers so that we can release the ones from receiveReadOnly()
	local_block_out[0] = allocate(); 
	local_block_out[1] = allocate();
	bloxx +=2;
	
	// always send a block, even if we didn't receive one.
	if(local_block_O[0] != NULL)
	{
		memcpy(&local_block_out[0]->data[0],  &local_block_O[0]->data[0],  SAMPLES_2); // copy the input buffers
		release(local_block_O[0]);
		local_block_O[0] = NULL;
		bloxx--;
	} else 
		memset(&(local_block_out[0]->data[0]),  0, SAMPLES_2); // no input, then zeros (silence)
	
	if(local_block_O[1] != NULL)	
	{
		memcpy(&local_block_out[1]->data[0], &local_block_O[1]->data[0], SAMPLES_2);
		release(local_block_O[1]);
		local_block_O[1] = NULL;
		bloxx--;		
	} else 
		memset(&(local_block_out[1]->data[0]), 0, SAMPLES_2);

/*	*/
//Serial.print("@");	
	// assign new audio buffers to a transmit block and queue them
	
	short aQ = myControl_O->getLinkNewQblk_A(&myControl_O->audioQ_O);	
#if OUT_ETH_SERIAL_DEBUG > 15

	short temp = myControl_O->countQ_A(&myControl_O->audioQ_O);
	if(temp >= 2)	
	{	// more than 1 in queue? print first two sequence/subscribed numbers
		short t2 = myControl_O->audioQ_O;
		Serial.print("#");  Serial.print(temp);  Serial.print(" ");
		t2 = myControl_O->audioQ_O;
		Serial.print(myControl_O->Qblk_A[t2].sequence);	Serial.print("/");Serial.print(myControl_O->Qblk_A[t2].subscribed);Serial.print(" ");
		t2 = myControl_O->Qblk_A[t2].nextBlock;
		Serial.print(myControl_O->Qblk_A[t2].sequence);	Serial.print("/");Serial.print(myControl_O->Qblk_A[t2].subscribed); 
		Serial.println(" #");
	} 

#endif
	if(aQ != -1)
	{
		myControl_O->Qblk_A[aQ].sequence = currentPacket_O % BLOCK_SEQ_WINDOW;
		myControl_O->Qblk_A[aQ].pktType = NET_AUDIO_DATA_PKT;
		myControl_O->Qblk_A[aQ].pktAge = PKT_DECAY_A;
		myControl_O->Qblk_A[aQ].localStreamID = myStreamID_O;
		myControl_O->Qblk_A[aQ].sourcehostID = aTargetID;
		myControl_O->Qblk_A[aQ].subscribed = 55; // nonsense value > 0,  stops audio queue cleaner immediately deleting the blocks
		myControl_O->Qblk_A[aQ].bufPtr[0] = local_block_out[0];
		myControl_O->Qblk_A[aQ].bufPtr[1] = local_block_out[1];
		currentPacket_O++;
		// queued ready to send
	} else 
#if OUT_ETH_SERIAL_DEBUG > 1
		Serial.print(" OOB ");
#else
		;
#endif
// send a stream info packet if needed 
	if(updateStreamMsgCntr++ == UPDATE_MSGS_EVERY)
	{		
		updateStreamMsgCntr = 0;	
#if OUT_ETH_SERIAL_DEBUG > 15	
		Serial.print("\n[[STREAM ");
		Serial.print(myStreamID_O); 
		Serial.print(" UPDATE]] ");
#endif
		myControl_O->streamPkt(myStreamID_O);		
	}
	// check and process control blocks
	/*
	 *	NOTHING YET DEFINED
	 */

#if OUT_ETH_SERIAL_DEBUG > 5		

	#if OUT_ETH_SERIAL_DEBUG <15	
		if((ot_report_cntr % OT_REPORT_EVERY) == 0)
	#endif
	{
		Serial.print(currentPacket_O);
		Serial.print("]");
	}	

#endif
} 

void AudioOutputNet::setStreamName(char * sName)
{
	// myControl_O and myStreamID_O can take several update() cycles to be established after start. 
	if(myControl_O == NULL || myStreamID_O < 0)
		delay(10); // less prone to trouble than while(myControl_O == NULL);
	myControl_O->setStreamName_O(sName, myStreamID_O);
}

// set a specific trasnmitted ID for this stream, to allow repeatable remote identification
short AudioOutputNet::setStreamID(short id)
{
	if (myStreamID_O <= 0 || id < 0)
		return -1;
	myControl_O->streamsOut[myStreamID_O].hostStreamID = id;
	return id;
}
	
unsigned long AudioOutputNet::getCurPktNo(void) 
{ 
	return currentPacket_O;
} 

void AudioOutputNet::setControl(AudioControlEtherNet * cont) 
{
	myControl_O = cont;
} 
void AudioOutputNet::setAudioTargetID(short targetID) // set target IP
{	
	aTargetID = targetID;
	if (myStreamID_O >= 0)
		myControl_O->streamsOut[myStreamID_O].remoteHostID = targetID;
	
}  
void AudioOutputNet::isr(void)
{
	// do nothing - hardware control is by Ethernet
}
