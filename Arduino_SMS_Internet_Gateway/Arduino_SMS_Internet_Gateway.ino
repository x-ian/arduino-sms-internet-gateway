#include <Dhcp.h>
#include <Dns.h>
#include <Ethernet.h>
#include <EthernetClient.h>
#include <SPI.h>
#include <EthernetServer.h>
#include <EthernetUdp.h>
#include <util.h>
#include <SimpleTimer.h>
#include <SoftwareSerial.h>

int pinGprsRx = 7;
int pinGrpsTx = 8;
int pinGprsPower = 9;
SoftwareSerial gprs(7, 8);

SimpleTimer checkForNewSmsTimer;

// needs to match value of _SS_MAX_RX_BUFF in SoftwareSerial.h 
// default 64 is not enough, so increase it!
// (found in somewhere like /Applications/Arduino.app/Contents/Resources/Java/libraries/SoftwareSerial)
#define INCOMING_BUFFER_SIZE 128

char incomingNumber[20];
char incomingMessage[INCOMING_BUFFER_SIZE];
char incomingTimestamp[20];

byte mac[] = { 
  0x90, 0xA2, 0xDA, 0x0D, 0x86, 0x4C };
byte ip[] = { 
  192, 168, 1, 177 };
EthernetServer server(80);
EthernetClient client ;

// Google docs stuff
char formkey[] = "<<INSERT YOUR GOOGLE FORM KEY HERE>>"; //Replace with your Key, found in Google Doc URL
byte googleDocsServer[] = { 74,125,153,139 }; // spreadsheets.google.com
EthernetClient googleDocsClient;
char buffer [33];

void setup()
{
  Serial.begin(19200);             // the Serial port of Arduino baud rate.
  while (!Serial) {
    ; // wait for serial port to connect. Needed for Leonardo only
  }
  Serial.println("setup");

  gprs_setup();
  ether_setup();
  delay(1000);

  checkForNewSmsTimer.setInterval(10, checkForNewSms);
}

void loop() {
  handleHttpRequests();
}

// *******************************************************
// Process incoming HTTP requests

void handleHttpRequests() {
  boolean smsSent = false;
  char line[INCOMING_BUFFER_SIZE];

  client = server.available();
  if (client.connected() && client.available()) {
    // assume 1st line is http method
    nextHttpLine(line);
    char * method = strtok(line, " \r\n");
    if (strcmp(method, "POST") == 0) {
      char * uri = strtok(NULL, " \r\n");
      if (strcmp(uri, "/sendsms") == 0) {
        // send out sms message
        while (client.connected() && client.available()) {
          nextHttpLine(line);
          // check for a line "number=+123456789&message=hallo"
          if (strstr(line, "number") != NULL && strstr(line, "message") != NULL) {
            // most likely you want some form of URL decoding as in http://rosettacode.org/wiki/URL_decoding#C
            char * param1 = strtok(line, "&\r\n\0");
            char * param2 = strtok(NULL, "&\r\n\0");
            strtok(param1, "=");
            char * number = strtok(NULL, "=\r\n\0");
            strtok(param2, "=");
            char * message = strtok(NULL, "=\r\n\0");
            gprs_sendTextMessage(number, message);
            smsSent = true;
          }          
        }
      }
    }
    if(smsSent) {
      client.println("HTTP/1.1 200 OK");
      client.println("Content-Type: text/html");
      client.println();
      client.println("<h2>OK - SMS sent.</h2>");
    } 
    else {
      client.println("HTTP/1.1 500 Internal Server Error");
      client.println("Content-Type: text/html");
      client.println();
      client.println("<h2>Something went wrong somewhen and somewhere.</h2>");
    }
  }

  // give the web browser time to receive the data
  delay(100);
  client.stop();
}

void nextHttpLine(char* line) {
  int i = 0;  
  while (client.connected()) {
    if (client.available())  {
      line[i] = client.read();
      if (i == INCOMING_BUFFER_SIZE - 2) {
        line[i+1] = '\0';
        break;
      } 
      else if (i > 0 && line[i-1] == '\r' && line[i] == '\n') {
        // stop here with null terminated string
        // simplification as HTTP spec requires always <CR><LF>
        line[i-1] = '\0';
        break;
      }
    } 
    else {
      line[i] = '\0';
      break;
    }
    i++;
  } 
}

// *******************************************************
// Process incoming text messages

void checkForNewSms() {
  char incomingStoragePosition = gprs_nextAvailableTextIndex();
  gprs_readTextMessage(incomingStoragePosition);
  Serial.println(incomingMessage);
  // check incoming message
  boolean b = checkIncomingTextMessage();
  if (b) Serial.println("parsing of incoming message succesful");
  // forward incoming message
  // gprs delete old message
  //gprs_deleteTextMessage(incomingStoragePosition);
}

void gf_submit(char *entry,char *val){

  char *submit = "&submit=Submit";

  if (googleDocsClient.connect(googleDocsServer, 80)) {
    googleDocsClient.print("POST /formResponse?formkey=");
    googleDocsClient.print(formkey);
    googleDocsClient.println("&ifq HTTP/1.1");
    googleDocsClient.println("Host: spreadsheets.google.com");
    googleDocsClient.println("Content-Type: application/x-www-form-urlencoded");
    googleDocsClient.print("Content-Length: ");    
    googleDocsClient.println(strlen(entry)+1+strlen(val)+strlen(submit),DEC);
    googleDocsClient.println();
    googleDocsClient.print(entry);
    googleDocsClient.print("=");
    googleDocsClient.print(val);
    googleDocsClient.print(submit);
    googleDocsClient.println();
  }   
  delay(1000);
  googleDocsClient.stop();
}

// *******************************************************
// GSM/GPRS shield specific code

char gprs_nextAvailableTextIndex() {
  // make sure nothing unexpected is coming in
  delay(500);
  while (gprs.available() > 0) {
    gprs.read();
  }

  gprs.println("AT+CMGL=\"ALL\"");
  delay(500);
  char c = ' ';
  for (int i = 0; i < 10; i++) {
    c = gprs.read();
    // assume output of CMGL is always '  +CMGL: <one digit number at position 9>'
    // TODO what if number is bigger than 9?
    if (i == 9) return c;
  }
  return '?';
}

void gprs_readTextMessage(char storagePosition) {
  // make sure nothing unexpected is coming in
  delay(500);
  while (gprs.available() > 0) {
    gprs.read();
  }

  gprs.print("AT+CMGR=");
  gprs.println(storagePosition);
  delay(500);
  int count = 0;
  char c = ' ';
  while (gprs.available() > 0 && count < INCOMING_BUFFER_SIZE) {
    c = gprs.read();
    incomingMessage[count++] = c;
  }
}

void gprs_deleteTextMessage(char storagePosition) {
  Serial.print("deleteTextMessage: ");
  Serial.println(storagePosition);

  delay(500);
  gprs.print("AT+CMGD=");
  gprs.println(storagePosition);
  delay(500);
}

void gprs_setup() {
  gprs_powerUpOrDown();
  delay(500);
  gprs.begin(19200); // the default GPRS baud rate   
  delay(500);
  //gprs_setTime();
  delay(500);
  gprs.println("ATE0"); // SMS in text mode
  delay(100);
  gprs.println("AT+CMGF=1"); // SMS in text mode
  delay(100);
}

void gprs_sendTextMessage(String number, char* message) {
  Serial.print("sendTextMessage: ");
  Serial.print(number);
  Serial.println(message);
  gprs.println("AT + CMGS = \"" + number + "\"");
  delay(100);
  gprs.println(message);
  delay(100);
  gprs.println((char)26); // ASCII code of the ctrl+z is 26
  delay(100);
  gprs.println();
}

void gprs_powerUpOrDown() {
  pinMode(pinGprsPower, OUTPUT); 
  digitalWrite(pinGprsPower,LOW);
  delay(1000);
  digitalWrite(pinGprsPower,HIGH);
  delay(2000);
  digitalWrite(pinGprsPower,LOW);
  delay(3000);
}

boolean checkIncomingTextMessage() {
  // start with +CMGR: , assume output if always '\r\n+CMGR: '
  if (incomingMessage[2] != '+' && incomingMessage[3] != 'C' && incomingMessage[4] != 'M' && incomingMessage[5] != 'G' && incomingMessage[6] != 'R' && incomingMessage[7] != ':' && incomingMessage[8] != ' ') return false;

  // 8 "'s before newline
  int charCounter=0;
  for (int i = 2; i < strlen(incomingMessage) - 1; i++) {
    if (charCounter < 8 && incomingMessage[i] == '\r' && incomingMessage[i+1] == '\n') return false;
    if (incomingMessage[i] == '"') charCounter++;
  }
  if (charCounter != 8) return false;

  // 4 ,'s before newline
  charCounter = 0;
  for (int i = 2; i < strlen(incomingMessage) - 1; i++) {
    if (charCounter < 4 && incomingMessage[i] == '\r' && incomingMessage[i+1] == '\n') return false;
    if (incomingMessage[i] == ',') charCounter++;
  }
  if (charCounter != 4) return false;

  // at least 2 newlines (one right at beginning and one between header and content of sms
  charCounter = 0;
  for (int i = 0; i < strlen(incomingMessage) - 1; i++) {
    if (incomingMessage[i] == '\r' && incomingMessage[i+1] == '\n') charCounter++;
  }
  if (charCounter < 2) return false;

  return true;
}

// *******************************************************
// ethernet shield specific code

void ether_setup() {
  Serial.println("ether_setup");
  Ethernet.begin(mac, ip);
  server.begin();
}


