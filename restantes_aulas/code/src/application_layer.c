// Application layer protocol implementation

#include "application_layer.h"
#include <io.h>

#define DATA 0x01
#define START 0x02
#define END 0x03

void applicationLayer(const char *serialPort, const char *role, int baudRate,
                      int nTries, int timeout, const char *filename)
{
    struct applicationLayer appLayer;
    appLayer.status = (strcmp(role, "tx") == 0) ? 1 : 0;
    int fd = open(serialPort, O_RDWR | O_NOCTTY);
    appLayer.fileDescriptor = fd;
    switch(appLayer.status){
        case 0:
            printf("Receiving file ");
            receivePacket(fd);
        case 1:
            printf("Sending file ");
            sendPacket(fd, 0x02, filename);
            sendPacket(fd, 0x01, filename);
            sendPacket(fd, 0x03, filename);
        default:
            printf("Invalid role");
    }
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
        int fd_file = open(filename,"r");
        fseek(fd_file, 0L, SEEK_END);
        int file_size = ftell(fd_file);
        unsigned char buffer[1000];
        buffer[0] = C;
        buffer[1] = 0x00;
        buffer[2] = 0x02;
        buffer[3] = file_size>>8;
        buffer[4] = (unsigned char) file_size;
        buffer[5] = 0x01;
        buffer[6] = strlen(filename);
        strcpy(buffer+7,filename);
        //llwrite(fd,buffer,7+strlen(filename));
        return 0;
}
sendDataPacket(int fd, const char *filename){
    int fd_file = open(filename,"r");
    int n = 0;
    unsigned char buffer[1000];
    while(read(fd_file,buffer+4,996) > 0){
        buffer[0] = 0x01;
        buffer[1] = n;
        buffer[2] = 996>>8;
        buffer[3] = (unsigned char) 996;
        //llwrite(fd,buffer,1000);
        n++;
    }
    return 0;
    }
    
int receivePacket(int fd){
    unsigned char buffer[1000];
    int n = 0, n_aux;
    int offset_needed = 0;
    int gif_fd, file_size, append_size, sizeRead;
    while(1){
        sizeRead = llread(fd,buffer);
        if(buffer[0] == START){
            gif_fd = open(buffer+7,O_CREAT | O_WRONLY, 0666);
            file_size = buffer[3]<<8 | buffer[4];
        }
        if(buffer[0] == DATA && sizeRead > 0){
            append_size = buffer[2]<<8 + buffer[3];
            if (buffer[1] != n){
                off_t offset = (buffer[1] + offset_needed) * (1000-4) ;
                lseek(gif_fd, offset, SEEK_SET);
             }  

            write(gif_fd,buffer+4,append_size);  

            if(buffer[1] != n){
                lseek(gif_fd, 0 , SEEK_HOLE);
            }
            if (buffer[1] == n){
                n++;
            }
            n_aux = n;
            n %= 255;

            if (n % 255 == 0 && n_aux!= 0){
                offset_needed+=255;
            }
        }
        if(buffer[0] == END){
            break;
        }
    }
    close(gif_fd);
    return fd;
}
