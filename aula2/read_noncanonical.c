// Read from serial port in non-canonical mode
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

// Baudrate settings are defined in <asm/termbits.h>, which is
// included by <termios.h>
#define BAUDRATE B38400
#define _POSIX_SOURCE 1 // POSIX compliant source

#define FALSE 0
#define TRUE 1
#define RECEIVER 0
#define TRANSMITER 1

#define BUF_SIZE 256
#define UA_SIZE 5
#define FLAG 0x7E
#define BCC(n,m) n^m

typedef enum{
    START,
    FLAG_RCV,
    A_RCV,
    C_RCV,
    BCC_OK,
    StateSTOP
} stateMachine;

volatile int STOP = FALSE;
struct termios oldtio;


void determineState(stateMachine *state, char byte){

    unsigned char SET_A = 0x03;
    unsigned char SET_C = 0x03;

    switch(*state){
        case START:
            if( byte == FLAG) *state = FLAG_RCV;
            break;

        case FLAG_RCV:
            if(byte == FLAG) *state = FLAG_RCV;
            else if(byte == SET_A) *state = A_RCV;
            else *state = START;
            break;

        case A_RCV:
            if(byte == FLAG) *state = FLAG_RCV;
            else if(byte == SET_C) *state = C_RCV;
            else *state = START;
            break;

        case C_RCV:
            if(byte == FLAG) *state = FLAG_RCV;
            else if(byte == BCC(SET_A, SET_C)) *state = BCC_OK;
            else *state = START;
            break;

        case BCC_OK:
            if(byte == FLAG)  *state = StateSTOP;
            else *state = START;
            break;
    }

}


int llopen(const char *serialPortName, int transmiter){
    int fd = open(serialPortName, O_RDWR | O_NOCTTY);
    if (fd < 0)
    {
        perror(serialPortName);
        exit(-1);
    }

    struct termios newtio;

    // Save current port settings
    if (tcgetattr(fd, &oldtio) == -1)
    {
        perror("tcgetattr");
        exit(-1);
    }

    // Clear struct for new port settings
    memset(&newtio, 0, sizeof(newtio));

    newtio.c_cflag = BAUDRATE | CS8 | CLOCAL | CREAD;
    newtio.c_iflag = IGNPAR;
    newtio.c_oflag = 0;

    // Set input mode (non-canonical, no echo,...)
    newtio.c_lflag = 0;
    newtio.c_cc[VTIME] = 1; // Inter-character timer unused
    newtio.c_cc[VMIN] = 0;  // Blocking read until 5 chars received
    tcflush(fd, TCIOFLUSH);

    // Set new port settings
    if (tcsetattr(fd, TCSANOW, &newtio) == -1)
    {
        perror("tcsetattr");
        exit(-1);
    }

    printf("New termios structure set\n");
    if(transmiter){
        //rui
    }
    else{

        // Loop for input
        unsigned char aux = 0;
        stateMachine state = START;

        // RECEIVE SET
        while (STOP == FALSE)
        {
            read(fd, &aux, 1);
            determineState(&state, aux);

            if(state == StateSTOP) STOP = TRUE;
        }
        printf("Received SET");

        // Send UA
        unsigned char msg[BUF_SIZE] = {0};
        msg[0] =  FLAG;
        msg[1] = 0x03;
        msg[2] = 0x07;
        msg[3] = BCC(0x03,0x07);
        msg[4] = FLAG;

        int bytes= write(fd, msg, UA_SIZE);
        printf(":%s:%d\n", msg, bytes);
        sleep(1);
    }

    return fd;

}


int main(int argc, char *argv[])
{
    // Program usage: Uses either COM1 or COM2
    const char *serialPortName = argv[1];
    const int user = atoi(argv[2]);

    if (argc < 3)
    {
        printf("Incorrect program usage\n"
               "Usage: %s <SerialPort>\n"
               "Example: %s /dev/ttyS1\n",
               argv[0],
               argv[0]);
        exit(1);
    }

    //call llopen()
    int fd = llopen(serialPortName, user);


    // Restore the old port settings
    if (tcsetattr(fd, TCSANOW, &oldtio) == -1)
    {
        perror("tcsetattr");
        exit(-1);
    }

    close(fd);

    return 0;
}