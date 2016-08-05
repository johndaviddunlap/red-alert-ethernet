#include <SPI.h>
#include <Ethernet.h>
#include <SD.h>

// 1 higher than 100(the desired max) because we need room for the terminating null byte
#define COMMAND_BUFFER_SIZE 101
#define DEBUG 0
    
// These will store the server's network settings, which will be loaded from the SD card
byte serverMacAddress[] = { 0xDE, 0xAD, 0xBE, 0xEF, 0xFE, 0xED };
IPAddress serverIP(192,168,1,177);
int serverPort = 23;

// Create a pointer for referencing the server object once it has been initialized
EthernetServer server(serverPort);

// 0 == red alert off, 1 == red alert on
int redAlert = 0;

// The pin which controls the lights for red alert
int alarmPin = 7;
int chipSelectPin = 4;

void setup() {
  // Open serial communications and wait for port to open:
  Serial.begin(9600);
  
  // wait for serial port to connect. Needed for Leonardo only
  while (!Serial);
  
  pinMode(chipSelectPin, OUTPUT);

  if (!SD.begin(chipSelectPin)) {
      Serial.println(F("INFO: ERROR - SD card initialization failed!"));
      return;
  }
  Serial.println(F("INFO: SD card initialized"));

  // start the Ethernet connection and the server:
  Ethernet.begin(serverMacAddress, serverIP);
  server.begin();

  Serial.println(F("INFO: Red Alert - Ethernet controller"));
  
  Serial.print(F("INFO: Server listening at address "));
  Serial.print(Ethernet.localIP());
  Serial.print(F(" on port "));
  Serial.println(serverPort);
  
  // This is the pin used to signal an alarm state
  pinMode(alarmPin, OUTPUT);

  if (DEBUG) {
    Serial.println(F("DEBUG: Debug mode enabled"));
  }
}

void loop() {
  // listen for incoming clients
  EthernetClient client = server.available();

  if (client) {
    char clientIP[16];
    getRemoteIPAsString(client, clientIP);

    if (!checkACL(client)) {
      client.println(F("ACCESS DENIED"));
      Serial.print(F("INFO: Access denied for client "));
      Serial.println(clientIP);

      disconnectClient(client);
    } else {
      client.println(F("ACCESS GRANTED"));
      Serial.print(F("INFO: Access granted for client "));
      Serial.println(clientIP);
      
      boolean welcomeDisplayed = false;
      boolean netcat = false;
      
      // Continue looping while the client is connected
      while (client.connected()) {
        // Create a buffer for storing a command from the client
        char commandBuffer[COMMAND_BUFFER_SIZE] = "";
  
        // Attempt to read a command from the client
        boolean hasCommand = readCommand(commandBuffer, COMMAND_BUFFER_SIZE, client);
        
        // Don't display the welcome message if a command was sent through netcat
        if (welcomeDisplayed == false && hasCommand == true) {
          netcat = true;
          welcomeDisplayed = true;
        }
  
        if (DEBUG && welcomeDisplayed == false) {
          Serial.print(F("DEBUG: hasCommand="));
          Serial.println(hasCommand);
          Serial.print(F("DEBUG: This is a netcat command: "));
          Serial.println(netcat);
          Serial.print(F("DEBUG: command: "));
          Serial.println(commandBuffer);
        }
        
        // Display the welcome message when connecting through telnet but not
        // when sending a command through netcat
        if (welcomeDisplayed == false && netcat == false) {
          executeHelpCommand(client);
          welcomeDisplayed = true;
        }
        
        // Otherwise, block while waiting for a command
        if (hasCommand) {
          // Execute command
          executeCommand(commandBuffer, client);
        }
        
        // Automatically disconnect the client, if this is a netcat command
        if (netcat == true) {
          disconnectClient(client);
        }
      }
    }
  }
}

boolean readCommand(char* commandBuffer, int commandBufferSize, EthernetClient client) {   
  // This flag prevents spaces being prepended to commands
  boolean nonSpaceEntered = false;

  // Buffer for storing commands from the client
  int bufferIndex = 0;

  while (client.available()) {
    char c = client.read();
    
    if (DEBUG) {
      Serial.print(F("DEBUG: Read character   (int="));
      Serial.print((int)c);
      Serial.print(F(", char="));
      Serial.print(c);
      Serial.println(F(") from client"));
    }

    // Attempt to execute the command if a newline has been received
    if (c == '\n') {
      break;
    } else if (c == '\r') {
      break;
    }

    if (bufferIndex == commandBufferSize - 1) {
      client.print(F("Server forced disconnect: Command longer than "));
      client.print(commandBufferSize - 1);
      client.println(F(" characters"));
      Serial.print(F("INFO: Server forced disconnect: Command longer than "));
      Serial.print(commandBufferSize - 1);
      Serial.println(F(" characters"));

      disconnectClient(client);
      commandBuffer[0] = 0;
      bufferIndex = 0;
      return false;
    } else if((c >= 97 && c <= 122) || (c >= 49 && c <= 57) || (c == 32 && nonSpaceEntered == true) || c == 46 || c == 92 || c == 63) {
      // These are useful for debugging
      if (DEBUG) {
        Serial.print(F("DEBUG: Adding character (int="));
        Serial.print((int)c);
        Serial.print(F(", char="));
        Serial.print(c);
        Serial.println(F(") to command buffer"));
      }

      // Now that we've seen a non-white-space character, we
      // will start accepting them as part of a command
      if (c != 32) {
        nonSpaceEntered = true;
      }

      // Add the character to the buffer
      commandBuffer[bufferIndex] = c;
      bufferIndex++;
      commandBuffer[bufferIndex] = 0;
    }        
  }
  
  // Don't report that a command was received from the client, if the command buffer is empty
  if (strlen(commandBuffer) > 0) {
    return true;
  } else {
    return false;
  }
}

void executeHelpCommand(EthernetClient client) {
  client.println(F("quit|exit|\\q    : disconnects from the server"));
  client.println(F("red alert        : toggles red alert on/off"));
  client.println(F("red alert on     : turns on red alert"));
  client.println(F("red alert off    : turns off red alert"));
  client.println(F("red alert status : is red alert on or off?"));
  client.println(F("help|?           : prints this message"));
}

void executeCommand(char* command, EthernetClient client) {
  Serial.print(F("INFO: Executing command: "));
  Serial.println(command);
  
  // Supported all three ways of exiting programs that I commonly use
  if (strcmp(command, "exit") == 0 || strcmp(command, "quit") == 0 || strcmp(command, "\\q") == 0) {
    disconnectClient(client);
  } else if (strcmp(command, "red alert") == 0) {
    if (redAlert == 0) {
      client.println(F("red alert on"));
      Serial.println(F("INFO: red alert on"));
      redAlert = 1;
      digitalWrite(alarmPin, HIGH);  
    } else {
      client.println(F("red alert off"));
      Serial.println(F("INFO: red alert off"));
      redAlert = 0;
      digitalWrite(alarmPin, LOW);  
    }
  } else if (strcmp(command, "red alert on") == 0) {
      redAlert = 1;
      digitalWrite(alarmPin, HIGH);  
      Serial.println(F("INFO: red alert on"));
  } else if (strcmp(command, "red alert off") == 0) {
      redAlert = 0;
      digitalWrite(alarmPin, LOW);  
      Serial.println(F("INFO: red alert off"));
  } else if (strcmp(command, "red alert status") == 0) {
    client.print(F("red alert "));
    
    if (redAlert) {
      client.println(F("on"));
    } else {
      client.println(F("off"));
    }
  } else if (strcmp(command, "help") == 0 || strcmp(command, "?") == 0) {
    executeHelpCommand(client);
  } else {
    client.print(command);
    client.println(F(": command not found"));

    // This is useful for debugging
    if (DEBUG) {
      client.print(F("Command length: "));
      client.println(strlen(command));
    }

    client.println(F("type \"help\" to list available commands"));
  }
}

void disconnectClient(EthernetClient client) {
  // close the connection:
  client.println(F("Client disconnected"));
  client.stop();
}

char* getRemoteIPAsString(EthernetClient client, char* strRepresentation) {
  return getRemoteIPAsString(client, strRepresentation, '.');
}

char* getRemoteIPAsString(EthernetClient client, char* strRepresentation, char separator) {
  unsigned char rawRemoteIP[4];
  unsigned char octet1, octet2, octet3, octet4;
  
  // Make sure that the provided char array was null terminated, just in case
  strRepresentation[0] = 0;
  
  client.getRemoteIP(rawRemoteIP);
  
  octet1 = (unsigned char) rawRemoteIP[0];
  octet2 = (unsigned char) rawRemoteIP[1];
  octet3 = (unsigned char) rawRemoteIP[2];
  octet4 = (unsigned char) rawRemoteIP[3];
  
  sprintf(
    strRepresentation, 
    "%i%c%i%c%i%c%i", 
    octet1,
    separator,
    octet2, 
    separator,
    octet3, 
    separator,
    octet4,
    separator
  );
  
  return strRepresentation;
} 

boolean checkACL(EthernetClient client) {
  char aclPath[32];
  char clientIP[16];

  // Get the client's IP address
  getRemoteIPAsString(client, clientIP, '/');

  // Initialize an empty string
  aclPath[0] = 0;
  
  // Construct the access control list
  strcat(aclPath, "acl/white/");
  strcat(aclPath, clientIP);

  if (DEBUG) {
    Serial.print(F("DEBUG: ACL PATH: "));
    Serial.println(aclPath);
  }

  // The client should be gratned access only if the fire/directory exists
  if (SD.exists(aclPath)) {
    if (DEBUG) {
      Serial.println(F("ACL PASS"));
    }
    
    return true;
  }
  
  if (DEBUG) {
    Serial.println(F("ACL FAIL"));
  }

  return false;
}


void readIPAddressFromFile(char* filename, byte* ipAddress) {
  int bufferSize = 16;
  char buffer[bufferSize];
  
  // Precaution in case we foolishly try to use it without initializing it
  buffer[0] = 0;
  
  // Load the string address
  readFromFile(filename, buffer, bufferSize);
  
  // Tokenize the address into individual octets and convert them to bytes
  Serial.print(F("LOADED IP "));
  Serial.println(buffer);
}

void readFromFile(char* filename, char* buffer, int bufferSize) {
  int bufferPosition = 0;
  int count = 0;
  return;
  // open the file. note that only one file can be open at a time,
  // so you have to close this one before opening another.
  File dataFile = SD.open(filename);

  // if the file is available, write to it:
  if (dataFile) {
    while (dataFile.available() && count < 32) {
      break;
      char c = dataFile.read();
      
      if (DEBUG) {
        Serial.print(F("DEBUG: Read charcter "));
        Serial.print(F("(int="));
        Serial.print((int)c);
        Serial.print(F(", char="));
        Serial.print(c);
        Serial.print(F(") from file "));
        Serial.println(filename);
      }

      // Abort reading from the file, if we have exceeded the size of the buffer
      if (bufferPosition >= bufferSize) {
        Serial.print(F("INFO: Aborting read from file "));
        Serial.print(filename);
        Serial.print(F(" because its contents exceeded the maximum buffer size of "));
        Serial.println(bufferSize);
        break;
      }
      
      buffer[bufferPosition] = c;
      bufferPosition++;
      
      // Add the terminating null byte
      buffer[bufferPosition] = 0;
      
      count++;
    }
    dataFile.close();
  } 
}
