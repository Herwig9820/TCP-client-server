/*
    Test Arduino Nano 33 IoT as TCP client

    Version:    v1.00 - 3/4/2021
    Author:     Herwig Taveirne

    Purpose: test connection and message transmission via WiFi between two Arduino nano 33 IoT boards
    One board as TCP server, one as TCP client
    This program handles the CLIENT side, you will need sketch 'test TCP server.ino' to handle the SERVER side


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
    conn_2_wifiConnected,                                               // attemp to connect to server 
    conn_3_clientDelayConnection,                                       // waiting for next attempt to connect to server (after timeout)
    conn_4_clientConnected,                                             // client connected to server (may send request to server)
    conn_5_requestSent,                                                 // client request sent to server: wait for server response
    conn_6_stopClientNow,                                               // stop connection to server
    conn_7_report                                                       // send info about last connection cycle to serial monitor
};

char ssid [] = SECRET_SSID;                                             // network SSID
char pass [] = SECRET_PASS;                                             // network password

const IPAddress serverAddress( 192, 168, 2, 5 );                        // server IP to connect to
const int serverPort = 4080;                                            // server port 

const unsigned long wifiConnectDelay { 5000 };                          // minimum delay between two attempts to connect to wifi
const unsigned long clientConnectDelay { 100 };                         // minimum delay between 2 connection attempts or stopping client and nect connection attempt
const unsigned long readingTimeOut { 10000 };                           // timeout while reading from server
const unsigned long heartbeatPeriod { 1000 };                           // time between two heartbeats

unsigned long messageCounter { 1230001 };                               // incrementing value to be sent as client request to server
unsigned long errors { 0 };                                             // error counter
unsigned long startReadingAt { 0 }, lastHeartbeat { 0 };                // timestamps in milliseconds
unsigned long wifiConnectTime { 0 }, clientStopTime { 0 };              // timestamps in milliseconds
unsigned long wifiConnections { 0 }, clientConnections { 0 };           // counters

int connectionState { conn_0_wifiConnectNow };                          // controls execution

char s200 [ 200 ] { "" };                                               // general purpose 
char serverResponse [ 20 ] { "" };                                      // received server response

WiFiClient client;


// forward declarations

void connectToWiFi();                                                   // if currently not connected to wifi (or connection lost): connect
void connectToServer();                                                 // if server is listening, connect
void sendRequestToServer();                                             // send client request to server 
void assembleServerResponse();                                          // read one incoming character of server response, if available 
void stopTCPconnection();                                               // stop connection to server
void lastConnectionReport();                                            // send info about last connection cycle to serial monitor
void heartbeat();                                                       // 1 second heartbeat: print current connection state

void reportWifiStatus();                                                // report WiFi status to serial monitor
void changeConnectionState( int newState );                             // change connection stateand report to serial monitor



void setup() {
    Serial.begin( 1000000 );
    delay( 2000 );                                                      // while(!Serial) {} does not work
    Serial.println( "Starting client side..." );
    pinMode( 11, OUTPUT );                                              // flag execution heartbeat procedure 
    pinMode( 12, OUTPUT );                                              // execution flow
    pinMode( 13, OUTPUT );                                              // flag connected client (on board led)
}


void loop() {
    // code contains no calls to 'delay()', all delays and timeouts controlled by timer

    // variable 'connectionState' controls proper sequencing of tasks in these procedures:
    connectToWiFi();                                                    // if currently not connected to wifi (or connection lost): connect
    connectToServer();                                                  // if server is listening, connect
    sendRequestToServer();                                              // send client request to server 
    assembleServerResponse();                                           // read one incoming character of server response, if available 
    stopTCPconnection();                                                // stop connection to server
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
    WiFi.disconnect();
    WiFi.end();
    if ( WiFi.begin( ssid, pass ) == WL_CONNECTED ) {                   // success

        Serial.print( "\nConnected to wifi at " );
        Serial.println( (float) millis() / 1000. );

        reportWifiStatus();

        changeConnectionState( conn_2_wifiConnected );                  // may try to connect client again
    }
    else {
        Serial.print( "..." );                                          // waiting for next wifi connection attempt
        changeConnectionState( conn_1_wifiDelayConnection );            // init (in case connection fails, try again after delay)
    }

    // always provide delay between two connection attempts 
    wifiConnectTime = millis();                                         // connection (attempt) time: record AFTER WiFi.connect()
}



// *** if server is listening: connect ***

void connectToServer() {
    if ( connectionState < conn_2_wifiConnected ) { return; }           // no wifi yet: nothing to do

    switch ( connectionState ) {                                        // state
    case conn_2_wifiConnected:                                          // no client connection yet
        break;                                                          // proceed (attempt connection with server)

    case conn_3_clientDelayConnection:                                  // connection was stopped: waiting for next connection attempt 
        if ( clientStopTime + clientConnectDelay > millis() ) {         // delay after client.stop() or previous connection attempt reached ?
            return;                                                     // no: do not try to connect again yet
        }

        changeConnectionState( conn_2_wifiConnected );                  // yes: may try to connect client again

        break;                                                          // ... and attempt connection with server again

    default:                                                            // client was already connected
        if ( !client.connected() ) {                                    // client still connected ?
            errors++;                                                   // server is not supposed to stop connection

            Serial.print( "**** Error: client connection was lost at " );
            Serial.println( (float) millis() / 1000. );

            changeConnectionState( conn_6_stopClientNow );              // connection lost or client side disconnected: stop connection
        }
        return;
    }

    // try connecting with server now
    Serial.print( "Connecting client... at " );
    Serial.println( (float) millis() / 1000. );

    if ( client.connect( serverAddress, serverPort ) ) {
        Serial.print( "connected to server at " );
        Serial.println( (float) millis() / 1000. );

        digitalWrite( 13, HIGH );                                       // flag 'client connected to server'
        clientConnections++;                                            // total

        changeConnectionState( conn_4_clientConnected );                // success: wait for client request
    }
    else {
        errors++;                                                       // server is supposed to be listening 

        clientStopTime = millis();                                      // here: time of last attempt to connect   
        Serial.print( "not connected yet at " );
        Serial.println( (float) millis() / 1000. );

        changeConnectionState( conn_3_clientDelayConnection );          // try again after a delay
    }
}



// *** send client request to server ***

void sendRequestToServer() {
    if ( connectionState != conn_4_clientConnected ) { return; }        // send request now ?

    int n = client.println( messageCounter );
    if ( n != 9 ) { Serial.print( "===================================" ); Serial.println( n ); }

    sprintf( s200, "request sent: %ld at ", messageCounter );
    Serial.print( s200 );
    Serial.println( (float) millis() / 1000. );

    strcpy( serverResponse, "" );                                       // init 
    startReadingAt = millis();                                          // timestamp: start waiting for server response

    changeConnectionState( conn_5_requestSent );                        // wait for server response
}



// *** read one incoming character of server response, if available ***

void assembleServerResponse() {
    if ( connectionState != conn_5_requestSent ) { return; }            // currently expecting data ?

    char c [ 2 ] = "";

    if ( client.available() ) {                                         // characters available: read one
        Serial.print( "[" );                                            // send character to serial monitor, surrounded by []
        c [ 0 ] = client.read();
        if ( !((c [ 0 ] == '\r') || (c [ 0 ] == '\n')) ) {
            strcat( serverResponse, c );
            Serial.print( c );
        }
        else {
            Serial.print( (c [ 0 ] == '\r') ? "(CR)" : ((c [ 0 ] == '\n') ? "(LF)" : "(??)") );
        }
        Serial.print( "]" );

        if ( c [ 0 ] == '\n' ) {                                        // server response has been read completely now

            strcpy( s200, "response read: " );
            strcat( s200, serverResponse );
            strcat( s200, " at " );
            Serial.print( s200 );
            Serial.println( (float) millis() / 1000. );

            messageCounter++;

            changeConnectionState( conn_6_stopClientNow );              // stop connection
        }
    }

    else if ( startReadingAt + readingTimeOut < millis() ) {            // no character available and time out reached
        errors++;

        Serial.print( "\n**** Error: timeout while reading response at " );
        Serial.println( (float) millis() / 1000. );

        changeConnectionState( conn_6_stopClientNow );                  // stop connectioni
    }
}



// *** stop connection to server ***

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
    if ( lastHeartbeat + heartbeatPeriod < millis() ) {                 // heartbeat ?
        if ( (connectionState <= conn_2_wifiConnected) ||
            (connectionState == conn_4_clientConnected) ) {
            Serial.println();
        }
        sprintf( s200, "******** Heartbeat - connection state is S%d at ",
            connectionState );
        Serial.print( s200 );                                           // print current connection state
        Serial.println( (float) millis() / 1000. );
        lastHeartbeat = millis();
    }
}



// *** print WiFi status to serial monitor ***

void reportWifiStatus() {
    IPAddress ip = WiFi.localIP();
    Serial.print( "IP Address: " );
    Serial.println( ip );

    long rssi = WiFi.RSSI();
    Serial.print( "signal strength (RSSI):" );
    Serial.print( rssi );
    Serial.println( " dBm\n" );
}



// *** change connection state and report to serial monitor

void changeConnectionState( int newState ) {
    connectionState = newState;                                         // wait for next client request
    sprintf( s200, "(S%d)", connectionState );
    Serial.print( s200 );
}