// Exchange structured data for Teensy Ethernet Audio Library
// Requires two Teensy 4.1s with Ethernet adaptors
// BUG: The first few  messages from a remote host are eaten as the host is identified and the incoming stream registered. 

#define TWO_TEENSYS
#define VERBOSE
#include <Audio.h>

#include "control_ethernet.h"
#include "inputService_net.h"
#include "outputService_net.h"

AudioControlEthernet      ether1;
AudioInputServiceNet      inStruct;
AudioOutputServiceNet     outStruct;
// patchCords are not required for non_Audio traffic

#define MY_SERVICE_ID 40 // there are exclusions
#include "utils.h"
char myHost[16] = "Teensy";
char dataStream[] =  "myDataStream";
struct myStruct
{
  char someText[8] = "ABCDEF";
  int   aNumber;
};

void setup() 
{
  Serial.begin(115200);
  while (!Serial && millis() < 5000) 
  {
    delay(10);
  }

  Serial.println("\n\nStarting BroadcastChat example");

#ifdef TWO_TEENSYS // randomise the hostname
  randomSeed(millis()); // otherwise we'll get the same initial number every time
  int hostNum = random(1, 999);
  char buf[4];
  itoa(hostNum, buf, 10);
  strcat(myHost, buf);
#endif

  ether1.setHostName(myHost);
  ether1.begin();
  if(!ether1.linkIsUp())
    Serial.printf("Ethernet is disconnected");
  else
    Serial.println(ether1.getMyIP());

  inStruct.begin();
  Serial.printf("Service In subscription %i\n",inStruct.subscribe(dataStream, MY_SERVICE_ID)); //receive from anyone 

  outStruct.begin();
  outStruct.subscribe(dataStream, MY_SERVICE_ID); // broadcast
  
  Serial.println("Done setup");
}

long count = 0;
long timer1;

void loop() 
{
  incomingPacket();  

  if(millis() - timer1 > 5000)
  {
    sendPacket();
  #ifdef VERBOSE

    Serial.printf("---------- Main: %i\n", millis()/1000);
    Serial.printf("LinkIsUp %i, IP ", ether1.linkIsUp());     
    Serial.println(ether1.getMyIP()); // test DHCP lease on cable disconnect / reconnect. VBAN will need to be restarted on Voicemeeter if IP changes.
    printActiveStreams(STREAM_IN);
    printActiveStreams(STREAM_OUT);
    ether1.printHosts();
    printActiveSubs();
    Serial.println("-----------");
  #endif
    timer1 = millis();
  }

  // regular Ethernet processing is tied to yield() and delay() 
  delay(2000); // code delays do not disturb processing
}

void sendPacket()
{
  myStruct buf;
  strcpy(buf.someText, "AACDEF");
  buf.someText[1] += random(0, 25); // randomly change second letter
  buf.aNumber = random(200,500);
  //Serial.printf("sending buf ['%c' '%s', %i], len %i, buf @ %X\n", buf.someText[0], buf.someText, buf.aNumber, sizeof(buf), &buf);
  
  outStruct.send((uint8_t*)&buf, sizeof(buf), dataStream, MY_SERVICE_ID); // broadcast, so no final argument
  Serial.printf("<<<< Sent pkt ['%s', %i], %i bytes (excl header)\n", buf.someText, buf.aNumber, sizeof(buf));
}

int incomingPacket()
{ 
  myStruct buf;
  uint8_t sType;
  int pkts = inStruct.getPktsInQueue();
  if(pkts == 0)
    return 0;
  Serial.printf(">>>> Incoming: %i packets waiting. ", pkts);
  queuePkt qpkt = inStruct.getPkt();
  sType = qpkt.hdr.format_nbc; 
  switch(sType)
  {
    case MY_SERVICE_ID:
      memcpy((void*)&buf, (void *) &qpkt.c.content[0], qpkt.samplesUsed); 
      Serial.printf("Got SERVICE sType %i packet, %i bytes: ['%s'; %i] %i\n", sType, qpkt.samplesUsed, buf.someText, buf.aNumber, qpkt.streamIndx) ;
      break;
    default :
      Serial.printf("Can't handle service type %i, len %i\n", sType, qpkt.samplesUsed);
      printHdr(qpkt.hdr);
  }
  return qpkt.samplesUsed;
}
