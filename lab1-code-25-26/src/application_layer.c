// Application layer protocol implementation

#include "application_layer.h"
#include "link_layer.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>


static int startTransmission(const char *filename);
static int startReception(const char *filename);
static FILE* openFile(const char *filename, const char *mode);
static long getFileSize(FILE *file);
static int sendControlPacket(unsigned char controlType, const char *filename, long fileSize);


void applicationLayer(const char *serialPort, const char *role, int baudRate,
                      int nTries, int timeout, const char *filename)
{
    LinkLayer config  = {
        .baudRate = baudRate,
        .nRetransmissions = nTries,
        .role = strcmp(role,"tx") == 0 ? LlTx : LlRx,
        .timeout = timeout,
    };
    strcpy(config.serialPort, serialPort);
    
    if (llopen(config) < 0) {
        perror("Erro ao abrir a conexão\n");
        exit(-1);
    }

    printf("Choosing transmission mode...\n");
    if (config.role == LlTx) {
        startTransmission(filename);
    }
    else if (config.role == LlRx) {
        startReception(filename);
    }

    printf("Closing connection...\n");
    llclose();
}


// Inicia a transmissão
static int startTransmission(const char *filename) {

    // Abre o arquivo
    FILE *file = openFile(filename, "rb");
    if (!file)
        return -1;

    // Calcula o tamanho do arquivo
    long fileSize = getFileSize(file);

    // Envia o start packet

    printf("Enviando start packet...\n");

    if(sendControlPacket(0x02, filename, fileSize) < 0) {
        perror("Erro ao enviar o start packet");
        return 1;
    }

   

    ///////////////////////////////////
    // Implementar ciclo while que cria e envia pacotes
    ///////////////////////////////////


    // Envia o end packet

    printf("Enviando end packet...\n");
    if(sendControlPacket(0x03, filename, fileSize) < 0) {
        perror("Erro ao enviar o end packet");
        return 1;
    }

    fclose(file);
    return 0;

}


static int startReception(const char *filename) {
    
    FILE *file = openFile(filename, "wb");
    if (!file) return -1;

    unsigned char buffer[512];
    int packetSize;

    // Recebe pacotes até o final do arquivo
    while ((packetSize = llread(buffer)) > 0) {
        if (buffer[0] == 0x01) {  // Verifica se é um pacote de dados
            fwrite(buffer + 4, sizeof(unsigned char), packetSize - 4, file);
        } else if (buffer[0] == 0x03) {  // Pacote de controle final
            break;
        }
    }

    fclose(file);  // Fecha o arquivo após a recepção completa
    return packetSize < 0 ? -1 : 0;
}


static int sendControlPacket(unsigned char controlType, const char *filename, long fileSize) {
    int filenameSize = strlen(filename);
    unsigned char *packet = malloc(7 + filenameSize); // Aloca memória para o pacote de controle
    int index = 0;

    packet[index++] = controlType;

    // File Size Type- Length- Value (TLV) structure
    packet[index++] = 0;
    packet[index++] = sizeof(long);

    for (int i = sizeof(long) - 1; i >= 0; i--) {
        packet[index++] = (fileSize >> (8 * i)) & 0xFF;
        index++;
    }

    // File name TLV structure
    packet[index++] = 1;              
    packet[index++] = filenameSize;
    memcpy(packet + index, filename, filenameSize);

    index += filenameSize;

    if (llwrite(packet, index) < 0) {
        free(packet);
        return -1;
    }

    printf("Pacote de controle enviado com sucesso (tipo: %02X, tamanho: %d bytes)\n", controlType, index);


    free(packet);
    return 0;

}

// Abre um arquivo no modo indicado
static FILE* openFile(const char *filename, const char *mode) {
    FILE *file = fopen(filename, mode);
    if (!file) {
        perror("Erro ao abrir o arquivo\n");
    }
    return file;
}

// Calcula o tamanho do arquivo
static long getFileSize(FILE *file) {
    // Move o ponteiro do arquivo para o final
    fseek(file, 0, SEEK_END);
    // Guarda a size
    long size = ftell(file);
    // Volta o ponteiro para o início
    fseek(file, 0, SEEK_SET);
    return size;
}



