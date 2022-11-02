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

typedef enum {START, FLAG_RCV, A_RCV, C_RCV, BCC_NORMAL, BCC_DATA, DONE} stateMachine;
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
#define ESC 0x7D
#define TRANSMITER 1
#define RECEIVER 0
#define ACK(n) ((n)<<7 | 0x05)
#define NACK(n) ((n)<<7 | 0x01)
#define REPEATED_MESSAGE 2

stateMachine state;
int failed = 0;
int alarm_enabled;
int alarm_count = 0;
int sn = 0;
int fd;

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
        case START:
            if( byte == FLAG) *state = FLAG_RCV;
            break;

        case FLAG_RCV:
            if(byte == FLAG) *state = FLAG_RCV;
            else if(byte == A_FLAG) *state = A_RCV;
            else *state = START;
            break;

        case A_RCV:
            if(byte == FLAG) *state = FLAG_RCV;
            else if(byte == C_FLAG) *state = C_RCV;
            else *state = START;
            break;

        case C_RCV:
            if(byte == FLAG) *state = FLAG_RCV;
            else if(byte == BCC(A_FLAG, C_FLAG)) *state = BCC_NORMAL;
            else *state = START;
            break;

        case BCC_NORMAL:
            if(byte == FLAG)  *state = DONE;
            else *state = START;
            break;
    }

}


////////////////////////////////////////////////
// LLOPEN
////////////////////////////////////////////////
int llopen(LinkLayer connectionParameters)
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

    fd = open(connectionParameters.serialPort, O_RDWR | O_NOCTTY);

    if (fd < 0)
    {
        perror(connectionParameters.serialPort);
        exit(-1);
    }

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
        state = START;
        unsigned char aux = 0;
        while (STOP == FALSE)
        {
            bytes = read(fd, &aux, 1);
            if( bytes > 0){
                determineState(&state, aux, 1);
            }
            if (state == DONE || failed == 1){ 
                    alarm(0);
                    STOP = TRUE;
                }
        }
    }while(alarm_count < connectionParameters.nRetransmissions && state != DONE);
    
    if(state == DONE) printf("UA Received\n");
    else {
        printf("UA Not Received\n");
        return -1;
        }

    }
    else{  

        // Loop for input
        unsigned char aux = 0;
        state = START;

        // RECEIVE SET
        while (STOP == FALSE)
        {
            read(fd, &aux, 1);
            determineState(&state, aux, 0);

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
    }

    return fd; 

    
}

void receiveACK(stateMachine* state,unsigned char byte, unsigned char * ack, int sn) {

    switch(*state){
        case START:
            if(byte == FLAG) *state = FLAG_RCV;
            break;

        case FLAG_RCV:
            if(byte == FLAG) *state = FLAG_RCV;
            else if(byte == A) *state = A_RCV;
            else *state = START;
            break;

        case A_RCV:
            if(byte == FLAG) *state = FLAG_RCV;
            else if(byte == ACK(sn)) {
                *state = C_RCV;
                *ack = ACK(sn);
            }
            else if (byte == NACK(sn)) {
                *state = C_RCV;
                *ack = NACK(sn);
            }
            else *state = START;

            
            break;

        case C_RCV:
            if(byte == BCC(A, *ack))
                *state = BCC_NORMAL;
            else if(byte == FLAG_RCV){
                *ack = FALSE;
                *state = FLAG_RCV;
            }
            else{
                *state = START;
                *ack = FALSE;
            }
            break;

        case BCC_NORMAL:
            if(byte == FLAG) *state = DONE;
            else{
                *ack = FALSE;
                *state = START;
                
            }
            break;
    }
    
}




int llwrite(const unsigned char *buf, int bufSize)
{
    int attemptNumber = 0;
    int done = FALSE;
    signal(SIGALRM, alarmHandler);
    state = START;

    // counting number in the middle
    unsigned int count = 0;

    for(int i = 0; i < bufSize; i++){
        if(buf[i] == FLAG || buf[i] == ESC)
            count++;
    }

    unsigned int size = bufSize + 6 + count;
    unsigned char msg[size];
    for(int i = 0; i < size; i++){
        msg[i]=0;
    }

    msg[0] = FLAG;
    msg[1] = A;
    msg[2] = sn << 7;
    msg[3] = BCC(A, sn << 7);
    unsigned int i = 0, BCC2=0;

    // byte stuffing
    for (int j = 0; j < bufSize; j++) {
        switch (buf[j]) {
            case FLAG:
                msg[j + i + 4] = ESC;
                msg[j + i + 5] = FLAG;
                BCC2 = BCC(BCC2, FLAG);
                i++;
                break;

            case ESC:
                msg[j + i + 4] = ESC;
                msg[j + i + 5] = ESC;
                BCC2 = BCC(BCC2, ESC);
                i++;
                break;

            default:
                msg[j + i + 4] = buf[j];
                BCC2 = BCC(BCC2, buf[j]);
        }
    }
    msg[i+bufSize+ 4] = BCC2;
    msg[i+bufSize+ 5] = FLAG;
    alarm_enabled = FALSE;
    STOP = FALSE;
    while(state != DONE && STOP != TRUE) {
        unsigned char received;
        unsigned char ack;
        
        if (alarm_enabled == FALSE) {
            if(attemptNumber == 4) return -1;
            attemptNumber ++;
            write(fd, msg, size);
            signal(SIGALRM, alarmHandler);
            alarm(3);
            alarm_enabled = TRUE;
        }

        unsigned int bytes = read(fd, &received, 1);
        if( bytes > 0){
            receiveACK(&state, received, &ack, 1-sn);
            

            if (state == DONE && ack == ACK(1-sn)){
                sn = 1-sn;
                alarm(0);
                STOP = TRUE;
                printf("RECEIVED ACK aka RR...\n");
            }
            // se  ack==NACK, tenho de reenviar
            else if(state == DONE){
                printf("RECEIVED NACK aka RREJ...\n");
                write(fd, msg, size);
                state = START;
            } 
        } 
        
        
    }
    

    return 0;
}


////////////////////////////////////////////////
// LLREAD
////////////////////////////////////////////////


/**
 * @brief
 * 
 * @param packet 
 * @param sn 
 * @return true se for para mandar um ack, false se for para mandar um nack
 */
int receiveData(unsigned char *packet, int sn, size_t *size_read) {

    state = START;  
    unsigned char C_CONTROL = sn << 7;  //sn
    unsigned char C_REPLY = (1-sn) << 7;   // nr
    unsigned int i=0, BCC2 = 0, stuffing = 0;  // meter a 1 sempre for enviado um ESC
    while(state != DONE){

        unsigned char received;
        unsigned int bytes = read(fd, &received, 1);

        if( bytes > 0){
            switch(state){
                case START:
                    if( received == FLAG) state = FLAG_RCV;
                    break;

                case FLAG_RCV:
                    if(received  == FLAG) state = FLAG_RCV;
                    else if(received  == A) state = A_RCV;
                    else state = START;
                    break;

                case A_RCV:
                    if(received  == FLAG) state = FLAG_RCV;
                    else if(received == C_CONTROL){
                        state = C_RCV;
                    }
                    // a receber uma mensagem repetida, e a querer a proxima (houve um erro)
                    else if(C_REPLY == received ){
                        // mandar um ack, para passar para a prox mensagem 
                        return REPEATED_MESSAGE;
                    }
                    else state = START;
                    break;

                case C_RCV:
                    if(received  == FLAG) state = FLAG_RCV;
                    else if(received  == BCC(A, C_CONTROL)) state = BCC_DATA;
                    else state = START;
                    break;

                //stuffing
                case BCC_DATA:
                    if(!stuffing){
                        if(received  == FLAG){
                            state = DONE;
                            
                            BCC2 = BCC(BCC2, packet[i-1]);
                            size_read = i-1;
                            return (BCC2 == packet[i-1]); 
                            // ser igual ao penultimo byte antes da flag
                        } 
                        else if(received  == ESC) stuffing = TRUE;
                        else{
                            BCC2 = BCC(BCC2, received );
                            packet[i] = received ;
                            i++;
                        }
                    }
                    else{
                        stuffing = FALSE;
                        BCC2 = BCC(BCC2, received );
                        packet[i] = received ;
                        i++;
                    }
                    break;
            }
        }
        
        
    }

    return FALSE;

}



int llread(unsigned char *packet)
{
    int reply;
    size_t size_read;
    while( (reply = receiveData(packet, sn, &size_read)) != TRUE){
        //mandar nack
        if( reply == 0){
            printf("Sending NACK or RRej...\n");
            unsigned char C_NACK = NACK(1-sn);
            unsigned char buf[] = {FLAG, A, C_NACK, BCC(A, C_NACK), F};
            write(fd, buf, 5);
        }
        // mandar ack, proveniente de mensagens repetidas
        else{
            printf("Sending ACK because repeated message...\n");
            unsigned char C_ACK = ACK(sn);
            unsigned char buf[] = {FLAG, A, C_ACK, BCC(A, C_ACK), F};
             write(fd, buf, 5);
        }
    }
    //mandar ack
    printf("Sending ACK everything in order...\n");
    sn = 1-sn;
    unsigned char C_ACK = ACK(sn);
    unsigned char buf[] = {FLAG, A, C_ACK, BCC(A, C_ACK), F};
    write(fd, buf, 5);

    return size_read;
}

////////////////////////////////////////////////
// LLCLOSE
////////////////////////////////////////////////
void determineStateDISC(stateMachine *state, char byte)
{
    unsigned char A_FLAG = 0x03;
    unsigned char C_FLAG = 0x0B;

    switch(*state){
        case START:
            if( byte == FLAG) *state = FLAG_RCV;
            break;

        case FLAG_RCV:
            if(byte == FLAG) *state = FLAG_RCV;
            else if(byte == A_FLAG) *state = A_RCV;
            else *state = START;
            break;

        case A_RCV:
            if(byte == FLAG) *state = FLAG_RCV;
            else if(byte == C_FLAG) *state = C_RCV;
            else *state = START;
            break;

        case C_RCV:
            if(byte == FLAG) *state = FLAG_RCV;
            else if(byte == BCC(A_FLAG, C_FLAG)) *state = BCC_NORMAL;
            else *state = START;
            break;

        case BCC_NORMAL:
            if(byte == FLAG)  *state = DONE;
            else *state = START;
            break;
    }

}

int llclose(int statistics, LinkLayer linkLayer)
{
    int bytes = 0;
    unsigned char msg[256] = {0};
    unsigned char aux = 0;
    switch (linkLayer.role)
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
            state = START;
            STOP = FALSE;
            while (STOP == FALSE)
            {   
                read(fd, &aux, 1);
                determineStateDISC(&state, aux);
                if(state == DONE) STOP = TRUE;
            }

            // Send UA
            unsigned char ua[256] = {0};
            ua[0] =  FLAG;
            ua[1] = 0x03;
            ua[2] = 0x07;
            ua[3] = BCC(0x03,0x07);
            ua[4] = FLAG;
            int bytes= write(fd, ua, UA_SIZE);
            printf("Sent UA\n");
            break;

        case LlRx:
            STOP = FALSE;
            state = START;
            // receive DISC
            while (STOP == FALSE)
            {   
                read(fd, &aux, 1);
                determineStateDISC(&state, aux);

                if(state == DONE) STOP = TRUE;
            }
            printf("Received DISC\n");

            // sending DISC
            msg[0] =  FLAG;
            msg[1] = 0x03;
            msg[2] = 0x0B;
            msg[3] = BCC(0x03,0x0B);
            msg[4] = FLAG;
            bytes= write(fd, msg, 5);

            //receiving UA
            state = START;
            STOP = FALSE;
             while (STOP == FALSE)
            {   
                read(fd, &aux, 1);
                determineState(&state, aux, 1);

                if(state == DONE) STOP = TRUE;
            }
             printf("Received UA\n");

            break;
    }
      if (tcsetattr(fd,TCSANOW,&oldtio) != 0){
        perror("llclose() - Error on tcsetattr()");
        return -1;
    }
    if (close(fd) != 0){
        perror("llclose() - Error on close()");
        return -1;
    }
        return 1;
}