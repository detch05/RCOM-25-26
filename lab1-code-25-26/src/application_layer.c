// Application layer protocol implementation

#include "application_layer.h"
#include "link_layer.h"

#include <stdio.h>

void applicationLayer(const char *serialPort, const char *role, int baudRate,
                      int nTries, int timeout, const char *filename)
{
    LinkLayer ll;
    ll.baudRate = baudRate; ll.nRetransmissions = nTries; ll.role = role; ll.serialPort = serialPort; ll.timeout = timeout;
    llopen(ll);
    if (role == "tx") {
        llwrite()
    }
    else if (role == "rx") {
        llread()
    }
    llclose();
}
