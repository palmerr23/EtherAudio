// Multi Stream Audio test for Teensy Ethernet Audio Library
// Requires:  two Teensy 4.1s with Ethernet adaptors 
//            or one Teensy 4.1 and a PC with Voicemeeter on the same network
//            An Audio Board on one Teensy is helpful to view signals, but not mandatory.

/* If using Voicemeeter: Loop back the signal from the Teensy and display its peak level and output to I2S 
  Go to the VBAN screen
    subscribe to the incoming Stream1 from Teensy1.local 
    output Stream1 to your network broadcast address (X.X.X.255)
    turn VBAN on
  On the main Voicemeeter screen
    Input the incoming stream and output it again on BUS A - to create a loopback to the Teensy
  After a few seconds the input signal should appear on both the main Voicemeeter and VBAN screens.
  The sine wave looped back from Voicemeeter should appear on the I2S output and as a peak reaging on the serial monitor.

  Two Teensys:
    Just connect both to the network.
    In1.peak should read 0.900 and the signal should appear on one channel of the I2S.
    in2.peak will read 1.000 because no Audio buffers are being provided. 
*/

#define HAVE_AUDIO_BOARD // uncomment if Teensy Audio Board is mounted (won't fail if left defined without an audio board)
#include <Audio.h>

#ifdef HAVE_AUDIO_BOARD
  #include <Wire.h>
  #include <SPI.h>
  #include <SD.h>
  #include <SerialFlash.h>
#endif

#include "control_ethernet.h"
#include "input_net.h"
#include "output_net.h"



AudioControlEthernet   ether1;
AudioInputNet          in1(1);
AudioInputNet          in2(1);
AudioSynthWaveformSine sine1;    
AudioOutputNet         out1(1);
   
AudioAnalyzePeak       peak1;
AudioAnalyzePeak       peak2;

AudioConnection       patchCord10(sine1, 0, out1, 0);
AudioConnection       patchCord1(in1, 0, peak1, 0);
AudioConnection       patchCord4(in2, 0, peak2, 0);

#ifdef HAVE_AUDIO_BOARD
  AudioOutputI2S         i2s1; // update_responsibility  
  AudioConnection       patchCord2(in1, 0, i2s1, 0);
  AudioConnection       patchCord3(in2, 0, i2s1, 1);
  AudioControlSGTL5000  sgtl;
#else
  AudioConnection       patchCord99(sine1, 0, out1, 1);
#endif
#include "utils.h" // diagnostic print functions - assumes "AudioControlEthernet  ether1;""
void setup() 
{
  AudioMemory(10); // more than required

  Serial.begin(115200);
  while (!Serial && millis() < 5000) 
  {
    delay(10);
  }
  Serial.println("\n\nStarting Multi Audio Stream Test");

  char myHost[] = "Teensy1";
  ether1.setHostName(myHost);
  ether1.begin();
  if(!ether1.linkIsUp())
    Serial.printf("Ethernet is disconnected");
  else
    Serial.println(ether1.getMyIP());

  in1.begin(); 
  in2.begin();
   
  out1.subscribe("Stream1");
  out1.begin();

  sine1.frequency(1000);
  sine1.amplitude(.9);

#ifdef HAVE_AUDIO_BOARD
  sgtl.enable();
  sgtl.volume(1);
  sgtl.unmuteLineout();
#endif
  char s1[] = "Stream1";
  char s2[] = "Stream2";
  in1.subscribe(s1);
  in2.subscribe(s2);

  Serial.println("Done setup");
}

#define EVERY 1000
long count = 0;
long timer1 = -3000;

void loop() 
{
  if(millis() - timer1 > 10000)
  {

    float val1 = peak1.read();    
    float val2 = peak2.read(); 
    Serial.printf("---------- Main: %i\n", millis()/1000);
    Serial.printf("Peak 1: %1.4f, 2: %1.4f \n", val1, val2);
    Serial.printf("LinkIs Up %i, IP ", ether1.linkIsUp());
    Serial.println(ether1.getMyIP());
    printActiveStreams(STREAM_IN);
    printActiveStreams(STREAM_OUT);
    ether1.printHosts();
    printActiveSubs();
    timer1 = millis();
  }
  // regular Ethernet processing is tied to yield() and delay() 
  delay(100); // code delays do not disturb processing
}
