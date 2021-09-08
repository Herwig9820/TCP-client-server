# TCP-client-server
A pair of programs to test two Arduino Nano 33 IoT boards, one acting as TCP client and the other one as TCP server

Both programs function as a state machine, with a variable 'connectionState' controlling execution.
WiFi connection and client connection state is constantly checked. There are no while loops, no calls to 'delay()' etc. All timeouts controlled by timer.

## Server-side states

```
enum ConnectionState_type {
    conn_0_wifiConnectNow,                          // attempt to connect wifi
    conn_1_wifiDelayConnection,                     // waiting for next attempt to connect wifi (after timeout)
    conn_2_wifiConnected,                           // attempt to connect to client
    conn_3_clientDelayConnection,                   // wait for next attempt to connect to client (after timeout)
    conn_4_clientConnected,                         // server connected to client (waiting for client request)
    conn_5_requestReceived,                         // client request received: send server response
    conn_6_stopClientNow,                           // stop client connection
    conn_7_report                                   // send info about last connection cycle to serial monitor
};
```

## Client-side states
```
enum ConnectionState_type {
    conn_0_wifiConnectNow,                          // attempt to connect wifi
    conn_1_wifiDelayConnection,                     // waiting for next attempt to connect wifi (after timeout)
    conn_2_wifiConnected,                           // attempt to connect to server
    conn_3_clientDelayConnection,                   // waiting for next attempt to connect to server (after timeout)
    conn_4_clientConnected,                         // client connected to server (may send request to server)
    conn_5_requestSent,                             // client request sent to server: wait for server response
    conn_6_stopClientNow,                           // stop connection to server
    conn_7_report                                   // send info about last connection cycle to serial monitor
};
```

## Server-side main loop (client side is similar)
```
void loop() {
    // code contains no calls to 'delay()', all delays and timeouts controlled by timer

    // variable 'connectionState' controls proper sequencing of tasks in these procedures:
    connectToWiFi();                 // if currently not connected to wifi (or connection lost): connect
    connectToClient();               // if a client is available but not connected: connect
    assembleClientRequest();         // read one incoming character of client request, if available
    sendResponseToClient();          // when client request is complete, send response to client
    stopTCPconnection();             // stop connection to client (when client side disconnected or after time out)
    lastConnectionReport();          // send info about last connection cycle to serial monitor

    // not controlled by state but by time
    heartbeat();                     // 1 second heartbeat: print current connection state
}
```

Both server and client print status information to the serial port (see excel workbook, attached).
In addition, the server-side code outputs signals on pins 10 to 12 that are displayed on an oscilloscope, enabling to see what is actually happening and when.

## Normal flow

client connects
server notices client and connects
client sends a 'client request' (a 7-digit number) followed by CRLF
server reads request and echoes it as a 'server response' to the client
client stops
server detects client stopped and stops as well

## Time outs

server and client time out if data is not received within 10 seconds
these time outs can be modified (defined as constants)

## Delays

if a WiFi connection attempt fails, next attempt will happen after a delay (using the state machine logic)
server side: after each client.stop(), a small delay is applied before next connection attempt (using the state machine logic)
client side: a small delay is applied between two connection attempts OR between a client.stop() and a next connection attempt
delay timings can be modified (defned as constants)
Client connection delay is quite short, but increasing it does not solve the issues.

## Note

In relation to these tests, I filed a bug report "Nano 33 IoT server hangs OR stops making connection with client"
(https://github.com/arduino/Arduino/issues/11421)



