
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
        Serial.printf(" Name '%s',",  st.streamName);
        Serial.printf("Proto 0x%2X, ", st.protocol);
         Serial.printf("SType 0x%2X, ", st.serviceType);
         Serial.printf("StreamID 0x%2X, ", st.streamID);         
        Serial.printf(" %s\n", (st.active)?"Active" : "Inactive");
     }
  }
}
