/*
    Test Arduino Nano 33 IoT as TCP server

    Version:    v1.00 - 3/4/2021
    Author:     Herwig Taveirne

    Purpose: test connection and message transmission via WiFi between two Arduino nano 33 IoT boards
    One board as TCP server, one as TCP client
    This program handles the SERVER side, you will need sketch 'test TCP server.ino' to handle the CLIENT side


    This program is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program.  If not, see <http://www.gnu.org/licenses/>.
*/

#include <WiFiNINA.h>
#include "secrets.h"


enum ConnectionState_type {
    conn_0_wifiConnectNow,                                              // attempt to connect wifi
    conn_1_wifiDelayConnection,                                         // waiting for next attempt to connect wifi (after timeout) 
    conn_2_wifiConnected,                                               // attemp to connect to client 
    conn_3_clientDelayConnection,                                       // wait for next attempt to connect to client (after timeout)
    conn_4_clientConnected,                                             // server connected to client (waiting for client request)
    conn_5_requestReceived,                                             // client request received: send server response
    conn_6_stopClientNow,                                               // stop client connection
    conn_7_report                                                       // send info about last connection cycle to serial monitor
};

char ssid [] = SECRET_SSID;                                             // network SSID
char pass [] = SECRET_PASS;                                             // network password

const IPAddress serverAddress( 192, 168, 2, 5 );                        // static server IP 
const int serverPort = 4080;                                            // server port 

const unsigned long wifiConnectDelay { 5000 };                          // minimum delay between two attempts to connect to wifi (milliseconds)
const unsigned long clientConnectDelay { 100 };                         // minimum delay between stopping and connecting client
const unsigned long readingTimeOut { 10000 };                           // timeout while reading from client
const unsigned long heartbeatPeriod { 1000 };                           // time between two heartbeats
const unsigned long reportingTimeout { 120000 };                        // stop reporting to serial monitor after timeout when connection lost 

unsigned long errors { 0 };                                             // error counter
unsigned long startReadingAt { 0 }, lastHeartbeat { 0 };                // timestamps in milliseconds
unsigned long wifiConnectTime { 0 }, clientStopTime { 0 };              // timestamps in milliseconds
unsigned long wifiConnections { 0 }, clientConnections { 0 };           // counters
unsigned long ConnTriesAfterStateChange { 0 };                          // limits output to Serial monitor (prevent flooding)
unsigned long reportingStartTime { 0 };                                 // start time for reporting heartbeat and connection attempts to serial monitor 

bool reportToSerialMonitor { true };                                    // controls reporting to serial monitor (heartbeat and connection attempts only)
int connectionState { conn_0_wifiConnectNow };                          // controls execution

char s200 [ 200 ] { "" };                                               // general purpose 
char clientRequest [ 20 ] { "" };                                       // received client request

WiFiClient client;
WiFiServer server( serverPort );


// forward declarations

void connectToWiFi();                                                   // if currently not connected to wifi (or connection lost): connect
void connectToClient();                                                 // if a client is available but not connected: connect
void assembleClientRequest();                                           // read one incoming character of client request, if available 
void sendResponseToClient();                                            // when client request is complete, send response to client
void stopTCPconnection();                                               // stop connection to client (when client side disconnected or after time out)
void lastConnectionReport();                                            // send info about last connection cycle to serial monitor
void heartbeat();                                                       // 1 second heartbeat: print current connection state

void reportWifiStatus();                                                // report WiFi status to serial monitor
void execFlowPulses( byte pulses );                                     // provide cues (using an oscilloscope) indicating which procedure is next to be executed
void changeConnectionState( int newState );                             // change connection stateand report to serial monitor



void setup() {
    Serial.begin( 1000000 );
    delay( 2000 );                                                      // while(!Serial) {} does not seem to work
    Serial.println( "Starting server side..." );
    pinMode( 10, OUTPUT );
    pinMode( 11, OUTPUT );                                              // flag execution heartbeat procedure 
    pinMode( 12, OUTPUT );                                              // execution flow
    pinMode( 13, OUTPUT );                                              // flag connected client (on board led)
}



void loop() {
    // code contains no calls to 'delay()', all delays and timeouts controlled by timer

    // variable 'connectionState' controls proper sequencing of tasks in these procedures:
    connectToWiFi();                                                    // if currently not connected to wifi (or connection lost): connect
    connectToClient();                                                  // if a client is available but not connected: connect
    assembleClientRequest();                                            // read one incoming character of client request, if available 
    sendResponseToClient();                                             // when client request is complete, send response to client
    stopTCPconnection();                                                // stop connection to client (when client side disconnected or after time out)
    lastConnectionReport();                                             // send info about last connection cycle to serial monitor

    // not controlled by state but by time
    heartbeat();                                                        // 1 second heartbeat: print current connection state
}



// *** if not yet connected to wifi (or connection was lost): connect to wifi ***

void connectToWiFi() {
    switch ( connectionState ) {                                        // state
    case conn_0_wifiConnectNow:                                         // no wifi connection yet
        Serial.print( "Connecting to WiFi, SSID = " );
        Serial.println( SECRET_SSID );
        wifiConnections++;                                              // total (retries do not count)
        break;                                                          // proceed (connect wifi)

    case conn_1_wifiDelayConnection:                                    // currently waiting for next wifi connection attempt
        if ( wifiConnectTime + wifiConnectDelay > millis() ) {          // time out reached ?
            return;                                                     // no: do not try to connect again yet
        }
        break;                                                          // yes: attempt wifi connection again

    default:                                                            // wifi was already connected 
        if ( WiFi.status() == WL_CONNECTED ) {                          // wifi still connected ?
            return;                                                     // yes: do nothing
        }
        else if ( wifiConnectTime + wifiConnectDelay > millis() ) {     // wifi connection was lost: time to reconnect ?
            return;                                                     // no: do not try to connect again yet
        }

        Serial.print( "Wifi connection lost. Reconnecting, SSID = " );
        Serial.println( SECRET_SSID );
        wifiConnections++;                                              // total (retries do not count)
        digitalWrite( 13, LOW );                                        // flag 'client not connected'
    }

    // try connecting to wifi now
    Serial.print( "..." );
    reportToSerialMonitor = true;                                       // may report to serial monitor again (even if wifi connection fails)
    reportingStartTime = millis();                                      // re-trigger

    WiFi.disconnect();
    WiFi.end();
    WiFi.config( serverAddress );                                       // this line: comment out if a static server IP is not wanted 
    if ( WiFi.begin( ssid, pass ) == WL_CONNECTED ) {                   // success
        ConnTriesAfterStateChange = 0;                                  // may report client connection attempts to serial monitor again

        Serial.print( "\nConnected to wifi at " );
        Serial.println( (float) millis() / 1000. );

        reportWifiStatus();
        server.begin();                                                 // start server

        changeConnectionState( conn_2_wifiConnected );                  // may try to connect client again
    }
    else {
        Serial.print( "..." );                                          // waiting for next wifi connection attempt

        changeConnectionState( conn_1_wifiDelayConnection );            // init (in case connection fails, try again after delay)
    }

    // always provide delay between two connection attempts 
    wifiConnectTime = millis();                                         // connection (attempt) time: record AFTER WiFi.connect()
}



// *** if a client is available but not yet connected: connect client ***

void connectToClient() {
    if ( connectionState < conn_2_wifiConnected ) { return; }           // no wifi yet ? nothing to do

    switch ( connectionState ) {                                        // state
    case conn_2_wifiConnected:                                          // no client connection yet
        break;                                                          // proceed (connect with client if available)

    case conn_3_clientDelayConnection:                                  // connection was stopped: waiting for next connection attempt 
        if ( clientStopTime + clientConnectDelay > millis() ) {         // delay after client.stop() reached ?
            return;                                                     // no: do not try to connect again yet
        }
        ConnTriesAfterStateChange = 0;                                  // may report connection attempts to serial monitor again

        changeConnectionState( conn_2_wifiConnected );                  // yes: may try to connect client again

        break;                                                          // ... and attempt client connection again

    default:                                                            // client was already connected
        digitalWrite( 10, HIGH );
        execFlowPulses( 1 );
        if ( !client.connected() ) {                                    // client still connected ?
            Serial.print( "Client disconnected (or connection lost) at " );
            Serial.println( (float) millis() / 1000. );

            changeConnectionState( conn_6_stopClientNow );              // connection lost or client side disconnected: stop connection
        }
        digitalWrite( 10, LOW );
        return;                                                         // nothing more to do
    }

    // try connecting client now
    if ( (ConnTriesAfterStateChange <= 20) && reportToSerialMonitor ) {
        Serial.print( "<" );                                            // demonstrate 'processor not hanging'    
    }

    digitalWrite( 10, HIGH );
    execFlowPulses( 3 );
    client = server.available();                                        // attemp to connect to client
    digitalWrite( 10, LOW );
    if ( (ConnTriesAfterStateChange++ <= 20) && reportToSerialMonitor ) {
        Serial.print( (ConnTriesAfterStateChange != 21) ? ">" : "...>" ); // stop reporting 
    }

    digitalWrite( 10, HIGH );
    execFlowPulses( 3 );
    if ( client.connected() ) {
        reportToSerialMonitor = true;                                   // may report to serial monitor again
        reportingStartTime = millis();                                  // re-trigger

        Serial.print( "\nconnected to client at " );
        Serial.println( (float) millis() / 1000. );

        digitalWrite( 13, HIGH );                                       // flag 'client connected'
        clientConnections++;                                            // total connection cycles

        strcpy( clientRequest, "" );                                    // init
        startReadingAt = millis();                                      // timestamp: start waiting for client request 

        changeConnectionState( conn_4_clientConnected );                // success: wait for client request
    }
    digitalWrite( 10, LOW );
}



// *** read one incoming character of client request (if available) ***

void assembleClientRequest() {
    if ( connectionState != conn_4_clientConnected ) { return; }        // currently expecting data ? 

    char c [ 2 ] = "";

    digitalWrite( 10, HIGH );
    execFlowPulses( 2 );

    if ( client.available() ) {                                         // characters available: read one
        Serial.print( "[" );                                            // send character to serial monitor, surrounded by []
        execFlowPulses( 0 );
        c [ 0 ] = client.read();
        strcat( clientRequest, c );
        if ( !((c [ 0 ] == '\r') || (c [ 0 ] == '\n')) ) {
            Serial.print( c );
        }
        else {
            Serial.print( (c [ 0 ] == '\r') ? "(CR)" : ((c [ 0 ] == '\n') ? "(LF)" : "(??)") );
        }
        Serial.print( "]" );

        if ( c [ 0 ] == '\n' ) {                                        // client request has been read completely now
            Serial.print( "\nrequest read at " );
            Serial.println( (float) millis() / 1000. );

            changeConnectionState( conn_5_requestReceived );            // may send server response now
        }
    }

    else if ( startReadingAt + readingTimeOut < millis() ) {            // no character available and time out reached
        errors++;

        Serial.print( "\n**** Error: timeout while reading request at " );
        Serial.println( (float) millis() / 1000. );

        changeConnectionState( conn_6_stopClientNow );                  // stop connection to client
    }
    digitalWrite( 10, LOW );
}



// *** when client request is complete, send server response to client ***

void sendResponseToClient() {
    if ( connectionState != conn_5_requestReceived ) { return; }        // anything to send for the moment ?

    int n = client.print( clientRequest );
    if ( n != 9 ) { Serial.print( "===================================" ); Serial.println( n ); }

    Serial.print( "response sent at " );
    Serial.println( (float) millis() / 1000. );

    strcpy( clientRequest, "" );                                        // init 
    startReadingAt = millis();                                          // timestamp: start waiting for client request

    changeConnectionState( conn_4_clientConnected );                    // wait for next client request
}



// *** stop connection to client (when client side disconnected or after time out) ***

void stopTCPconnection() {
    if ( connectionState != conn_6_stopClientNow ) { return; }          // do it now ?

    Serial.print( "stopping... " );
    client.stop();

    // provide a minimum delay between client stop and re-connect 
    clientStopTime = millis();                                          // record AFTER client.stop()
    digitalWrite( 13, LOW );

    Serial.print( "disconnected from client at " );
    Serial.println( (float) millis() / 1000. );

    changeConnectionState( conn_7_report );                             // may now print out to serial monitor
}



// *** send info about last connection cycle to serial monitor ***

void lastConnectionReport() {
    if ( connectionState != conn_7_report ) { return; }                 // do it now ?

    sprintf( s200, "Connections WiFi: %ld, client: %ld. Errors: %ld at ",
        wifiConnections, clientConnections, errors );
    Serial.print( s200 );
    Serial.println( (float) millis() / 1000. );
    Serial.println( "------------------------------------------------------------\n" );

    // uncomment if testing disconnecting / connecting ...
    // ... to wifi every 128 client connection cycles
    /*if (!( clientConnections & 0x0000007F)) {                         // reset wifi every 128 client connections
        changeConnectionState(conn_0_wifiConnectNow); }                 // reset wifi connection
    else */
    { changeConnectionState( conn_3_clientDelayConnection ); }          // may connect client again after a delay
}



// *** print current connection info at every heartbeat ***

void heartbeat() {
    execFlowPulses( 0 );                                                // flag every passage 

    if ( lastHeartbeat + heartbeatPeriod < millis() ) {                 // heartbeat ?
        ConnTriesAfterStateChange = 0;                                  // yes: may report client connection attempts to serial monitor again 

        if ( reportingStartTime + reportingTimeout > millis() ) {       // report heartbeat to serial monitor ?
            if ( (connectionState <= conn_2_wifiConnected) ||
                (connectionState == conn_4_clientConnected) ) {
                Serial.println();
            }
            sprintf( s200, "******** Heartbeat - connection state is S%d at ",
                connectionState );
            Serial.print( s200 );                                       // print current connection state
            Serial.println( (float) millis() / 1000. );
            lastHeartbeat = millis();
        }
        else if ( reportToSerialMonitor ) {                             // was still reporting
            reportToSerialMonitor = false;                              // stop reporting
            Serial.print( "\n\nNo connection since a while: stopping reporting at " );
            Serial.println( (float) millis() / 1000. );

        }
    }
}



// *** report WiFi status to serial monitor ***

void reportWifiStatus() {
    IPAddress ip = WiFi.localIP();
    Serial.print( "IP Address: " );
    Serial.println( ip );

    long rssi = WiFi.RSSI();
    Serial.print( "signal strength (RSSI):" );
    Serial.print( rssi );
    Serial.println( " dBm\n" );
}



// *** provide cues (using an oscilloscope) indicating which procedure is next to be executed

void execFlowPulses( byte pulses ) {                                    // pin 12: procedure execution cue, pin 11: same, for heartbeat procedure
    int pin { 12 };
    if ( pulses == 0 ) {
        pulses = 1;
        pin = 11;
    }
    unsigned long m = micros();
    while ( m + 20 > micros() ) {}
    for ( int i = 1; i <= pulses; i++ ) {                               // number of pulses indicates which procedure will execute
        digitalWrite( pin, HIGH );
        m = micros();
        while ( m + 20 > micros() ) {}
        digitalWrite( pin, LOW );
        m = micros();
        while ( m + 20 > micros() ) {}
    }
}



// *** change connection state and report to serial monitor

void changeConnectionState( int newState ) {
    connectionState = newState;                                         // wait for next client request
    sprintf( s200, "(S%d)", connectionState );
    Serial.print( s200 );
}