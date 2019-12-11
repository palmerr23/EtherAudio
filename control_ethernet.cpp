/*
 * Ethernet (UDP) Network Control object for Teensy Audio Library
 * This file handles low level communications management, interface setup and all Datagram traffic
 * Currently handles all Network queue management
 * 
 * Copyright 2019, Richard Palmer
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
 

#include <Arduino.h>
#include "control_ethernet.h"
#include "audio_net.h"

#define CE_SERIAL_DEBUG 1 	// General serial monitor debug info. Set to 0 for production
#define CE_REPORTEVERY 2000	// summary report every X updates

#define CE_SYNC_DEBUG	1	// Send square wave from update() to test clock differences between hosts. Set to 0 for production
#define CE_SYNC_PIN 2		// pin for sync pulse - actually appears on Pin 3
#if CE_SYNC_DEBUG > 0
bool syncPulse_CE = false;
#endif
//#define RESET_WIZ  // if Wiz does not start reliably

//extern W5100Class W5100;
extern short bloxx = 0; // debug block counter (global)

// default values - may be reset by code
IPAddress defaultNetMask_CE(255, 255, 255, 0); // Class C is assumed by Ethernet.h
IPAddress defaultIP_CE(192, 168, 1, 100); // set IP/MAC to something unlikely to be in use - will be changed by code.
uint8_t defaultMAC_CE[6] = { 0xDE, 0xAD, 0xBE, 0xEF, 0xFE, 100};

bool AudioControlEtherNet::enable()
{ 
	uint8_t udpval;	 
	short wiztype, i;
	// set up the Ethernet for UDP comms 
	// reset the WIZ - may be unnecessary

#ifdef RESET_WIZ	// optional Wiz reset
	pinMode(ResetWIZ_PIN, OUTPUT);
    digitalWrite(ResetWIZ_PIN,LOW);
    delay(1); // pulse > 50nS
    digitalWrite(ResetWIZ_PIN,HIGH);
    delay(10); // tReset is < 500uS	 
#endif
	Ethernet.init(WIZ_CS_PIN);  // Most Arduino shields use 10	 for SPC_CS
	Ethernet.begin(defaultMAC_CE, defaultIP_CE); // ugly but effective! Change them from the running code using setXXX()
	memcpy(_myMAC, defaultMAC_CE, 6);
	_myIP = defaultIP_CE;
	Ethernet.setSubnetMask(defaultNetMask_CE);
			
	wiztype = Ethernet.hardwareStatus();

	udpval = Udp.begin(_controlPort);	// as above	 
	
	active = true;   // set the AudioStream object active by force to start update() - as it isn't going to connect to anything else

#if CE_SERIAL_DEBUG > 0
    if (wiztype == EthernetNoHardware) 
	{
		Serial.println("**** Ethernet shield was not found. Sorry, can't run without hardware. :( ****");    
	} else 
	{
		Serial.print("WIZ chip: ");
		Serial.println(wiztype);  
	}
	if (Ethernet.linkStatus() == LinkOFF) 
	{
		Serial.println("Ethernet cable is not connected....yet");
    }

   Serial.print("UDP "); Serial.println(udpval ? "OK" : "Bad");	

   Serial.print("Ether Audio stream: "); Serial.println(isActive() ? "Active" : "Inactive");
#endif
	// no periodic interrupt, so can't take update_responsibility	
	
	// initialize the audio and control queues	
	audioQ_I = audioQ_O = -1;
	audioQ_Free = 0; // first block 
	for (i = 0; i < MAX_AUDIO_QUEUE; i++)
	{
		Qblk_A[i].bufPtr[1] = NULL;
		Qblk_A[i].bufPtr[0]  = NULL;
		Qblk_A[i].nextBlock  = i + 1;
	}
	Qblk_A[MAX_AUDIO_QUEUE - 1].nextBlock  = -1;

	controlQ_I = controlQ_O = -1;
	controlQ_Free = 0;
	for (i = 0; i < MAX_CONTROL_QUEUE; i++)
		Qblk_C[i].nextBlock  = i + 1;

	Qblk_C[MAX_CONTROL_QUEUE - 1].nextBlock  = -1;
	
	// initialize input and output streams, and object counts
	activeTCPstreams_I = activeTCPstreams_O = activeObjects_C = 0;
	for (i = 0; i < MAX_TCP_STREAMS_IN; i++)
	{
		streamsIn[i].hostStreamID = -1;
		streamsIn[i].subscribers = 0;
	}
	for (i = 0; i < MAX_TCP_STREAMS_OUT; i++)
	{
		streamsOut[i].hostStreamID = -1;
		// spkts[] doesn't need initializing, as slaved to streamsOut 
	}	
	for (i = 0; i < MAXCHANNELS; i++) // local audio block pointers 
		myBlock[i] = NULL;
	
	memset(&zeroBlockA, 0, SAMPLES_2); // create an audio block of zeros to send when no packets are available.
	
	
#if CE_SERIAL_DEBUG > 0
	Serial.print("AQ Free "); Serial.println(countQ_A(&audioQ_Free));
	Serial.print("CQ Free "); Serial.println(countQ_C(&controlQ_Free));
	Serial.print("Audio memory ");Serial.println(AudioMemoryUsage());
    Serial.println("control_ethernet.init() complete"); 
#endif

   myLinkOn = (Ethernet.linkStatus() == LinkON); // Don't call linkStatus() directly from user space, as it initiates an SPI transaction IMMEDIATELY. Use getLinkStatus() instead.
   ethernetEnabled = true;
   
#if CE_SYNC_DEBUG > 0
		pinMode(CE_SYNC_PIN, OUTPUT);
		digitalWrite(CE_SYNC_PIN, LOW);
#endif
   return (wiztype != EthernetNoHardware);
}

void AudioControlEtherNet::begin(void)
{
	// all the work is done in enable()
}

/**************** UPDATE ***********************
 * clear out expired audio and control blocks
 * make streams that have received no recent blocks inactive
 * receive blocks - queue all control blocks, and only audio blocks for subscribed streams, discard others
 * transmit blocks in output queue - audio, then control blocks
*/
void AudioControlEtherNet::update(void)
{ 
uint8_t  thisHost, thisHostStream; 
short pktsCycle;
short i,  packetSize = 0;
#if CE_SYNC_DEBUG > 0	
	digitalWrite(CE_SYNC_PIN, (syncPulse_CE ? HIGH : LOW));
	syncPulse_CE = !syncPulse_CE;
#endif
	if (!ethernetEnabled) // no updates until enable() is complete.
			return;
#if CE_SERIAL_DEBUG > 5
	if((ce_reportCntr % CE_REPORTEVERY) == 0) 
			Serial.print("\n<E ");
#endif
#if CE_SERIAL_DEBUG > 15
	Serial.print("<E ");
	Serial.print(AudioMemoryUsage());
#endif
	// don't send anything to the ethernet hardware until ethernet is up and cable is connected
	// still need to dump output blocks and provide NULL data pointers for and registered input streams if we get disconnected temporarily
	myLinkOn = (Ethernet.linkStatus() == LinkON); // Generates SPI transaction
	ethernetUp_C = (ethernetEnabled  && myLinkOn); 
	if (ethernetUp_C && !ethernetUp_C_last) // re-start Udp after cable disconnection or Ethernet intialize
	{
#if CE_SERIAL_DEBUG > 1
		Serial.println("****** UDP restart ****** ");
		if (ethernetEnabled)
			Serial.println(myLinkOn);
#endif
		Udp.begin(_controlPort); 		//Udp.stop() is not needed - Udp.begin closes the socket before reopening
		myLinkOn = (Ethernet.linkStatus() == LinkON);
		ethernetUp_C_last = ethernetUp_C = (ethernetEnabled && myLinkOn);
	}
	ethernetUp_C_last = ethernetUp_C;  // remember last Ethernet status
	
	/*********** CLEAN UP Qs ***************/
	// age and clear out any expired blocks (audio/control)
	// unless there's a flood of received audio blocks, these will mostly be unread control blocks from other hosts - i.e. not subscribed.

#if CE_SERIAL_DEBUG > 15
	Serial.print(" AI ");
	Serial.print(cleanAudioQueue(&audioQ_I) ? "" : " CAQ bad ");// audio queue aging and cleanout 
	Serial.print(countQ_A(&audioQ_I));
	Serial.print(" CI ");
	Serial.print(cleanControlQueue(&controlQ_I) ? "" : " CCC bad ");// control queue aging and cleanout 
	Serial.print(countQ_C(&controlQ_Free));
	Serial.println(" QX ");
#else
	cleanAudioQueue(&audioQ_I); 		// audio input queue aging and cleanout 
	cleanControlQueue(&controlQ_I) ; 	// control input queue aging and cleanout 
#endif
	
	for (i = 0; i < activeTCPstreams_I; i++) // age input streams 
		if(streamsIn[i].active > 0)
				streamsIn[i].active--;

/***********  ETHERNET PACKET INPUT  ***************/
	// read any incoming audio or control packets, putting them into queues for processing by clients.
	pktsCycle = 0;
	while ((packetSize = Udp.parsePacket()))	// read UDP header, > 0 if anything new waiting. parsePacket cleans up any left overs from previous packets. 
	{
		pktsCycle++;

#if CE_SERIAL_DEBUG > 15
	// if(packetSize) {Serial.print("P "); Serial.print(packetSize);}
#endif		
		if  (Udp.available()) //anything waiting in a buffer? returns packetSize
		{		
			Udp.read((char *)&UDPBuffer, sizeof(UDPBuffer)); // UDPBuffer is big enough for an audio packet(largest packet type)
			thisHost = hostIPtoHostID(Udp.remoteIP());
			thisHostStream = UDPBuffer.netStreamID; // assume it's an audio packet, otherwise this is trash
			addPacketToQueues(&UDPBuffer, thisHost, thisHostStream);
		} 
	}	// while
	//if(pktsCycle < 2)
		//Serial.print(" PI2- ");
//if (countQ_A_S(&audioQ_I, 1) > 1)
	//Serial.print(" QI2+ ");
	//*********** OUTPUT ALL QUEUED AUDIO PKTs **************
	sendAudioPkts(); //&audioQ_O, activeTCPstreams_O

	// ************* OUTPUT ALL QUEUED CONTROL PACKETS  ******************
	sendCtrlPackets(&controlQ_O);		

#if CE_SERIAL_DEBUG > 5
	if((ce_reportCntr % CE_REPORTEVERY) == 0) 
	{
		Serial.print("A Mem "); Serial.print(AudioMemoryUsage());
		Serial.print(ethernetUp_C ? " E-up, ASent " : " E-down, ASent ");		
		Serial.print(aSent);
		Serial.print(", CSent ");Serial.print(cSent);
		Serial.print(", CRecd "); Serial.print(cRecd);
		Serial.print(", ARecd "); Serial.print(aRecd);
		Serial.print(", B "); Serial.print(bloxx);
	#if CE_SERIAL_DEBUG > 10
		Serial.print(" AQF "); Serial.print(countQ_A(&audioQ_Free));
		Serial.print(" O "); Serial.print(countQ_A(&audioQ_O));
		Serial.print(" I "); Serial.print(countQ_A(&audioQ_I));
		Serial.print(" CQF "); Serial.print(countQ_C(&controlQ_Free));
		Serial.print(" O "); Serial.print(countQ_C(&controlQ_O));
		Serial.print(" I "); Serial.print(countQ_C(&controlQ_I));
	#endif
		Serial.println(">");
		ce_reportCntr = 1;
	}
	ce_reportCntr++ ;
#endif	
} // update

/****************  Stream functions ******************/
bool AudioControlEtherNet::addPacketToQueues(audio2Datagram * buffer, short thisHost, short thisHostStream) // PHY independent
{
	short i, thisLocalStreamID, mySubscribers;
	uint8_t thisPktType;

	thisPktType = buffer->pktType;	// ALL datagrams have pktType as the first payload byte
	if (thisPktType == 0)  // it's a dud if 0, maybe other error checking e.g. runt packets?
	{
		// ignore block
	} else 
		if (thisPktType < NET_STREAM_CONTROL_BASE) // audio pkt 
		{
			aRecd++;

			// For now, only pktType == 2 Audio Datagrams supported
			// fill a header and move data into two buffers if the stream is subscribed, else ignore it
			// an input object will transmit the packets later
			mySubscribers = subscribed(thisHost, thisHostStream); // currently always returns 1
			if (mySubscribers) // if not subscribed stream, dump packet
			{
				thisLocalStreamID = getLocalStreamID(thisHost, thisHostStream);
				
				i = getLinkNewQblk_A(&audioQ_I); // get a free audio Q block, link it to tail of audioQ_I
				
				if (i >= 0) // got a free block
				{
					// create header and transfer data to two new allocated buffers
					myBlock[0] = allocate();
					myBlock[1] = allocate();
					bloxx +=2;
					memcpy(&(myBlock[0]->data[0]), &(buffer->content[0]), SAMPLES_2); // sequential blocks only, length in bytes
					memcpy(&(myBlock[1]->data[0]), &(buffer->content[AUDIO_BLOCK_SAMPLES]), SAMPLES_2); // second half of buffer (offset in samples)
					Qblk_A[i].bufPtr[0] = myBlock[0];
					Qblk_A[i].bufPtr[1] = myBlock[1];						
					Qblk_A[i].pktAge = PKT_DECAY_A;
					Qblk_A[i].pktType = buffer->pktType;
					Qblk_A[i].sequence = buffer->sequence;					
					Qblk_A[i].localStreamID = thisLocalStreamID;
					Qblk_A[i].subscribed = mySubscribers;
					streamsIn[thisLocalStreamID].active = STREAM_DECAY;	// reset stream active
				} else 
				{	
#if CE_SERIAL_DEBUG > 5
					Serial.print("NFB "); Serial.print(countQ_A(&audioQ_Free)); 
					Serial.print("/");Serial.print(countQ_A(&audioQ_I));
					Serial.print("/"); Serial.print(countQ_A(&audioQ_O));
#endif
					// bugger! out of free slots - drop the packet					
				}
			} // subscribed
			else // Unsubscribed: Update the activity indicators if this is a known stream, as someone might subscribe soon
			{
#if CE_SERIAL_DEBUG > 15
				Serial.print("UNS ");
#endif
				if(thisLocalStreamID >= 0)
				{						
					streamsIn[thisLocalStreamID].active = STREAM_DECAY; // reset count down if received AUDIO or STREAM UPDATE pkt
				}					
			}
		}  // audio packet
		else // *************** control packet *********************
		{
			cRecd++;
			memcpy(&Cbuffer, buffer, sizeof(Cbuffer)); // Control buffers are smaller than audio (easier than a cast or union!)

			if (thisPktType == NET_AUDIO_STREAM_INFO) // update stream info table
			{
				memcpy(&streamInfobuffer, &(Cbuffer.payload[0]), sizeof (streamInfoPkt)); // get the contents

				// if not already registered, else just update the activity counter
				thisLocalStreamID = getLocalStreamID(hostIPtoHostID(Udp.remoteIP()), streamInfobuffer.streamID);
				if (thisLocalStreamID == -1)
					registerNewStream(hostIPtoHostID(Udp.remoteIP()), streamInfobuffer);
				else
					updateStreamInfo(thisLocalStreamID, streamInfobuffer);						
			} else // another type of control packet
			{
		
				i = getLinkNewQblk_C(&controlQ_I); // keep all general control packets				
				if (i >= 0)
				{					
					Qblk_C[i].pktAge = (thisPktType < NET_USER_CONTROL_BASE)? 1 : PKT_DECAY_C;	// aged out in a single cycle for control blocks for audio objects, else when read by USER program.
					Qblk_C[i].pktType = thisPktType;
					Qblk_C[i].payloadSize = Cbuffer.payloadSize; 
					Qblk_C[i].remoteHostID = hostIPtoHostID(Udp.remoteIP());
					Qblk_C[i].subscribed = 1;	// ignored for control blocks at this stage 
					memcpy(&(Qblk_C[i].payload), &(Cbuffer.payload), Cbuffer.payloadSize); //
				} else 
				{	
#if CE_SERIAL_DEBUG > 5
					Serial.print("NCQ ");
#endif
					// bugger! out of free slots - drop the packet
					// return false; // not sure we can do this without some clean up
				}
			}
		} // stream info control packet
			
	return true;
}
void AudioControlEtherNet::sendAudioPkts() // Ethernet/UDP specific volatile short * queue, short actStr
{
	short itemsInQ, strm, i,  targetID, sendMe;
	short sentPkts = 0;
	bool pktOK = true;
	sendMe = audioQ_O;
	//itemsInQ = 1;
	while (sendMe >= 0)		// no need to watch audio packet sequence - they are FIFO queued 
	{	
		//assemble the audioBuffer and transmit packet				
		if (Qblk_A[sendMe].bufPtr[0] != NULL) // if left is OK, assume right is also
		{
			memcpy(&audioBuffer.content[0], 				  &Qblk_A[sendMe].bufPtr[0]->data[0], SAMPLES_2); //  length in bytes
			memcpy(&audioBuffer.content[AUDIO_BLOCK_SAMPLES], &Qblk_A[sendMe].bufPtr[1]->data[0], SAMPLES_2); // offset in samples

			release(Qblk_A[sendMe].bufPtr[0]); // buffers were allocated in an output object
			release(Qblk_A[sendMe].bufPtr[1]);
			Qblk_A[sendMe].bufPtr[0] = NULL; // clear them out, so they aren't released again
			Qblk_A[sendMe].bufPtr[1] = NULL;
			bloxx -= 2;
		} else  // shouldn't occur, but just in case...
		{
			memset(&audioBuffer.content[0], 		0, SAMPLES_2);
			memset(&audioBuffer.content[SAMPLES_2], 0, SAMPLES_2); 
		}		
				
		audioBuffer.pktType = NET_AUDIO_DATA_PKT;
		audioBuffer.sequence = Qblk_A[sendMe].sequence;
		targetID = Qblk_A[sendMe].sourcehostID;
		audioBuffer.netStreamID = Qblk_A[sendMe].localStreamID;
		
		freeQblk_A(&audioQ_O, sendMe); // copied to send buffer, so can free the queue block

// send the packet		
		// send the packet - if control is initialized	
		// *********************** need to abstract this so that other PHY layers can use it  ****************
		if (ethernetUp_C)  // just throw away the data if link is not up.
		{
			pktOK = sendUDPPacket(hostIDtoHostIP(targetID), _controlPort, (uint8_t *) &audioBuffer, sizeof(audio2Datagram));		
aSent++;	
sentPkts++;				
		}	

		sendMe = audioQ_O; // another block?
	} // items in audio output queue
#if CE_SERIAL_DEBUG > 5	
if (!sentPkts && (activeTCPstreams_O > 0))
		Serial.print("NPO ");
if(!pktOK)
	Serial.print("DPSA ");	
#endif
}

void AudioControlEtherNet::sendCtrlPackets(volatile short * queue) 
{
	short payloadSiz, i, targetID;
	bool pktOK  = true;
	
	if (controlQ_O == -1) // nothing to send
		return; 
	
	while ((i = controlQ_O)!= -1)	// freeQblk() will reset the pointer to the next block
	{
		// re-use the control buffer	
		controlBuffer.pktType = Qblk_C[i].pktType;
		payloadSiz = controlBuffer.payloadSize = Qblk_C[i].payloadSize;
		memcpy(&(controlBuffer.payload[0]), &(Qblk_C[i].payload[0]), payloadSiz);
		targetID = Qblk_C[i].remoteHostID;
		if (ethernetUp_C)  // just throw away the data if link is not up.
		{
			// Send control packet
			pktOK = sendUDPPacket(hostIDtoHostIP(targetID), _controlPort, (uint8_t *) &controlBuffer, payloadSiz + TCPDG_HDR_C);
			cSent++;
#if CE_SERIAL_DEBUG > 25	
	Serial.print(" SEND_C "); Serial.print(payloadSiz + TCPDG_HDR_C);
	if(!pktOK)
		Serial.print(" DPSC ");
#endif
		}
		// release 
		freeQblk_C(&controlQ_O, i);
	} // items in control output queue
}

bool AudioControlEtherNet::cleanAudioQueue(volatile short * thisQ)
{ 
short i, tq, guard = 0;
// clean up audio queues - aged blocks, and ones where all the subscribers have read them. Release any allocated blocks
	if (*thisQ == -1) // empty queue
		return true;
		
	i = *thisQ;
	short count = 0;
	do  // clean up AUDIO Input (or Output queue - only after UDP restart)
	{ count++;
		if (Qblk_A[i].pktAge > 0) // age live audio blocks
			Qblk_A[i].pktAge--;

		if ((Qblk_A[i].pktAge <= 0) || (Qblk_A[i].subscribed <= 0)) // release blocks if aged-out or all subscribers have read it. 
		{			
			tq = Qblk_A[i].nextBlock;

			freeQblk_A(thisQ, i);	
			i = tq;
		}	
		else i = Qblk_A[i].nextBlock;
	
	} while ((i != -1) && (++guard < MAX_AUDIO_QUEUE));

	return (++guard >= MAX_AUDIO_QUEUE)? false : true; // good/bad queue integrity test
}

bool AudioControlEtherNet::cleanControlQueue(volatile short * thisQ)
{ 
short i, tq, guard = 0;
// 	different rules for control pkts for Object management (32-95) 
//  - objects subscribe to these and they are deleted when all subscribers have read them (i.e. each update cycle) or aged out BEFORE THE NEXT update() cycle
// and user types (129-255)
// these are read once by a user program and discarded (as well as aged out if not read in time) 

	i = *thisQ;
	if (i == -1)	
		return true;
	
do  // clean up CONTROL Input (or Output queue - only after UDP restart)
	{
		if (Qblk_C[i].pktAge > 0) // age live audio blocks
			Qblk_C[i].pktAge--;

		if ((Qblk_C[i].pktAge <= 0) || (Qblk_C[i].subscribed == 0)) // release blocks if aged-out or all subscribers have read it. 
		{		
			tq = Qblk_C[i].nextBlock;
			freeQblk_C(thisQ, i);	
			i = tq;
		}		
	} while ((i != -1) && (++guard < MAX_CONTROL_QUEUE));
	return (++guard >= MAX_CONTROL_QUEUE)? false : true; // good/bad queue integrity test
}

// find a free audio block, link it into the back of the queue and return its block index
short AudioControlEtherNet::getLinkNewQblk_A(volatile short * queue)
{
	short  tempindex, lastindex = -1, freeblk; //index,
	if (audioQ_Free == -1)
		return -1;			// NO FREE BLOCKS
	
	freeblk = audioQ_Free; // take from front of q
	audioQ_Free = Qblk_A[freeblk].nextBlock; // relink the free list
	
	Qblk_A[freeblk].nextBlock = -1; // new item will be at the end of the Q
	
	if (*queue == -1) // nothing in queue? new element is entire Q
	{		
		*queue = freeblk;
		return freeblk;
	}
	
	tempindex = *queue;
	while(tempindex != -1) // find end of queue
	{
		lastindex = tempindex;
		tempindex = Qblk_A[tempindex].nextBlock;
	}
	Qblk_A[lastindex].nextBlock = freeblk; // link in new block at end
	return freeblk;	
}

short AudioControlEtherNet::getLinkNewQblk_C(volatile short * queue)
{
	short  tempindex, lastindex = -1, freeblk; //index,
	if (controlQ_Free == -1)
		return -1;			// NO FREE BLOCKS
	
	freeblk = controlQ_Free; // take from front of q
	controlQ_Free = Qblk_C[freeblk].nextBlock; // remove block from free list
	
	Qblk_C[freeblk].nextBlock = -1; // new item will be at the end of the Q
	if (*queue == -1) // nothing in queue? new element is entire Q
	{		
		*queue = freeblk;
		return freeblk;
	}
	tempindex = *queue;
	while(tempindex != -1) // find end of queue
	{
		lastindex = tempindex;
		tempindex = Qblk_C[tempindex].nextBlock;
	}
	Qblk_C[lastindex].nextBlock = freeblk;
	return freeblk;	
}

// free a block in the incoming or outgoing Audio queues
void AudioControlEtherNet::freeQblk_A(volatile short * queue, short blockNum)
{
	short tempindex, freeFirst; //lastindex,
	if ((*queue < 0) || (blockNum < 0)  || (blockNum >= MAX_AUDIO_QUEUE))
		return; 		// dud index, or empty queue so ignore

	if (*queue == blockNum) // first block in queue
	{
		*queue = Qblk_A[*queue].nextBlock;  // link around it
	} else
	{
		tempindex = *queue; //lastindex =
		while(tempindex != -1 && Qblk_A[tempindex].nextBlock != blockNum) // find block before blockNum queue (or end)
		{
			//lastindex = tempindex;
			tempindex = Qblk_A[tempindex].nextBlock;
		}
		if (Qblk_A[tempindex].nextBlock == blockNum)// didn't reach end of queue, link around blockNum 
			Qblk_A[tempindex].nextBlock = Qblk_A[blockNum].nextBlock;
				
	}
	freeFirst = audioQ_Free; // link block back into front of Free Queue
	audioQ_Free = blockNum;
	Qblk_A[blockNum].nextBlock = freeFirst;		
	
	if (Qblk_A[blockNum].bufPtr[0] != NULL) // release any allocated blocks
	{
		release(Qblk_A[blockNum].bufPtr[0]);					
		bloxx--;
	}
	if (Qblk_A[blockNum].bufPtr[1] != NULL) 
	{
		release(Qblk_A[blockNum].bufPtr[1]);					
		bloxx--;
	}
	Qblk_A[blockNum].bufPtr[0] = NULL; 
	Qblk_A[blockNum].bufPtr[1] = NULL;
}

// free a block in the incoming or outgoing Control queues
void AudioControlEtherNet::freeQblk_C(volatile short * queue, short blockNum)
{
	short tempindex,  freeFirst; //lastindex,
	if ((*queue < 0) || (blockNum < 0)  || (blockNum >= MAX_CONTROL_QUEUE))
		return; 		// dud index, or empty queue so ignore	
	
	if (*queue == blockNum) // first block in queue
	{
		*queue = Qblk_C[*queue].nextBlock;  // link around it
	} else
	{
		tempindex = *queue; //lastindex = 
		while(tempindex != -1 && Qblk_C[tempindex].nextBlock != blockNum) // find block before blockNum queue (or end)		
			tempindex = Qblk_C[tempindex].nextBlock;
		
		if (Qblk_C[tempindex].nextBlock == blockNum)// didn't reach end of queue, link around blockNum 
			Qblk_C[tempindex].nextBlock = Qblk_C[blockNum].nextBlock;				
	}
	freeFirst = controlQ_Free; // link block back into front of Free Queue
	controlQ_Free = blockNum;
	Qblk_C[blockNum].nextBlock = freeFirst;		
}

short AudioControlEtherNet::countQ_A(short *q)
{
short qp, counter = 0;
	qp = *q;
	while( (qp != -1) && (counter <= MAX_AUDIO_QUEUE))
	{
		qp = Qblk_A[qp].nextBlock;
		counter++;
	}
	return (qp == -1 )? counter : -1;
}
// count Queue for a specific local stream --- may only work for the input queue
short AudioControlEtherNet::countQ_A_S(short *q, short stream)
{
short qp, counter = 0;
	qp = *q;
	while( (qp != -1) && (counter <= MAX_AUDIO_QUEUE))
	{
		qp = Qblk_A[qp].nextBlock;
		if( Qblk_A[qp].localStreamID == stream)
			counter++;
	}
	return (qp == -1 )? counter : -1;
}
short AudioControlEtherNet::countQ_C(short *q)
{
short qp, counter = 0;
	qp = *q;	
	while( (qp != -1) && (counter <= MAX_CONTROL_QUEUE))
	{
		qp = Qblk_C[qp].nextBlock;
		counter++;
	}	
	return (qp == -1 )? counter : -1;	
}

short AudioControlEtherNet::getNextAudioQblk_I(uint8_t stream) // get the next audio packet for this stream, mark it as read by a subscriber (input object)
{
	short qItem;	
	if(!ethernetEnabled || audioQ_I == -1) // nothing to get: Ethernet not up yet, or nothing in queue
		return -1;
		
	qItem = audioQ_I;	
	while ((Qblk_A[qItem].localStreamID != stream  || Qblk_A[qItem].subscribed <= 0) && (Qblk_A[qItem].nextBlock != -1))	// find unread block for this stream or EoQ
		qItem = Qblk_A[qItem].nextBlock;
	
	if (Qblk_A[qItem].localStreamID != stream || Qblk_A[qItem].subscribed <= 0) // reached EoQ, no match, out of subscriptions
		return -1;
		
	Qblk_A[qItem].subscribed--;
	// do not release the Qblk at this point - still in use
	return qItem;
}

void AudioControlEtherNet::streamPkt(short streamID)
{
	// send the details of the nominated stream 
	short blockID;
	blockID = getLinkNewQblk_C(&controlQ_O);
#if CE_SERIAL_DEBUG > 15
	Serial.print("CE:SP ");
#endif
	if (blockID != -1)
	{
		Qblk_C[blockID].pktType = NET_AUDIO_STREAM_INFO;
		Qblk_C[blockID].remoteHostID = streamsOut[streamID].remoteHostID; // where are we sendng this? 
		Qblk_C[blockID].payloadSize = sizeof(streamInfoPkt);		
		memcpy(&(Qblk_C[blockID].payload[0]), &(streamInfoPkts[streamID].streamID), sizeof(streamInfoPkt));		
	} // packet will be sent on next update cycle
}

short AudioControlEtherNet::registerObject(void)  // register a new object for control queue management (pktTypes 32-95)
{
		//activeObjects_C++; 
		return activeObjects_C++; //return current object ID, not count
}

bool AudioControlEtherNet::queueControlBlk(uint8_t hID, uint8_t pType, uint8_t * pkt, short pktLen)
{ 
// queue a control block for output ****** user mode so interrupts off
	short blockID;

	if (pType < NET_USER_CONTROL_BASE || pktLen < 1 || pktLen > sizeof(audio2Datagram)) // user programs not allowed to send system messages, or packets without a pktType, or bigger than an audio buffer
		return false;
	
	__disable_irq( );
		blockID = getLinkNewQblk_C(&controlQ_O);
		if (blockID < 0) // no blocks available
		{
			__enable_irq( );
			return false;
		}
		Qblk_C[blockID].pktType = pType;//129;//
		Qblk_C[blockID].remoteHostID = hID;//45;//
		Qblk_C[blockID].payloadSize = pktLen;//16;//
		memcpy(&(Qblk_C[blockID].payload[0]), pkt, pktLen);

	__enable_irq( );
	return true;
}

bool AudioControlEtherNet::getQueuedUserControlMsg(controlQueue *buf)
{
short i, next;
bool foundOne = false;
	// insert a copy of the next available user control message into buf
	// ONLY looks for user pktTypes (>= NET_USER_CONTROL_BASE)	
	// only one (user) activity reads these, so OK to free them after reading
	// nothing available: return buf with pkTtype = 0
	// user mode, so IRQs off
	__disable_irq( );
	buf->pktType = 0; // reserved pktType == nothing more in queue
	if (controlQ_I != -1)	 // something in queue?	
	{
		i =  controlQ_I;
		while((Qblk_C[i].pktType < NET_USER_CONTROL_BASE) && (Qblk_C[i].nextBlock != -1))
			i = Qblk_C[i].nextBlock;		
		
		// either last block in queue or found a user block
		if (Qblk_C[i].pktType >= NET_USER_CONTROL_BASE) 
		{
			memcpy(buf, &Qblk_C[i], sizeof(controlQueue));
			freeQblk_C(&controlQ_I, i);
			foundOne = true;
		}
	}
	__enable_irq();
	return foundOne;
}

short AudioControlEtherNet::subscribed(uint8_t host, uint8_t hostStream)
{
	short i;
	
return 1; // **************************** just assume stream subscribed by 1 for now. remove me when subscriptions properly managed!
	
	for(i = 0; i < MAX_TCP_STREAMS_IN; i++)
		if (streamsIn[i].remoteHostID == host && streamsIn[i].hostStreamID == hostStream ) 
			return streamsIn[i].subscribers; // may be zero
	return 0;
}
short AudioControlEtherNet::getLocalStreamID(short thisHost, short thisHostStream)
{
	short i;
	for(i = 0; i < activeTCPstreams_I; i++)
		if(streamsIn[i].remoteHostID == thisHost &&  streamsIn[i].hostStreamID == thisHostStream)
			return i;
	return -1;
			
}

void AudioControlEtherNet::registerNewStream(uint8_t thisHost, streamInfoPkt spkt)
{
#if CE_SERIAL_DEBUG > 5	
	Serial.print("~~~~~NewStrm from ");	Serial.println(thisHost);
#endif
	short i = activeTCPstreams_I;
	streamsIn[i].hostStreamID = spkt.streamID; 								
	streamsIn[i].remoteHostID = thisHost;
	streamsIn[i].subscribers = 0; 		
 	updateStreamInfo(i, spkt); // rest of data is the same as a stream update

	activeTCPstreams_I++;
}
void AudioControlEtherNet::updateStreamInfo(uint8_t StreamID, streamInfoPkt spkt)
{ 		// called when a stream update packet (#32 ) is received
#if CE_SERIAL_DEBUG > 5	
	Serial.print("~~StrmUpd <");	Serial.print(spkt.streamName);Serial.print("> ID "); Serial.println(StreamID);	
#endif
	strcpy(streamsIn[StreamID].streamName, spkt.streamName);
	streamsIn[StreamID].active = STREAM_DECAY;			
	streamsIn[StreamID].nChannels = spkt.nChannels; 		
	streamsIn[StreamID].nSamples = spkt.nChannels ; 
	streamsIn[StreamID].btyesPerSample = spkt.btyesPerSample; 	
	streamsIn[StreamID].sampleFreq = spkt.sampleFreq;	
}

int AudioControlEtherNet::createStream_O(uint8_t channels) // no target, assume broadcast
{	
	return createStream_O(channels, TARGET_BCAST, AUDIO_BLOCK_SAMPLES,2, 44);
}

int AudioControlEtherNet::createStream_O(uint8_t channels, uint8_t targetID)
{	
	return createStream_O(channels, targetID, AUDIO_BLOCK_SAMPLES,2, 44);
}

int AudioControlEtherNet::createStream_O(uint8_t channels, uint8_t targetID, uint8_t samples = AUDIO_BLOCK_SAMPLES, uint8_t bps = 2, uint8_t freq = 44)
{ // create a new stream structure and also create and queue a stream info packet for transmission
	if (activeTCPstreams_O >= (MAX_TCP_STREAMS_OUT - 1)) return -1; // failed to allocate
	streamsOut[activeTCPstreams_O].nChannels = streamInfoPkts[activeTCPstreams_O].nChannels = channels;
	streamsOut[activeTCPstreams_O].nSamples = streamInfoPkts[activeTCPstreams_O].nSamples = samples;
	streamsOut[activeTCPstreams_O].btyesPerSample = streamInfoPkts[activeTCPstreams_O].btyesPerSample = bps;
	streamsOut[activeTCPstreams_O].sampleFreq = streamInfoPkts[activeTCPstreams_O].sampleFreq = freq;	
	streamsOut[activeTCPstreams_O].hostStreamID = streamInfoPkts[activeTCPstreams_O].streamID = activeTCPstreams_O;
	streamsOut[activeTCPstreams_O].remoteHostID = targetID;
	// stream name set by user code via setStreamName_O()
		
	return activeTCPstreams_O++; // new one is always the biggest (for now - no destructor)
}

void AudioControlEtherNet::setStreamName_O(char * sName, short stream)
{	// can only set the stream name output objects) access this via a function in output object
	strcpy(streamsOut[stream].streamName, sName);
	strcpy(streamInfoPkts[stream].streamName, sName);
}

void AudioControlEtherNet::isr(void)
{
	// no interrupts defined
}

void AudioControlEtherNet::printIP(char text[], IPAddress buf)
{		// debug only!
#if CE_SERIAL_DEBUG > 0
  short j;
     Serial.print(text);
     for (j=0; j<4; j++)
	 {
       Serial.print(buf[j]); Serial.print(" ");
     }
     Serial.println();
#endif
}

//************* Ethernet functions *******************
int sUDPcntr = 0;
int AudioControlEtherNet::sendUDPPacket(IPAddress targetIP, uint16_t thePort, uint8_t * buffer, size_t buflen)
{
	short pktOK, bpn, bWritn;
	bpn = Udp.beginPacket(targetIP, thePort); 
	bWritn = Udp.write(buffer, buflen);	
	pktOK = Udp.endPacket();
#if CE_SERIAL_DEBUG > 15
	if(buffer[0] > 3 && ((sUDPcntr % CE_REPORTEVERY) == 0)) // buffer[0] == pktType
	{
		Serial.print("\nSendPkt "); Serial.print(bpn);
		Serial.print(", Type "); Serial.print(buffer[0],DEC); 
		Serial.print(", len "); Serial.print(buflen); 
		Serial.print(", written ");	Serial.print(bWritn); 
		Serial.print(", written ");	Serial.print(bWritn); 
		Serial.print("> ");
		printIP(", IP ",targetIP);//sUDPcntr = 1;
	}
	sUDPcntr++;
#endif
	return pktOK; // 1 for sent OK, 0 for not
}

uint8_t AudioControlEtherNet::hostIPtoHostID(IPAddress ip)
{
	return ip[3];
}	
IPAddress AudioControlEtherNet::hostIDtoHostIP(uint8_t id)
{
	IPAddress ipx;
	ipx = _myIP; // everything on my (Class C) subnet
	ipx[3] = id; // just replace Host 
	return ipx;
}

bool AudioControlEtherNet::disable(void) 
{
	Udp.stop(); 
	return true;
}

void AudioControlEtherNet::setMyID(uint8_t thisID)
{
	_myHostID = thisID;
	setMyNet(hostIDtoHostIP(_myHostID));
	_myMAC[5] = thisID;
	setMyMAC(_myMAC);
}
void AudioControlEtherNet::setMyNet(IPAddress ip) 	//sets this interface's IP	
{ 
	Ethernet.setLocalIP(ip);
	_myIP = ip;
	//_myHostID = hostIPtoHostID(ip);

}

void AudioControlEtherNet::setMyMAC(uint8_t *mac) 
{	
	memcpy(_myMAC, mac, 6);
	Ethernet.setMACAddress(mac);
}
void AudioControlEtherNet::setListenlPort(unsigned int cPort) 
{		// set the TCP port for comms
		_controlPort = cPort;
		Udp.begin(cPort); // requires a restart to take effect
}
		
unsigned int AudioControlEtherNet::getListenlPort(void) 
{ // for debug only
	return _controlPort;
} 

bool AudioControlEtherNet::volume(float n)
{	
	return true; // irrelevant for this object
}

// required for the AudioControl class

bool AudioControlEtherNet::inputLevel(float volume) // volume 0.0 to 1.0
{
	return true;
} 
 
bool AudioControlEtherNet::inputSelect(int n)
{
	return true;
}

/*bool  AudioControlEtherNet::getStreams()
{
 	if (! getMaster()) // master node: do nothing - already maintaining the list
	{
		// go get the streams from the master
		
		// copy them locally and set the counter
		// for now - there is just one default stream - see enable()
		
		for(short i = 0; i< MAX_TCP_STREAMS_IN; i++){
			TCPstreams
		}
		
	}
	return true; // false if failed to get a list
}*/
/*
void setStreamTargetID(uint8_t ID)
{	
}
*/
/*
int AudioControlEtherNet::getHardwareStatus(void) 
{ 
// disabled - causes immediate SPI call
	return Ethernet.hardwareStatus();
}
*/

/*bool AudioControlEtherNet::write(unsigned int reg, unsigned int val)
{
		//code here
	return true;
}
*/
/*
// target IP may be different for control (specific IP) and audio (may be broadcast)
void AudioControlEtherNet::setTargetIP(IPAddress ip) 
{     //set the target IP address for the following datagrams - broadcast addresses may be used
	_CTargetIP = ip;
}
*/