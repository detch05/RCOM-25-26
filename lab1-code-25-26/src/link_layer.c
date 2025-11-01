// Link layer protocol implementation

#include "link_layer.h"
#include "serial_port.h"

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>


// MISC
#define _POSIX_SOURCE 1 // POSIX compliant source

#define MAX_FRAME_SIZE 2048 // Tamanho maximo de cada frame


// Endereços de controlo para UA
#define C_RR0 0xAA  
#define C_RR1 0xAB   
#define C_REJ0 0x54  
#define C_REJ1 0x55

// Bytes Importantes e muito utilizados

#define FLAG 0x7E     // Usado para indicar o início e fim de um frame

// Addresses used all frames

#define A_TRANSMITTER 0x03 // Address in the frame from the transmitter
#define A_RECEIVER 0x01 // Address in the frame from the receiver


// Comandos de controlo importantes

#define C_SET 0x03
#define C_UA 0x07
#define C_DISC 0x0B   //Encerra a comunicação
#define C_DATA 0x01   //Indica envio de dados
#define C_RR 0x05     //Reconhece recebimento correto de uma trama
#define C_REJ 0x01      //Rejeita uma trama incorreta


// Estados utilizado pela maquina de estados para processar frames
typedef enum {
    START,
    FLAG_RCV,
    A_RCV,
    C_RCV,
    BCC1_OK,
    DATA,
    STOP_R
} LinkLayerState;


int timeout = 3;                
int retransmissions = 5;         
int alarmEnabled = 0;            
int alarmCount = 0;

void alarmHandler(int signal) {
    alarmEnabled = 1;
    alarmCount++;
}


////////////////////////////////////////////////
// LLOPEN
////////////////////////////////////////////////
int llopen(LinkLayer connectionParameters)
{
    if (openSerialPort(connectionParameters.serialPort, connectionParameters.baudRate) < 0)
    {
        perror("openSerialPort");
        exit(-1);
    }

    printf("Serial port %s opened\n", connectionParameters.serialPort);

    // Read from serial port until the 'z' char is received.

    // NOTE: This while() cycle is a simple example showing how to read from the serial port.
    // It must be changed in order to respect the specifications of the protocol indicated in the Lab guide.

    // TODO: Save the received bytes in a buffer array and print it at the end of the program.
    if(connectionParameters.role == LlRx)
    {
        unsigned char byte;
        LinkLayerState state = START;
        while (!alarmEnabled && state != STOP_R) {
            if (readByteSerialPort(&byte) > 0) {
                switch (state) {
                    case START:
                        if (byte == FLAG) state = FLAG_RCV;
                        break;
                    case FLAG_RCV:
                        if (byte == A_TRANSMITTER) state = A_RCV;
                        else if (byte == FLAG) state = FLAG_RCV;
                        else state = START;
                        break;
                    case A_RCV:
                        if (byte == C_SET) state = C_RCV;  // Recepção correta
                        else if (byte == FLAG) state = FLAG_RCV;
                        else state = START;
                        break;
                    case C_RCV:
                        if (byte == A_TRANSMITTER ^ C_SET) state = BCC1_OK;
                        else if (byte == FLAG) state = FLAG_RCV;
                        else state = START;
                        break;
                    case BCC1_OK:
                        if (byte == FLAG) state = STOP_R;
                        else state = START;
                        break;
                    default:
                        state = START;
                        break;
                }
            }
        }

        unsigned char frame[5];
        int frameIndex = 0;
        
        frame[frameIndex++] = FLAG;
        frame[frameIndex++] = A_TRANSMITTER;
        frame[frameIndex++] = C_UA;
        frame[frameIndex++] = A_TRANSMITTER ^ C_UA;
        frame[frameIndex++] = FLAG;

        for (int i = 0; i < frameIndex; i++) {
            printf("DEBUG (llopen): Frame[%d] = 0x%X\n", i, frame[i]);
        }
        writeBytesSerialPort(frame, frameIndex);

        if (state == STOP_R && byte == FLAG) {
            printf("DEBUG (llopen): Frame recebido com sucesso\n");
            return 1;
        }
    }

    else if (connectionParameters.role == LlTx)
    {
        unsigned char frame[5];
        int frameIndex = 0;
        
        frame[frameIndex++] = FLAG;
        frame[frameIndex++] = A_TRANSMITTER;
        frame[frameIndex++] = C_SET;
        frame[frameIndex++] = A_TRANSMITTER ^ C_SET;
        frame[frameIndex++] = FLAG;

        for (int i = 0; i < frameIndex; i++) {
            printf("DEBUG (llopen): Frame[%d] = 0x%X\n", i, frame[i]);
        }

        int tentativas = retransmissions;
        while (tentativas > 0) {
            writeBytesSerialPort(frame, frameIndex);
            alarmEnabled = 0;
            alarm(timeout);

            printf("DEBUG (llopen): Frame enviado, aguardando confirmação...\n");

            unsigned char byte;
            LinkLayerState state = START;
            while (!alarmEnabled && state != STOP_R) {
                if (readByteSerialPort(&byte) > 0) {
                    switch (state) {
                        case START:
                            if (byte == FLAG) state = FLAG_RCV;
                            else state = START;
                            break;
                        case FLAG_RCV:
                            if (byte == A_TRANSMITTER) state = A_RCV;
                            else if (byte == FLAG) state = FLAG_RCV;
                            else state = START;
                            break;
                        case A_RCV:
                            if (byte == C_UA) state = C_RCV;
                            else if (byte == FLAG) state = FLAG_RCV;
                            else state = START;
                            break;
                        case C_RCV:
                            if (byte == A_TRANSMITTER ^ C_UA) state = BCC1_OK;
                            else if (byte == FLAG) state = FLAG_RCV;
                            else state = START;
                            break;
                        case BCC1_OK:
                            if (byte == FLAG) state = STOP_R;
                            else state = START;
                            break;
                        default:
                            state = START;
                            break;
                    }
                }
            }

            // Confirmar que o frame foi corretamente recebido e avançar
            if (state == STOP_R && byte == FLAG) {
                printf("DEBUG (llopen): Frame recebido com sucesso\n");
                return frameIndex;
            }

            printf("DEBUG (llopen): Timeout ou erro, reenviando frame...\n");
            tentativas--;
        }
    }

    printf("DEBUG (llopen): Erro ao enviar frame/n");
    return -1;
}

////////////////////////////////////////////////
// LLWRITE
////////////////////////////////////////////////
int llwrite(const unsigned char *buf, int bufSize) {

    unsigned char frame[MAX_FRAME_SIZE];
    int frameIndex = 0;
    
    frame[frameIndex++] = FLAG;
    frame[frameIndex++] = A_TRANSMITTER;
    frame[frameIndex++] = C_DATA;
    frame[frameIndex++] = A_TRANSMITTER ^ C_DATA;

    unsigned char BCC2 = getBCC2(buf, bufSize);
    frameIndex += byteStuffing(buf, bufSize, &frame[frameIndex]);
    frameIndex += byteStuffing(&BCC2, 1, &frame[frameIndex]);
    frame[frameIndex++] = FLAG;

    for (int i = 0; i < frameIndex; i++) {
        printf("DEBUG (llwrite): Frame[%d] = 0x%X\n", i, frame[i]);
    }

    int tentativas = retransmissions;
    while (tentativas > 0) {
        writeBytesSerialPort(frame, frameIndex);
        alarmEnabled = 0;
        alarm(timeout);

        printf("DEBUG (llwrite): Frame enviado, aguardando confirmação...\n");

        unsigned char byte;
        LinkLayerState state = START;
        while (!alarmEnabled && state != STOP_R) {
            if (readByteSerialPort(&byte) > 0) {
                switch (state) {
                    case START:
                        if (byte == FLAG) state = FLAG_RCV;
                        break;
                    case FLAG_RCV:
                        if (byte == A_RECEIVER) state = A_RCV;
                        break;
                    case A_RCV:
                        if (byte == C_RR0 || byte == C_RR1) {
                            state = STOP_R;  // Recepção correta
                        } else if (byte == C_REJ0 || byte == C_REJ1) {
                            // Reiniciar a transmissão
                            state = START; 
                            printf("DEBUG (llwrite): REJ received, resending frame...\n");
                            break;
                        }
                        break;
                    default:
                        state = START;
                        break;
                }
            }
        }

        // Confirmar que o frame foi corretamente recebido e avançar
        if (state == STOP_R && (byte == C_RR0 || byte == C_RR1)) {
            printf("DEBUG (llwrite): Frame recebido com sucesso\n");
            return frameIndex;
        }

        printf("DEBUG (llwrite): Timeout ou erro, reenviando frame...\n");
        tentativas--;
    }

    
    printf("DEBUG (llwrite): Erro ao enviar frame/n");
    return -1;
}

////////////////////////////////////////////////
// LLREAD
////////////////////////////////////////////////
int llread(unsigned char *packet)
{
    readByteSerialPort(packet);
    return 0;
}

////////////////////////////////////////////////
// LLCLOSE
////////////////////////////////////////////////
int llclose(LinkLayerRole role)
{   
    printf("DEBUG (llclose): Iniciando processo de terminação/n");
    LinkLayerState state = START;
    if(role == LlRx)
    {
        unsigned char byte;
        LinkLayerState state = START;
        while (!alarmEnabled && state != STOP_R) {
            if (readByteSerialPort(&byte) > 0) {
                switch (state) {
                    case START:
                        if (byte == FLAG) state = FLAG_RCV;
                        else state = START;
                        break;
                    case FLAG_RCV:
                        if (byte == A_TRANSMITTER) state = A_RCV;
                        else if (byte == FLAG) state = FLAG_RCV;
                        else state = START;
                        break;
                    case A_RCV:
                        if (byte == C_DISC) state = C_RCV;
                        else if (byte == FLAG) state = FLAG_RCV;
                        else state = START;
                        break;
                    case C_RCV:
                        if (byte == A_TRANSMITTER ^ C_DISC) state = BCC1_OK;
                        else if (byte == FLAG) state = FLAG_RCV;
                        else state = START;
                        break;
                    case BCC1_OK:
                        if (byte == FLAG) state = STOP_R;
                        else state = START;
                        break;
                    default:
                        state = START;
                        break;
                }
            }
        }
        unsigned char frame[5];
        int frameIndex = 0;
        
        frame[frameIndex++] = FLAG;
        frame[frameIndex++] = A_RECEIVER;
        frame[frameIndex++] = C_DISC;
        frame[frameIndex++] = A_TRANSMITTER ^ C_DISC;
        frame[frameIndex++] = FLAG;

        for (int i = 0; i < frameIndex; i++) {
            printf("DEBUG (llclose): Frame[%d] = 0x%X\n", i, frame[i]);
        }

        writeBytesSerialPort(frame, frameIndex);
    }
    else if (role == LlTx)
    {
        unsigned char frame[5];
        int frameIndex = 0;
        
        frame[frameIndex++] = FLAG;
        frame[frameIndex++] = A_TRANSMITTER;
        frame[frameIndex++] = C_DISC;
        frame[frameIndex++] = A_TRANSMITTER ^ C_DISC;
        frame[frameIndex++] = FLAG;

        for (int i = 0; i < frameIndex; i++) {
            printf("DEBUG (llclose): Frame[%d] = 0x%X\n", i, frame[i]);
        }

        int tentativas = retransmissions;
        while (tentativas > 0) {
            writeBytesSerialPort(frame, frameIndex);
            alarmEnabled = 0;
            alarm(timeout);

            printf("DEBUG (llclose): Frame enviado, aguardando confirmação...\n");

            unsigned char byte;
            LinkLayerState state = START;
            while (!alarmEnabled && state != STOP_R) {
                if (readByteSerialPort(&byte) > 0) {
                    switch (state) {
                        case START:
                            if (byte == FLAG) state = FLAG_RCV;
                            else state = START;
                            break;
                        case FLAG_RCV:
                            if (byte == A_RECEIVER) state = A_RCV;
                            else if (byte == FLAG) state = FLAG_RCV;
                            else state = START;
                            break;
                        case A_RCV:
                            if (byte == C_DISC) state = C_RCV;
                            else if (byte == FLAG) state = FLAG_RCV;
                            else state = START;
                            break;
                        case C_RCV:
                            if (byte == A_TRANSMITTER ^ C_DISC) state = BCC1_OK;
                            else if (byte == FLAG) state = FLAG_RCV;
                            else state = START;
                            break;
                        case BCC1_OK:
                            if (byte == FLAG) state = STOP_R;
                            else state = START;
                            break;
                        default:
                            state = START;
                            break;
                    }
                }
            }

            // Confirmar que o frame foi corretamente recebido e avançar
            if (state == STOP_R && byte == FLAG) {
                printf("DEBUG (llclose): Frame recebido com sucesso\n");
                unsigned char frame[5];
                int frameIndex = 0;
                
                frame[frameIndex++] = FLAG;
                frame[frameIndex++] = A_TRANSMITTER;
                frame[frameIndex++] = C_UA;
                frame[frameIndex++] = A_TRANSMITTER ^ C_UA;
                frame[frameIndex++] = FLAG;

                for (int i = 0; i < frameIndex; i++) {
                    printf("DEBUG (llopen): Frame[%d] = 0x%X\n", i, frame[i]);
                }
                writeBytesSerialPort(frame, frameIndex);
            }

            printf("DEBUG (llclose): Timeout ou erro, reenviando frame...\n");
            tentativas--;
        }
    }
    closeSerialPort();
    return 0;
}


unsigned char getBCC2(const unsigned char *buf, int bufSize) {
    unsigned char bcc2 = 0;
    for (int i = 0; i < bufSize; i++) {
        bcc2 ^= buf[i];
    }
    return bcc2;
}

int byteStuffing(const unsigned char *input, int length, unsigned char *output) {
    int stuffedIndex = 0;  
    
    for (int i = 0; i < length; i++) {
        if (input[i] == FLAG) {
            output[stuffedIndex++] = 0x7D;
            output[stuffedIndex++] = 0x5E;
            printf("DEBUG (byteStuffing): FLAG detectado, aplicando stuffing -> 0x7D + 0x5E\n");
        } 
        
        else if (input[i] == 0x7D) {
            output[stuffedIndex++] = 0x7D;
            output[stuffedIndex++] = 0x5D;
            printf("DEBUG (byteStuffing): 0x7D detectado, aplicando stuffing -> 0x7D + 0x5D\n");
        } 
    
        else {
            output[stuffedIndex++] = input[i];
            //printf("DEBUG (byteStuffing): Byte sem alteração = 0x%X\n", input[i]);
        }
    }
    printf("DEBUG (byteStuffing): Tamanho final após stuffing = %d\n", stuffedIndex);
    return stuffedIndex;
}


int byteDestuffing(const unsigned char *input, int length, unsigned char *output) {

    int destuffedIndex = 0;   
    int finded = FALSE;             

    for (int i = 0; i < length; i++) {
        if (finded) {
            if (input[i] == 0x5E) {
                output[destuffedIndex++] = FLAG;
                printf("DEBUG (byteDestuffing): 0x7D seguido de 0x5E, convertendo para FLAG\n");
            } 
            else if (input[i] == 0x5D) {
                output[destuffedIndex++] = 0x7D;
                printf("DEBUG (byteDestuffing): 0x7D seguido de 0x5D, convertendo para 0x7D\n");
            }
            finded = FALSE;
        } 

        else if (input[i] == 0x7D) {
            finded = TRUE;    
            printf("DEBUG (byteDestuffing): Byte 0x7D detectado, aguardando próximo byte\n");
        } 

        else if (input[i] == FLAG && i == length - 1) { 
            printf("DEBUG (byteDestuffing): FLAG final detectado, parando processamento\n");
            break;
        } 

        else {
            output[destuffedIndex++] = input[i];
            //printf("DEBUG (byteDestuffing): Byte sem alteração = 0x%X\n", input[i]);
        }
    }
    printf("DEBUG (byteDestuffing): Tamanho final após destuffing = %d\n", destuffedIndex);
    return destuffedIndex;
}

