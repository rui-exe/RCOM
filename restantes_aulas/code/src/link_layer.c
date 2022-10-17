// Link layer protocol implementation

#include "link_layer.h"
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <termios.h>
#include <unistd.h>
#include <signal.h>

typedef enum {Start, FLAG_RCV, A_RCV, C_RCV, BCC_NORMAL , DONE} stateMachine;
// MISC
#define _POSIX_SOURCE 1 // POSIX compliant source
#define FALSE 0
#define TRUE 1
#define SET_SIZE 5
#define UA_SIZE 5
#define FLAG 0x7e
#define A 0x03
#define C 0x03
#define C_RECEIVER 0x07
#define BCC(n,m) (n ^ m)
#define F 0x7e
#define TRANSMITER 1
#define RECEIVER 0

stateMachine state;
int failed = 0;
int alarm_enabled;
int alarm_count = 0;

volatile int STOP = FALSE;
struct termios oldtio;

void alarmHandler(int signal)
{   
    printf("<Receiver didn't Answer>\n");
    alarm_enabled = FALSE;
    alarm_count++;
    failed = 1;
}

void determineState(stateMachine *state, char byte, int user)
{
    unsigned char A_FLAG = 0x03;
    unsigned char C_FLAG = (user)? 0x07 : 0x03;
    
    
    switch(*state){
        case Start:
            if( byte == FLAG) *state = FLAG_RCV;
            break;

        case FLAG_RCV:
            if(byte == FLAG) *state = FLAG_RCV;
            else if(byte == A_FLAG) *state = A_RCV;
            else *state = Start;
            break;

        case A_RCV:
            if(byte == FLAG) *state = FLAG_RCV;
            else if(byte == C_FLAG) *state = C_RCV;
            else *state = Start;
            break;

        case C_RCV:
            if(byte == FLAG) *state = FLAG_RCV;
            else if(byte == BCC(A_FLAG, C_FLAG)) *state = BCC_NORMAL;
            else *state = Start;
            break;

        case BCC_NORMAL:
            if(byte == FLAG)  *state = DONE;
            else *state = Start;
            break;
    }

}


////////////////////////////////////////////////
// LLOPEN
////////////////////////////////////////////////
int llopen(LinkLayer connectionParameters, int fd)
{
   struct sigaction action;
    sigemptyset(&action.sa_mask);
    action.sa_flags = 0;
    action.sa_handler = alarmHandler;
    sigaction(SIGALRM, &action, NULL);
    //(void)siginterrupt(SIGALRM,1);
    // Open serial port device for reading and writing, and not as controlling tty
    // because we don't want to get killed if linenoise sends CTRL-C.

    struct termios newtio;

    // Save current port settings
    if (tcgetattr(fd, &oldtio) == -1)
    {
        perror("tcgetattr");
        exit(-1);
    }

    // Clear struct for new port settings
    memset(&newtio, 0, sizeof(newtio));

    newtio.c_cflag = connectionParameters.baudRate | CS8 | CLOCAL | CREAD;
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
    if(connectionParameters.role == LlTx){
        // Create string to send
    unsigned char buf[256] = {0};
    buf[0] = FLAG;
    buf[1] = A;
    buf[2] = C;
    buf[3] = BCC(buf[1],buf[2]);
    buf[4] = F;
    failed = 0;
    int bytes = 0;
    do{
        STOP = FALSE;
        bytes = write(fd, buf, SET_SIZE);
        printf("%d bytes written\n", bytes);
        alarm(connectionParameters.timeout);
        alarm_enabled = TRUE;
        printf("Attempt %d\n", alarm_count);
        failed = 0;
        state = Start;
        unsigned char aux = 0;
        sleep(1);
        while (STOP == FALSE)
        {
            bytes = read(fd, &aux, 1);
            if( bytes > 0){
                determineState(&state, aux,connectionParameters.role);
            }
            if (state == DONE || failed == 1){ 
                    alarm(0);
                    STOP = TRUE;
                }
        }
    }while(alarm_count < connectionParameters.nRetransmissions && state != DONE);
    if(state == DONE) printf("UA Received\n");
    else printf("UA Not Received\n");

    }
    else{

        // Loop for input
        unsigned char aux = 0;
        stateMachine state = Start;

        // RECEIVE SET
        while (STOP == FALSE)
        {
            read(fd, &aux, 1);
            determineState(&state, aux, connectionParameters.role);

            if(state == DONE) STOP = TRUE;
        }
        printf("Received SET");

        // Send UA
        unsigned char msg[256] = {0};
        msg[0] =  FLAG;
        msg[1] = 0x03;
        msg[2] = 0x07;
        msg[3] = BCC(0x03,0x07);
        msg[4] = FLAG;

        int bytes= write(fd, msg, UA_SIZE);
        printf(":%s:%d\n", msg, bytes);
        // sleep(1);
    }

    return fd; 

    
}

////////////////////////////////////////////////
// LLWRITE
////////////////////////////////////////////////

int llwrite(LinkLayer connectionParameters,int fd,const unsigned char *buf, int bufSize)
{
    return 0;
}

////////////////////////////////////////////////
// LLREAD
////////////////////////////////////////////////
int llread(int fd, unsigned char *packet)
{
    return 0;
}

////////////////////////////////////////////////
// LLCLOSE
////////////////////////////////////////////////
void determineStateDisc(stateMachine *state, char byte)
{
    unsigned char A_FLAG = 0x03;
    switch(*state){
        case Start:
            if( byte == FLAG) *state = FLAG_RCV;
            break;

        case FLAG_RCV:
            if(byte == FLAG) *state = FLAG_RCV;
            else if(byte == A_FLAG) *state = A_RCV;
            else *state = Start;
            break;

        case A_RCV:
            if(byte == FLAG) *state = FLAG_RCV;
            else if(byte == 0x0B) *state = C_RCV;
            else *state = Start;
            break;

        case C_RCV:
            if(byte == FLAG) *state = FLAG_RCV;
            else if(byte == BCC(A_FLAG, C_FLAG)) *state = BCC_NORMAL;
            else *state = Start;
            break;

        case BCC_NORMAL:
            if(byte == FLAG)  *state = DONE;
            else *state = Start;
            break;
    }

}

int llclose(int fd)
{
    stateMachine state;
    int bytes = 0;
    unsigned char msg[256] = {0};
    unsigned char aux = 0;
    switch (connectionParameters.role)
    {
    case LlTx:
        // Send DISC
        msg[0] =  FLAG;
        msg[1] = 0x03;
        msg[2] = 0x0B;
        msg[3] = BCC(0x03,0x0B);
        msg[4] = FLAG;

        bytes= write(fd, msg, 5);
        printf("Sent Disconnect Flag\n", msg, bytes);
        sleep(1);
        state = Start;
        STOP = FALSE;
        while (STOP == FALSE)
        {
            read(fd, &aux, 1);
            determineStateDISC(&state, aux);

            if(state == DONE) STOP = TRUE;
        }
        break;
    case LlRx:
        STOP = FALSE;
        state = Start;
        while (STOP == FALSE)
        {
            read(fd, &aux, 1);
            determineStateDISC(&state, aux);

            if(state == DONE) STOP = TRUE;
        }
        printf("Received DISC");
        msg[0] =  FLAG;
        msg[1] = 0x03;
        msg[2] = 0x0B;
        msg[3] = BCC(0x03,0x0B);
        msg[4] = FLAG;
        bytes= write(fd, msg, 5);
        break;
    }
      if (tcsetattr(fd,TCSANOW,&oldtio) != 0){
        log_error("llclose() - Error on tcsetattr()");
        return -1;
    }
    if (close(fd) != 0){
        log_error("llclose() - Error on close()");
        return -1;
    }
        return 1;
}
