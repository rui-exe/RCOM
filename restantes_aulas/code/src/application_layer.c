// Application layer protocol implementation

#include "application_layer.h"
#include <stdio.h>
#include <sys/types.h>
#include <unistd.h>

#define DATA 0x01
#define START 0x02
#define END 0x03

void applicationLayer(const char *serialPort, const char *role, int baudRate,
                      int nTries, int timeout, const char *filename)
{

    struct applicationLayer appLayer;
    LinkLayerRole linkLayerRole;
    linkLayerRole = (strcmp(role, "tx") == 0)? LlTx : LlRx;
    appLayer.status = (strcmp(role, "tx") == 0) ? 1 : 0;
    LinkLayer linkLayer;
    linkLayer.baudRate = baudRate;
    linkLayer.nRetransmissions = nTries;
    linkLayer.role = linkLayerRole;
    strcpy( linkLayer.serialPort, serialPort);
    linkLayer.timeout = timeout;
    int fd = llopen(linkLayer);    
    if(fd==-1) return;
    appLayer.fileDescriptor = fd;
    switch(appLayer.status){
        case 0:
            printf("Receiving file \n");
            receivePacket(fd, filename);
            break;
        case 1:
            printf("Sending file \n");
            sendPacket(fd, 0x02, filename);
            sendPacket(fd, 0x01, filename);
            sendPacket(fd, 0x03, filename);
            break;
        default:
            printf("Invalid role\n");
            

    }

    printf("END\n");
    llclose(0, linkLayer);
}
int sendPacket(int fd ,unsigned char C, const char *filename)
{
    switch(C)
    {
        case 0x01:
            return sendDataPacket(fd, filename);
        case 0x02: case 0x03:
            return sendControlPacket(fd, C, filename);
        default:
            return -1;
    }
}
int sendControlPacket(int fd, unsigned char C,const char* filename){
        
        FILE* fd_file = fopen(filename,"rb");
        fseek(fd_file, 0L, SEEK_END);
        int file_size = ftell(fd_file);
        fclose(fd_file);
        
        unsigned char buffer[1000];
        buffer[0] = C;
        buffer[1] = 0x00;
        buffer[2] = 0x02;
        buffer[3] = file_size>>8;
        buffer[4] = (unsigned char) file_size;
        buffer[5] = 0x01;
        buffer[6] = strlen(filename);
        strcpy(buffer+7,filename);


        llwrite(buffer,7+strlen(filename));
        return 0;
}
int sendDataPacket(int fd, const char *filename){
    FILE* fd_file = fopen(filename,"rb");
    int n = 0;
    unsigned char buffer[1000];
    unsigned int bytes_read = 0; 
    while((bytes_read=fread(buffer+4,1,996, fd_file)) > 0){
        buffer[0] = 0x01;
        buffer[1] = n;
        buffer[2] = bytes_read/256;
        buffer[3] = bytes_read%256;

        if(llwrite(buffer,bytes_read+4)==-1){
            printf("Max number tries reached ");
            exit(-1);
        }
        n++;
    }

    fclose(fd_file);
    return 0;
    }
    
int receivePacket(int fd, const char * filename){
    unsigned char buffer[2000];
    int n = 0, n_aux;
    int offset_needed = 0;
    FILE* gif_fd;
    unsigned int file_size, append_size, sizeRead;
    while(1){
        sizeRead = llread(buffer);
        
        if(buffer[0] == START){
            gif_fd = fopen(filename, "wb");
            file_size = buffer[3]<<8 | buffer[4];
        }
        else if(buffer[0] == DATA && sizeRead > 0){
            append_size = buffer[2]*256 + buffer[3];
            fwrite(buffer+4, 1,append_size, gif_fd);  
            if (buffer[1] == n){
                n++;
            }
        }
        else if(buffer[0] == END){
            
            printf("END\n");
            break;
        }
    }
    fclose(gif_fd);
    return fd;
}