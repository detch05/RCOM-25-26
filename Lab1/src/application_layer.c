// Application layer protocol implementation

#include "application_layer.h"
#include "link_layer.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>


static int startTransmission(const char *filename);
static int startReception(const char *filename);
static int sendControlPacket(unsigned char controlType, const char *filename, long fileSize);
static unsigned char* createDataPacket(unsigned char *buffer, int bufferSize);
static FILE* openFile(const char *filename, const char *mode);
static long getFileSize(FILE *file);
//static unsigned char nextSequence(unsigned char seq);



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
    printf("CONNECTION CLOSED\n");
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

    //  Cria e envia pacotes
    //unsigned char seq = 0;
    unsigned char buffer[256];
    int countBytesReaded = 0;

    while ((countBytesReaded = fread(buffer, sizeof(unsigned char), sizeof(buffer), file)) > 0) {
        // Cria o pacote de dados com os bytes lidos
        //printf("Criando data packet com %d bytes...\n", countBytesReaded);
        unsigned char *dataPacket = createDataPacket(buffer, countBytesReaded);
        if (!dataPacket) {
            perror("createDataPacket failed");
            return -1;
        }

        printf("Enviando data packet...\n");
        if (llwrite(dataPacket, countBytesReaded + 3) < 0) {
            free(dataPacket);
            return -1;
        }
        //seq = nextSequence(seq);  // Atualiza a sequência
        free(dataPacket);
    }

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

    unsigned char buffer[518];
    int packetSize;

    // Recebe pacotes até o final do arquivo
    while ((packetSize = llread(buffer)) > 0) {
        if (buffer[0] == 0x01) {  // Verifica se é um pacote de dados
            fwrite(buffer + 3, sizeof(unsigned char), packetSize - 3, file);
        } else if (buffer[0] == 0x03) {  // Pacote de controle final
            break;
        }
    }

    fclose(file);  // Fecha o arquivo após a recepção completa
    return packetSize < 0 ? -1 : 0;
}


static int sendControlPacket(unsigned char controlType, const char *filename, long fileSize) {
    int filenameSize = strlen(filename);
    /* Allocate exact TLV size:
       1 byte C + [1 type + 1 length + sizeof(long) value] + [1 type + 1 length + filenameSize value]
    */
    int totalSize = 1 + (1 + 1 + sizeof(long)) + (1 + 1 + filenameSize);
    unsigned char *packet = malloc(totalSize);
    if (!packet) return -1;
    int index = 0;

    packet[index++] = controlType;

    // File Size TLV: Type, Length, Value
    packet[index++] = 0;                     // type = 0 (file size)
    packet[index++] = sizeof(long);          // length
    for (int i = sizeof(long) - 1; i >= 0; i--) {
        packet[index++] = (fileSize >> (8 * i)) & 0xFF;
    }

    // File name TLV structure: Type, Length, Value
    packet[index++] = 1;
    packet[index++] = filenameSize;
    memcpy(packet + index, filename, filenameSize);
    index += filenameSize;

    if (llwrite(packet, index) < 0) {
        free(packet);
        return -1;
    }

    free(packet);
    return 0;
}

// Cria um pacote de dados
static unsigned char* createDataPacket(unsigned char *buffer, int bufferSize) {
    //printf("DEBUG (createDataPacket): malloc de %d bytes para o data packet\n", bufferSize + 3);
    unsigned char* dataPacket = (unsigned char*)malloc(bufferSize + 1000);

    int index = 0;
    dataPacket[index++] = 0x01;                      //C
    dataPacket[index++] = (bufferSize >> 8) & 0xFF;  //L2
    dataPacket[index++] = bufferSize & 0xFF;         //L1
    memcpy(&dataPacket[index], buffer, bufferSize);  // Adiciona os dados ao pacote

    return dataPacket;
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



/*static unsigned char nextSequence(unsigned char seq) {
    return (seq + 1) % 256; //Numero de sequencia entre 0 e 255
}*/
