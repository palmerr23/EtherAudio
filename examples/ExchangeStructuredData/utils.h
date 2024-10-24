
void printActiveStreams(int direction)
{
  stream_pretty st;
for(int i = 0; i < MAX_UDP_STREAMS; i++)  
  {
     st = ether1.getStreamInfo(i, direction);
     if(st.active)
     {
        Serial.printf("Stream %s [%i] ", (direction)? "Out" : "In", i);
        Serial.printf("Host '%s', IP ",  st.hostName);
        Serial.print(st.ipAddress);
        Serial.printf(" Name '%s', ",  st.streamName);
        Serial.printf("Proto 0x%02X, ", st.protocol);
        if(st.protocol == (int)VBAN_AUDIO)
          Serial.printf(" Chans %i,  SRate %i,", st.channels, st.sampleRate);
        Serial.printf(" %s", (st.active)?"Active" : "Inactive");
        if(direction == STREAM_IN)
          Serial.printf(", Subscribed from %i\n", st.subscription);
        else
          Serial.println();
     }
  }
}
void printActiveSubs()
{
  subscription st;
  for(int i = 0; i < MAX_SUBSCRIPTIONS; i++)  
  {
     st = ether1.getSubInfo(i);
     if(st.active)
     {
        Serial.printf("Subscription %i: ", i);
        Serial.printf("Host '%s', IP ",  st.hostName);
        Serial.print(st.ipAddress);
        Serial.printf(" StreamID %i, ", st.streamID);  
        Serial.printf("Name '%s', ",  st.streamName);
        Serial.printf("Proto 0x%2X, ", st.protocol);
        Serial.printf("SType 0x%2X, ", st.serviceType);       
        Serial.printf("%s\n", (st.active)?"Active" : "Inactive");
     }
  }
}
void printHdr(vban_header hdr)
{
	Serial.printf("Hdr: '%c' SR 0x%02X, nbs 0x%02X, nbc 0x%02X, bit 0x%02X, stream '%s' frame %i\n", (char)hdr.vban, hdr.format_SR, hdr.format_nbs, hdr.format_nbc, hdr.format_bit, hdr.streamname, hdr.nuFrame);
}
