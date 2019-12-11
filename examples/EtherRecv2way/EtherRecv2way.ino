/*
 * UDP receive datagrams - audio library
 * Example: Create an audio loop between two hosts
 *          Send Sine (Out CH 0) -> 
 *          Loop: Recv (In Ch 0) -> Recv (Out Ch0) -> [and DAC on T3.5 or 6]
 *          Loop: Send (In Ch 0) -> Send (Out Ch 1) -> 
 *          Recv (In Ch 1)  -> DAC
 * using Teensy Audio library and Ethernet Library
 * Example: Receive a single stream. Loop input channel[0] back to other host on output channel [0]. 
 *   When the companion "send" example is used, input channel[1] is the looped signal returned.
 * WIZ 5500 preferred
 * The Ethernet code DOES NOT have an ISR() so something that does is required in the sketch for packets to flow (PWM or DAC used here). 
 */

#define DEBUG 2     // > 0 for debug messages to Serial Monitor

//#define T4    // DACs not implemented on T4, so use PWM output instead.
//#define T35_6 // Two DACs
#define T32     // only one DAC

// Assumes WIZNET 5500 shield attached to pins CS/SS=10, MOSI=11, MISO=12, SCK=13, RST=9
//#define ResetWIZ_PIN 9
//#define WIZ_CS_PIN 10 

// Enter a unique HostID (1-254) 
// Assumes a Class C network and IP = (192, 168, 1, MYID).
#define MYID 5    // range: 1 - 254, must be different for each Teensy connected.

#include <Audio.h>
#include <Wire.h>
#include <SPI.h>
#include <SD.h>
#include <SerialFlash.h>

// GUItool: begin automatically generated code
AudioControlEtherNet     EtherNet1;      
AudioInputNet            net_in1;        
AudioOutputNet           net_out1;   
AudioConnection          patchCord3(net_in1, 0, net_out1, 0);   
#ifdef T4     // T4 use PWM for output and to provide interrupts
AudioOutputPWM           pwm1;          
AudioConnection          patchCord1(net_in1, 1, pwm1, 0);
#endif
#ifdef T35_6         // other Teensys have DACS for better output quality
AudioOutputAnalogStereo  dacs1;          
AudioConnection          patchCord1(net_in1, 0, dacs1, 0);
AudioConnection          patchCord2(net_in1, 1, dacs1, 1);
#endif
#ifdef T32         // other Teensys have DACS for better output quality
AudioOutputAnalog        dacs1;          
AudioConnection          patchCord1(net_in1, 1, dacs1, 0);
#endif
// GUItool: end automatically generated code

unsigned long  pkts, lastdropped, dropped = 0;
short wiztype;
//#define CE_SYNC_PIN 2  
//bool syncPulse_CE = false;
void setup() {
 // pinMode(CE_SYNC_PIN, OUTPUT);
 // digitalWrite(CE_SYNC_PIN, LOW);
 AudioMemory(40);
#if DEBUG >0
    // Open serial communications and wait for port to open:
  Serial.begin(38400);
  while (!Serial) 
    ; // wait for serial port to connect. Needed for native USB port only
  Serial.println("Ethernet Audio - Receive a single stream - loop channel[0] back to other host. \n Channel 1 has looped signal back from Send");
#endif   
delay(100);
    // start the Ethernet
    EtherNet1.enable(); // order is important - start the control first
    EtherNet1.setMyID(MYID);
    net_in1.setControl(&EtherNet1);
    net_out1.setControl(&EtherNet1);
    net_out1.setAudioTargetID(TARGET_BCAST);
    net_out1.setStreamName("Stream_F");
 
#if DEBUG > 0
    printDiagnostics();
#endif 
}

#define TIME_PRINT_EVERY 5 // secs

bool subscribed1 = false;
short streamsAvail;
controlQueue cQbuf;

void loop() {
  // all audio processing happens in background
  // subscribe to an input stream - mandatory for inputs
  if(!subscribed1) // in loop() as streams are not guaranteed to be available in setup()
  {
       streamsAvail = EtherNet1.getActiveStreams(); // Ethernet control owns the streams
#if DEBUG > 0
      if (streamsAvail > 0)
        printStreams();
#endif   
      // Subscribe to an incoming stream, via an input control, once they become available 
      if(streamsAvail > 0)
          subscribed1 = net_in1.subscribeStream( 0 );  // using streamID = 0 as an example
  }   
   
  // read control packets - mandatory
  while (EtherNet1.getQueuedUserControlMsg(&cQbuf)) // need to regularly read the control queue for incoming messages  
       ; // process any messages we care about

   // end of mandatory processing
  
#if DEBUG > 0
    // general statistics report
    if((micros() % (TIME_PRINT_EVERY * 1000000)) == 0) // print every few seconds
    {
         printGeneralStats();     
         printStreams();
    }
#endif
}

void printStreams()
{
  short i, maxStream;
  netAudioStream stream;
  maxStream = EtherNet1.getActiveStreams();
  Serial.print("\nStreams\n");
  for (i = 0; i < maxStream; i++)
  {
      stream = EtherNet1.getStream(i);
      Serial.print("Name: "); Serial.print(stream.streamName); 
      Serial.print(", Host: "); Serial.print(stream.remoteHostID);
      Serial.print(", Stream: "); Serial.print(stream.hostStreamID);
      Serial.print(", Active: "); Serial.println(stream.active ? " Y": " N");
  }
}
void printDiagnostics()
{
short wiztype;
    // Check for Ethernet hardware present
    // Warning: very few direct calls to the Ethernet library may be executed from a user program, as most directly issue SPI transactions.
    if ((wiztype = Ethernet.hardwareStatus()) == EthernetNoHardware) {
      Serial.println("Ethernet shield was not found.  Sorry, can't run without hardware.");   
    }
    else {
        Serial.print("Found WIZ type ");
        Serial.println(wiztype);    
    }
    printIP("My IP is ", EtherNet1.getMyIP());
    Serial.print("Ethernet cable connected = ");
    Serial.println(EtherNet1.getLinkStatus() ? "Yes" : "No");
    Serial.print("Audio Memory "); Serial.println(AudioMemoryUsage());
    Serial.println("Exiting setup() - ethernet link may take some time to connect. \n______________________");
}
void printGeneralStats()
{
        Serial.print("\nEthernet cable connected = ");
        Serial.println(EtherNet1.getLinkStatus() ? "Yes" : "No");
        Serial.print("Processor (Curr/Max) = ");  Serial.print(AudioProcessorUsage());
        Serial.print("/"); Serial.print(AudioProcessorUsageMax());   
        Serial.print(". Audio Memory (Curr/Max) = ");  Serial.print(AudioMemoryUsage());
        Serial.print("/"); Serial.print(AudioMemoryUsageMax());  
}

void printIP(char text[], IPAddress buf){
  short j;
     Serial.print(text);
     for (j=0; j<4; j++){
       Serial.print(buf[j]); Serial.print(" ");
     }
     Serial.println();
}
