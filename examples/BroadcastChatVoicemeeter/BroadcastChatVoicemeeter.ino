// BroadcastChat for Teensy Ethernet Audio Library
// Requires two Teensy 4.1s with Ethernet adaptors or one Teensy 4.1 and a PC with Voicemeeter on the same network
// When using Voicemeeter: send a line from Teensy first, to register it as a new host.
// BUG: The first few CHAT messages from a remote host are eaten as the host is identified and the incoming stream registered. 

#define TWO_TEENSYS
//#define VERBOSE
#include <Audio.h>

#include "control_ethernet.h"
#include "inputService_net.h"
#include "outputService_net.h"

AudioControlEthernet      ether1;
AudioInputServiceNet      inChat;
AudioOutputServiceNet     outChat;
// patchCords are not required for non_Audio traffic

#include "utils.h"
char myHost[16] = "Teensy";
IPAddress myIP;
IPAddress myBroadcastIP;
char myAudioOutStream[] = "myStream";
char myChatStream[] =  "myStreamChat";
//char myChatStream[] = "VBAN Service"; // all Voicemeeter chat sreams have this name
char myChatHost[] = "SURFACE-RP"; 

void setup() 
{
  Serial.begin(115200);
  while (!Serial && millis() < 5000) 
  {
    delay(10);
  }

  Serial.println("\n\nStarting BroadcastChat example");

  uint32_t colour = 0xc00000; // (blue) ***BGR*** 24-bit chat bg colour (default is red)
#ifdef TWO_TEENSYS // randomise the hostname
  randomSeed(millis()); // otherwise we'll get the same initial number every time
  int hostNum = random(1, 999);
  char buf[4];
  itoa(hostNum, buf, 10);
  strcat(myHost, buf);
  colour = random(128, 65535*256); // 24-bits, avoid very small values 
#endif

  ether1.setColour(colour);
  ether1.setHostName(myHost);
  ether1.begin();
  if(!ether1.linkIsUp())
    Serial.printf("Ethernet is disconnected");
  else  
    Serial.println(myIP = ether1.getMyIP());
 
  Serial.printf("Chat In subscription %i\n",inChat.subscribe(myChatStream, VBAN_SERVICE_CHAT, myChatHost));   
  inChat.begin();
 
  myBroadcastIP = ether1.getBroadcastIP();
  outChat.subscribe(myChatStream, VBAN_SERVICE_CHAT, myBroadcastIP);
  outChat.begin();

  Serial.println("Done setup");
}

long count = 0;
long timer1;

void loop() 
{

  char buffer[80];
  uint8_t sType;
  int charsRead = VBANchatInput(buffer, &sType);
  if(charsRead)
  {
    Serial.printf(">>>>> CHAT got %i chars, service type %i: '", charsRead, sType);
    Serial.print(buffer);
    Serial.println("'");
  }
  serialToVBANchat();
#ifdef VERBOSE
  if(millis() - timer1 > 20000)
  {
    Serial.printf("---------- Main: %i\n", millis()/1000);
    Serial.printf("LinkIs Up %i, IP ", ether1.linkIsUp());     
    Serial.println(ether1.getMyIP()); // test DHCP lease on cable disconnect / reconnect. VBAN will need to be restarted on Voicemeeter if IP changes.
    printActiveStreams(STREAM_IN);
    printActiveStreams(STREAM_OUT);
     ether1.printHosts();
    printActiveSubs();
    timer1 = millis();
  }
  #endif
  // regular Ethernet processing is tied to yield() and delay() 
  delay(100); // code delays do not disturb processing
}

void serialToVBANchat()
{
  char buf[80];
  if(!Serial.available())
    return;

  int count = 0;
  while(Serial.available())
  {
    buf[count] = Serial.read();
    count++;
  }
  buf[count] = '\0';

  outChat.send((uint8_t*)buf, strlen(buf), myChatStream, VBAN_SERVICE_CHAT); // broadcast, so no final argument
  Serial.printf("<<<<< Chat sent %i chars '%s'\n", count, buf);
}

int VBANchatInput(char *buffer, uint8_t *sType)
{ 

  *buffer = '\0';
  int pkts = inChat.getPktsInQueue();
  if(pkts == 0)
    return 0;
  //Serial.printf("VCI got %i packets\n", pkts);
  queuePkt qpkt = inChat.getPkt();
  *sType = qpkt.hdr.format_nbc; // return the service type
  memcpy((void*)buffer, (void *) qpkt.c.content, qpkt.samplesUsed);  
  buffer[qpkt.samplesUsed] = '\0'; // Voicemeeter chat pkt is not null-terminated
  return qpkt.samplesUsed;

}
