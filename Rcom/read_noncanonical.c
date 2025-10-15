// Example of how to read from the serial port in non-canonical mode
//
// Modified by: Eduardo Nuno Almeida [enalmeida@fe.up.pt]

#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <termios.h>
#include <unistd.h>

#define _POSIX_SOURCE 1 // POSIX compliant source

#define FALSE 0
#define TRUE 1

#define BAUDRATE 38400
#define BUF_SIZE 256

#define FLAG 0x7E
#define A_SET 0x03
#define C_SET 0x03
#define BCC (A_SET ^ C_SET)


// STATES
typedef enum
{
    START,
    FLAG_RCV,
    A_RCV,
    C_RCV,
    BCC_OK,
    STOP
} State;

// STATE MACHINE
typedef struct
{
    State state;
    unsigned char address;
    unsigned char control;
    unsigned char bcc;
} SM;

// ---------------------------------------------------

int fd = -1;           // File descriptor for open serial port
struct termios oldtio; // Serial port settings to restore on closing

int openSerialPort(const char *serialPort, int baudRate);
int closeSerialPort();
int readByteSerialPort(unsigned char *byte);
int writeBytesSerialPort(const unsigned char *bytes, int nBytes);
void sm_process(SM *sm, unsigned char byte);
void send_UA();

void sm_init(SM *sm)
{
    sm->state = START;
    sm->address = 0;
    sm->control = 0;
    sm->bcc = 0;
}

// ---------------------------------------------------
// MAIN
// ---------------------------------------------------
int main(int argc, char *argv[])
{
    if (argc < 2)
    {
        printf("Incorrect program usage\n"
               "Usage: %s <SerialPort>\n"
               "Example: %s /dev/ttyS0\n",
               argv[0],
               argv[0]);
        exit(1);
    }

    // Open serial port device for reading and writing, and not as controlling tty
    // because we don't want to get killed if linenoise sends CTRL-C.
    //
    // NOTE: See the implementation of the serial port library in "serial_port/".
    const char *serialPort = argv[1];

    if (openSerialPort(serialPort, BAUDRATE) < 0)
    {
        perror("openSerialPort");
        exit(-1);
    }

    printf("Serial port %s opened\n", serialPort);

    // Read from serial port until the 'z' char is received.

    // NOTE: This while() cycle is a simple example showing how to read from the serial port.
    // It must be changed in order to respect the specifications of the protocol indicated in the Lab guide.

    // TODO: Save the received bytes in a buffer array and print it at the end of the program.

    unsigned char byte;
    int countBytes = 0;

    SM sm;
    sm_init(&sm);

    while (TRUE)
    {
        readByteSerialPort(&byte);
        printf("Byte received: 0x%02X\n PROCESSING...\n", byte);
        sm_process(&sm, byte);
        countBytes++;

        if (sm.state == STOP) {
            printf("SET frame successfully received!\nSending UA\n");
            send_UA();
            break;
        }

        if (countBytes > 20) {
            printf("Too many bytes received without success, exiting\nI've had enough!\n");
            break;
        }
    }

    printf("Total bytes received: %d\n", countBytes);

    // Close serial port
    if (closeSerialPort() < 0)
    {
        perror("closeSerialPort");
        exit(-1);
    }

    printf("Serial port %s closed\n", serialPort);

    return 0;
}

// ---------------------------------------------------
// SERIAL PORT LIBRARY IMPLEMENTATION
// ---------------------------------------------------

// Open and configure the serial port.
// Returns -1 on error.
int openSerialPort(const char *serialPort, int baudRate)
{
    // Open with O_NONBLOCK to avoid hanging when CLOCAL
    // is not yet set on the serial port (changed later)
    int oflags = O_RDWR | O_NOCTTY | O_NONBLOCK;
    fd = open(serialPort, oflags);
    if (fd < 0)
    {
        perror(serialPort);
        return -1;
    }

    // Save current port settings
    if (tcgetattr(fd, &oldtio) == -1)
    {
        perror("tcgetattr");
        return -1;
    }


    // Convert baud rate to appropriate flag

    // Baudrate settings are defined in <asm/termbits.h>, which is included by <termios.h>
#define CASE_BAUDRATE(baudrate) \
    case baudrate:              \
        br = B##baudrate;       \
        break;

    tcflag_t br;
    switch (baudRate)
    {
        CASE_BAUDRATE(1200);
        CASE_BAUDRATE(1800);
        CASE_BAUDRATE(2400);
        CASE_BAUDRATE(4800);
        CASE_BAUDRATE(9600);
        CASE_BAUDRATE(19200);
        CASE_BAUDRATE(38400);
        CASE_BAUDRATE(57600);
        CASE_BAUDRATE(115200);
    default:
        fprintf(stderr, "Unsupported baud rate (must be one of 1200, 1800, 2400, 4800, 9600, 19200, 38400, 57600, 115200)\n");
        return -1;
    }
#undef CASE_BAUDRATE

    // New port settings
    struct termios newtio;
    memset(&newtio, 0, sizeof(newtio));

    newtio.c_cflag = br | CS8 | CLOCAL | CREAD;
    newtio.c_iflag = IGNPAR;
    newtio.c_oflag = 0;

    // Set input mode (non-canonical, no echo,...)
    newtio.c_lflag = 0;
    newtio.c_cc[VTIME] = 30; // Block reading
    newtio.c_cc[VMIN] = 5;   // Byte by byte

    tcflush(fd, TCIOFLUSH);

    // Set new port settings
    if (tcsetattr(fd, TCSANOW, &newtio) == -1)
    {
        perror("tcsetattr");
        close(fd);
        return -1;
    }

    // Clear O_NONBLOCK flag to ensure blocking reads
    oflags ^= O_NONBLOCK;
    if (fcntl(fd, F_SETFL, oflags) == -1)
    {
        perror("fcntl");
        close(fd);
        return -1;
    }

    return fd;
}

// Restore original port settings and close the serial port.
// Returns 0 on success and -1 on error.
int closeSerialPort()
{
    // Restore the old port settings
    if (tcsetattr(fd, TCSANOW, &oldtio) == -1)
    {
        perror("tcsetattr");
        return -1;
    }

    return close(fd);
}

// Wait up to 0.1 second (VTIME) for a byte received from the serial port.
// Must check whether a byte was actually received from the return value.
// Save the received byte in the "byte" pointer.
// Returns -1 on error, 0 if no byte was received, 1 if a byte was received.
int readByteSerialPort(unsigned char *byte)
{
    return read(fd, byte, 1);
}

// Write up to numBytes from the "bytes" array to the serial port.
// Must check how many were actually written in the return value.
// Returns -1 on error, otherwise the number of bytes written.
int writeBytesSerialPort(const unsigned char *bytes, int nBytes)
{
    return write(fd, bytes, nBytes);
}

void send_UA()
{
    unsigned char F = 0x7E;
    unsigned char A = 0x03;
    unsigned char C = 0x07;
    unsigned char BCC1 = A ^ C;
    unsigned char buf[BUF_SIZE] = {F, A, C, BCC1, F};
    writeBytesSerialPort(buf, 5);
}

void sm_process(SM *sm, unsigned char byte) {
        switch (sm->state) {
        case START:
            if (byte == FLAG)
                sm->state = FLAG_RCV;
            break;

        case FLAG_RCV:
            if (byte == FLAG)
                sm->state = FLAG_RCV;   // stay in FLAG_RCV
            else if (byte == A_SET) {
                sm->address = byte;
                sm->state = A_RCV;
            } else
                sm->state = START;
            break;

        case A_RCV:
            if (byte == FLAG)
                sm->state = FLAG_RCV;
            else if (byte == C_SET) {
                sm->control = byte;
                sm->state = C_RCV;
            } else
                sm->state = START;
            break;

        case C_RCV:
            if (byte == FLAG)
                sm->state = FLAG_RCV;
            else if (byte == (sm->address ^ sm->control)) {
                sm->bcc = byte;
                sm->state = BCC_OK;
            } else
                sm->state = START;
            break;

        case BCC_OK:
            if (byte == FLAG)
                sm->state = STOP;
            else
                sm->state = START;
            break;

        case STOP:
            break;
    }
}