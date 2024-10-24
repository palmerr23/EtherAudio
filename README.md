# <a name="_toc180675723"></a>Teensy Ethernet Audio Library for Teensy 4.1

## This is ALPHA code and may change without notice

Version 2.0.1 

The Ethernet Audio library provides connectivity between hosts on an Ethernet network for audio, text and structured data (service) functions. It assumes a T4.1 with a native Ethernet connection on a class C network. While 10 Mbps connections are supported, 100 Mbps is required for more than four channels of audio.

The library uses the VBAN UDP protocol (<https://vb-audio.com/Services/support.htm#VBAN>) to transmit messages over IP-based ethernet using QNEthernet (https://github.com/ssilverman/QNEthernet/).

Audio streams are compatible with VB-Audio products such as Voicemeeter (<https://vb-audio.com/>), Talkie and Receptor on Windows, Android and IOS. Only Teensy Audio compatible, 44.1kHz INT16 PCM, audio is supported.

This library is distributed under the "AGPL-3.0-or-later" license. Please contact the author if you wish to inquire about other license options.
# Contents
1. [Introduction](#_toc180675724)
   * [Using this library](#_toc180675725)  
2. [Ethernet management](#_toc180675726)
   * [Debugging](#_toc180675727)
   * [Network quality, buffering and latency](#_toc180675728)
   *   [Sample Code](#_toc180675729)
3. [Audio Streams](#_toc180675730)
   * [Subscribing to Inputs and Outputs](#_toc180675731)
   * [Sample Code](#_toc180675732)
4. [Non-audio (Service) streams](#_toc180675733)
   * [Structured data](#_toc180675734)
   * [Unstructured data](#_toc180675735)
   * [Long messages](#_toc180675736)
   * [Sample Code](#_toc180675737)
5. [Examples](#_toc180675738)
   * [MultiStreamAudio](#_toc180675739)
   * [BroadcastChatVoicemeeter](#_toc180675740)
   * [ExchangeStructuredData](#_toc180675741)
6. [Bugs & Limitations](#_toc180675742)
7. [To Do](#_toc180675743)
8. [For developers](#_toc180675744)
   * [AudioControlEtherNet](#_toc180675745)
   * [Subscriptions](#_toc180675746)
   * [Queues](#_toc180675747)
9. [Other VBAN Sub-protocols](#_toc180675748)
   * [Text (TBC)](#_toc180675749)
   * [MIDI – using Serial or Service(TBC)](#_toc180675750)


## <a name="_toc180675724"></a>Introduction
*`AudioControlEthernet`* provides all user-facing functions and must be explicitly instanced. The underlying Ethernet and UDP packet management is handled by  *`AudioControlEtherTransport`* which is invisible at the user layer.

The library provides MDNS, so hosts can be referenced by hostname.local as well as by their IP addresses.

The underlying network must provide DHCP services.

*`AudioInputNet`* and *`AudioOutputNet`* handle audio traffic, and behave like other teensy Audio Library input and output objects.

*`AudioInputServiceNet`* and *`AudioOutputServiceNet`* can transport either text or structured data and have user-defined fields to aid in message triage.

Host identification is handled transparently, with hostname to IPAddress matching 

Noe of the objects in this library can take update\_responsibility.
## <a name="_toc180675725"></a>Using this library
- As with QNEthernet, the underlying loop that services UDP packets is called from *`yield()`*and delay(). This also means that you should use delay(mS), or *yield()*,when waiting on conditions. Blocking code that doesn’t call these functions will cause UDP packet processing to stall. 
- None of the objects associated with this library can take update\_responsibility.
## <a name="_toc180675726"></a>Ethernet management
- AudioControlEthernet manages all the user-facing Ethernet, IP and UDP management tasks. An instance is required in user code, as for other control objects.
- *`setHostName(host)`* sets the hostname (not fully qualified) and may be called at any time. The HostName is used to identify VBAN senders.
- *`setPort(port)`* sets the UDP listen port number for both sending and receiving. The default is the VBAN standard, 6980. This fucntion may be called at any time.
- *`begin()`* connects to the network. Currently the network must support DHCP.
- MDNS is supported and will respond to HostName.local.
- *`setUserName(uName)`* and *`setAppName(app)`* two other parameters visible in Voicemeeter’s VBAN stream info pop-up. They are not currently used for any practical purpose in this library.
- *`getMyIP()`* returns the DHCP-provided IP address.
- *`getBroadcastIP`*() returns a class C broadcast (.255) address for the DHCP-provided network address.
- *`getLinkStatus()`* reports whether ethernet is active.
- If subscriptions are made using hostNames, it may be wise for each host to regularly (every few seconds) call  *`announce()`*, which broadcasts a PING REPLY message. 
- *`setColour()`* sets VBAN Ping *`color_rgb`* (24-bit BGR) which is used as the background colour for Voicemeeter chat messages from this host. The default value is dark red (0x0000C0).
- If the cable is disconnected at startup, or becomes disconnected for more than 30 seconds connection may fail. 
### <a name="_toc180675727"></a>Debugging
- *`AudioControlEtherTransport`* is shared by all the ethernet audio objects and has no functions intended for end-user code. 
- It is also possible, but not recommended, for user code to access the underlying *`AudioControlEtherTransport`* , and QNEthernet *`Ethernet`* / *`udp`* objects. 
- To allow access to QNEthernet objects, user code should directly include *`#include "QNEthernet.h`*" and declare *`using namespace qindesign::network`*.
### <a name="_toc180675728"></a>Network quality, buffering and latency
If the network quality is poor, incoming packets may be dropped. At this point there is no error correction for dropped frames.

*`droppedFrames(bool reset)`* provides the number of VBAN frames that failed to be processed since the last reset.

When an incoming queue grows longer than *`MAX_AUDIO_QUEUE`*, frames are dropped. 

Similarly for outputs, for instance when there is a network disconnection. There does not need to be an active receiver for output packet streams.
### <a name="_toc180675729"></a>Sample Code
    // Connect to Ethernet but do no audio processing.
    #include "control_ethernet.h"
    AudioControlEtherNet   ether1;
    setup()
    {
      ether1.setHostName("Teensy1");
      ether1.begin();
      IPAddress myIP = ether1. getMyIP();
      Serial.println(myIP);
    }
    loop()
    {
      // no code is required in the loop
    }

## <a name="_toc180675730"></a>Audio Streams
*`AudioInputNet`* and *`AudioOutputNet`* carry audio in 44.1kHz INT16 PCM (Teensy Audio) format.

They may be instanced with 1 to 8 audio channels. The default is 2 channels.

Creating input objects with more channels than required has little impact on memory or processing. 

Output objects with more than five channels produce two VBAN packets per Teensy Audio buffer and consume twice as much queue memory.

Multiple instances of input and output objects are allowed, however each should have a distinct *`streamName`*. 

Be aware of the CPU, AudioMemory and Ethernet bandwidth impacts of large numbers of channels as each channel creates buffers, a packet queue and a separate packet stream.

*`begin()`* must be called for each input or output instance.

As with other Teensy Audio objects, individual inputs and outputs may be left unconnected (no AudioConnections). 
### <a name="_toc180675731"></a>Subscribing to Inputs and Outputs
Only one subscription is permitted per VBAN stream for both inputs and outputs. Each stream must have a unique streamName. The same streamName may be used for both an input and output stream.

Only one *`AudioConnection`* is allowed to any individual output channel.

*`subscribe(streamName, hostName)`* connects to an incoming stream. *`hostName`* may be omitted for output streams where the stream is to be broadcast, or for inputs where there is only one other host emitting that streamName on the network. Currently this form of *`subscribe()`* is not enabled for output streams. Subscriptions by fully-qualified hostName is not yet supported.

*`Subscribe(streamName, IPAddress)`* acts similarly for output. For broadcast output streams, omit the second argument. IPAddress can either be a broadcast address, obtained with *`getBroadcastIP()`* or the full IPV4 address.

A stream only becomes active once it is subscribed. No outgoing packets are sent, and all incoming received packets are dumped, for inactive streams. 

Subscriptions can be made prior to VBAN packets appearing, as there is a regular housekeeping function that tries to match orphan streams to subscriptions. 

*`unsubscribe()`* frees the input or output stream.
### <a name="_toc180675732"></a>Sample Code
    // Connect to Ethernet and process audio packets
    #include "control_ethernet.h"
    #include "input_net.h"
    #include "output_net.h"

      AudioControlEtherNet   ether1;
      AudioInputNet             in1(8);   // 8-channel input
      AudioOutputNet         out1(2); // 2-channel output
      // patchCords as required
    setup()
    {
    …
    AudioMemory(10);		// need at least the total number of active channels (AudioConnections)
      ether1.setHostName(“myHost”);
      ether1.begin();
 
     in1.begin();  
     in1.subscribe(“Stream1”);

      out1.subscribe(“myStream”);
      out1.begin();
    …
    }
    loop()
    {
      // no additional code is required in loop()
    }
# <a name="_toc180675733"></a>Non-audio (Service) streams
The Serve sub-protocol can send and receive structured or unstructured (text) data.
## <a name="_toc180675734"></a>Structured data
- Payloads of up to 1436 bytes are supported.
- An 8-bit service type identifier hdr.format\_nbc transmitted with each packet is used to identify user-defined message types. 
  - Service type 1 is Voicemeeter CHAT (UTF8, but generally ASCII readable) and may be subscribed by end user code. StreamName is ignored by Voicemeeter for CHAT.
  - End-user code should not use the following pre-defined service types in the VBAN specification:
    - 0 = PING – incoming PING packets are trapped by the controlEthernet object.
    - 32 = RTPACKETREGISTER or 33 = RTPACKET unless wanting to engage the RTPACKET service.
- The message length is available in each queued packet’s header (samplesUsed).

Possible uses include the regular communication of a set of control parameters or audio levels for remote display.
## <a name="_toc180675735"></a>Unstructured data
Text can be transferred in the same way as structured data. VBAN chat is explicitly supported using the VBAN\_SERVICE\_CHAT *`serviceType`* (see hdr.format\_nbc). 
## <a name="_toc180675736"></a>Long messages
Multi-frame reconstruction is not directly implemented, so messages of more than 1436 are not supported. For instance, long MIDI SYSEX messages may be broken by this limitation.

Not yet implemented: Long messages may, however, be handled by user code. When writing extended packets, the optional argument extend may be set to true, and is returned as a header flag in packets received. The last packet in the sequence should set the extend argument to false.
### <a name="_toc180675737"></a>Sample Code
    // Chat with another host
    #include "control_ethernet.h"
    #include "inputService_net.h"
    #include "outputService_net.h"
      AudioControlEtherNet   ether1;
      AudioInputServiceNet      inChat;
      AudioOutputServiceNet     outChat;

    setup()
    {
    …
    // AudioMemory is not consumed by non-audio traffic
      ether1.setHostName(“myHost”);
      ether1.begin();
      inChat.subscribe(“myChatStream”, VBAN_SERVICE_CHAT, myChatHost);   
      inChat.begin();
      IPAddress chatHostIP = {192,168,10,3};
      outChat.subscribe(“myChatStream”, VBAN_SERVICE_CHAT, chatHostIP);
      outChat.begin();
    …
    }
    loop()
    {
    // Process incoming and outgoing chat messages – see VoicemeeterChat example.
    }

# <a name="_toc180675738"></a>Examples
### <a name="_toc180675739"></a>MultiStreamAudio
This example requires two Teensy 4.1s or a single Teensy 4.1 and Voicemeeter.

A Teensy Audio Shield one Teensy is useful to monitor/display audio.

- Create one output and two input single-channel AUDIO objects.
- Feed a synthesised sine wave into the audio output.
- Subscribe all objects to separate streamNames.
- If Voicemeeter:
  - Set up Voicemeeter on the same network and start VBAN.
  - Subscribe to the Teensy’s output stream.
  - Send the signal back as two separate streams.
- Pick up any incoming streams on the Teensy and send them to the Audio Shield.
- Regularly display the peak value of any incoming streams.
### <a name="_toc180675740"></a>BroadcastChatVoicemeeter
This example requires two Teensy 4.1s OR a Teensy 4.1 and Voicemeeter.

- Create input and output TEXT objects.
- Subscribe them to the same streamName.
- Take USB Serial input lines and send (broadcast) them.
- Receive VBAN TEXT messages and print them to USB Serial.
### <a name="_toc180675741"></a>ExchangeStructuredData
Create an input and output SERVICE object pair on the two hosts.

- Subscribe them to the same streamName.
- Define a service subType for your traffic.
- Define and send (broadcast) a structure or string.
- Receive the data and display the content as a string or structure depending on the received service subType (pkt->hdr.format\_nbc).
# <a name="_toc180675742"></a>Bugs & Limitations
- Starting with the cable connected and the network active is usually required for a successful connection. Connecting the network cable more than 30 seconds after boot has a high likelihood of a failed connection.
- Cable disconnection during a session is not handled perfectly. 
- A restart is required if the network is changed (i.e. plugged in to a different IP range) as subscriptions will not be updated.
- If another host changes its IP address during a session (e.g. unplugged and re-plugged with a different IP address) subscriptions will may not renew without a restart.
- Initial packets of any stream get eaten – The input object’s subscription isn’t joined to a stream until after the first packet is registered, so there is no queue yet defined to take it. When it is a previously unregistered host (IPAddress to hostName) any packets received until the host has been pinged (automatic on receipt of packets from an unknown host) and the response processed.
  This is usually inconsequential for audio, but may be significant if Service packets are lost.
- Different streams will have different average queue lengths which will result in different group delays. The effect results in greater phase differences at higher frequencies. The group delay is constant, except on poor networks where dropped packets occur.
# <a name="_toc180675743"></a>To Do
- Ethernet
  - Restart after disconnection
  - Audio stream deactivation on cessation of packets – is this needed?
- Service
  - Long message flag handling
- MIDI 
  - basic support 
  - long message flag handling 
  - transparent long (multi-frame) message send and receive reconstruction (Perhaps best done in user code?)
- Text 
  - multiple writes before send and partial buffer reads.
  - UTF8 and WCHAR text format flagging (only if requested).
- Audio
  - Subscription by Hostname for output packets.
  - Update subscriptions after a network change.
  - Dropped packets (see Queues, below).
  - Time synchronization of separate streams.
- Subscribe
  - Add an 8-bit (final digit) IPAddress option to subscribe(streamName, IPAddress).
  - Support FQDN subscriptions.
  - Support hostname output subscriptions
# <a name="_toc180675744"></a>For developers
### <a name="_toc180675745"></a>AudioControlEtherNet
AudioControlEtherNet has the user-code facing functions and is instanced like any other Teensy Audio control object.

AudioControlEtherNet creates a static global AudioControlEtherTransport instance etherTran which handles network tasks and ethernet-facing queue management. This allows etherTran to be accessed transparently from input and output objects. Its static updateNet() function is hooked into EventResponder which runs at every call to yield(), at the end of each loop() and while delay() is operating. 

Each type of VBAN input packet (audio, service and MIDI == serial) shares the same queue packet format.

samplesUsed is used for managing different VBAN packet and Audio buffer sizes for audio inputs and outputs. For incoming service packets it contains the length of the data payload.
### <a name="_toc180675746"></a>Subscriptions
Subscriptions tie an input object to a host/stream of the same VBAN sub-protocol. Subscriptions may be made before an incoming stream becomes active.

If a new stream comes from a previously unknown host, a PING0 packet is sent with this host’s credentials.

If a PING ‘REPLY’ is received, matching inactive subscriptions are made active (updateActiveStreams()). This matching is also performed regularly by housekeeping called from updateNet().

Outgoing streams do not have subscriptions and have only a streamsOut entry and a queue. This precludes subscribe() by hostname for outgoing streams, which may be addressed in a later release.
### <a name="_toc180675747"></a>Queues
- Each input or output object has its own packet queue 
  std::queue <queuePkt> \_myQueueX;
- These queues are registered with ‘AudioControlEtherTransport’ by calls to subscribe().
- Subscriptions (subsIn[]) tie incoming VBAN packet streams (streamsIn[]) to individual packet queues which are then processed by the appropriate input object. 
- Queues are kept from growing during fault conditions by not pushing packets if the queue’s size() grows to a fixed value.
- There is work to be done on error correction when packets are dropped. Not popping the following packet from the queue, and modifying its hdr.nuFrame, would appear to be the simplest approach. 
- Queue management activity in the control object needs to be protected against AudioStream update() interrupts.
# <a name="_toc180675748"></a>Other VBAN Sub-protocols
## <a name="_toc180675749"></a>Text (TBC) 
Use the Service sub-protocol for sending and receiving text.
## <a name="_toc180675750"></a>MIDI – using Serial or Service(TBC)
VBAN offers specific support for MIDI in the Serial sub-protocol (VBAN Specification, p17). This appears to be aimed at direct to UART implementations 

MIDI messages may be transferred using the Service sub-protocol (untested).

## Thanks
Special thanks go to Shawn Silverman for assistance in ironing out the network layer bugs. 

Thanks also to Vincent Burel for the valuable assistance with all things VBAN.

Processed with https://products.aspose.app/words/conversion/word-to-md
