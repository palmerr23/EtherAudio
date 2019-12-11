/* EtherNet transmit audio datagrams
 * Example:  Broadcast 2 x sine audio signals to all hosts on network 
 * using Teensy Audio library and Ethernet Library
 * WIZ 5500 preferred
 * EtherNet object DOES NOT have an ISR() so include something that does for packets to flow.  SPDIF works on both T3 & T4
 * While it is possible to use a network shared with other traffic, a separate auido network (10/100 switch) is recommended to avoid packet dropout.
 */

#define DEBUG 1     // > 0 for debug messages to Serial Monitor

// Assumes WIZNET 5500 shield attached to pins CS/SS=10, MOSI=11, MISO=12, SCK=13, RST=9
// pin definitions must be before #include <audio.h>
//#define ResetWIZ_PIN 9
//#define WIZ_CS_PIN 10 

#include <Audio.h>
#include <Wire.h>
#include <SPI.h>
#include <SD.h>
#include <SerialFlash.h>

// GUItool: begin automatically generated code
AudioOutputSPDIF         spdif1;          // need something with an ISR to make update() work - no DAC on T4
AudioControlEtherNet     EtherNet1;     
AudioSynthWaveformSine   sine1;         
AudioSynthWaveformSine   sine2;         
AudioOutputNet           net_out1;      
AudioConnection          patchCord1(sine1, 0, net_out1, 0);
AudioConnection          patchCord3(sine2, 0, net_out1, 1);
AudioConnection          patchCord2(sine1, 0, spdif1, 0); // spdif isn't active unless connected
// GUItool: end automatically generated code

// Enter a unique HostID (1-254) 
#define MYID 2    // range: 1 - 254, must be different for each Teensy connected.

void setup() {
    AudioMemory(40);
   
#if DEBUG >0
  Serial.begin(38400);
   while (!Serial) 
    delay(1); 
  Serial.println("Ethernet Audio - Broadcast single stream - 2 sine waves");
#endif  
  EtherNet1.enable(); // order is important - start the control first
  net_out1.begin();
  EtherNet1.setMyID(MYID); // Assumes a Class C network and IP = (192, 168, 1, MYID).
  net_out1.setControl(&EtherNet1);
  net_out1.setAudioTargetID(TARGET_BCAST);
  net_out1.setStreamName("MyStream1");

  sine1.frequency(220);
  sine1.amplitude(0.9);
  sine2.frequency(100);
  sine2.amplitude(0.7); 
  
#if DEBUG > 0   
    // Check  Ethernet 
    printDiagnostics();
#endif
}

#define TIME_PRINT_EVERY 10   // seconds
controlQueue cQbuf;

void loop() {  
  // read control packets
  while (EtherNet1.getQueuedUserControlMsg(&cQbuf)) // need to reguarly read the control queue for incoming messages
    ; // simply discard them 

  // end of mandatory processing
  
#if DEBUG > 0            
      if((micros() % (TIME_PRINT_EVERY * 1000000)) == 0) // print every few seconds
          printGeneralStats();               
#endif
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
