// Link layer protocol implementation

#include "link_layer.h"
#include "serial_port.h"

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <signal.h>

// MISC
#define _POSIX_SOURCE 1 // POSIX compliant source

#define MAX_FRAME_SIZE 2048 // Tamanho maximo de cada frame

LinkLayerRole currentRole;

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


int timeout,retransmissions;         
int alarmEnabled = 0;            
int alarmCount = 0;

void alarmHandler(int signal) {
    alarmEnabled = 1;
    alarmCount++;
}

// Variavel utilizado para controlar erros durante a transferencia de DATA
int errorControl = 0;

////////////////////////////////////////////////
// LLOPEN
////////////////////////////////////////////////
int llopen(LinkLayer config)
{
    LinkLayerState state = START;
    if (openSerialPort(config.serialPort, config.baudRate) < 0) return -1;

    unsigned char byte;
    timeout = config.timeout;
    retransmissions = config.nRetransmissions;
    /* install SIGALRM handler so alarmEnabled is set when timeout expires */
    signal(SIGALRM, alarmHandler);
    
    switch(config.role) {
        case(LlTx): {
            while (config.nRetransmissions != 0 && state != STOP_R) {
                sendSupervisionFrame(A_TRANSMITTER, C_SET);
                alarm(config.timeout);
                alarmEnabled = 0;

                while (alarmEnabled == 0 && state != STOP_R) {
                    int res = readByteSerialPort(&byte);
                    if (res > 0) {
                        switch (state) {
                            case START:
                                if (byte == FLAG) {
                                    state = FLAG_RCV;
                                }
                                break;
                            case FLAG_RCV:
                                if (byte == A_RECEIVER) {
                                    state = A_RCV;
                                }
                                else if (byte != FLAG) state = START;
                                break;
                            case A_RCV:
                                if (byte == C_UA) {
                                    state = C_RCV;
                                }
                                else if (byte == FLAG) state = FLAG_RCV;
                                else state = START;
                                break;
                            case C_RCV:
                                if (byte == (A_RECEIVER ^ C_UA)) {
                                    state = BCC1_OK;
                                }
                                else if (byte == FLAG) state = FLAG_RCV;
                                else state = START;
                                break;
                            case BCC1_OK:
                                if (byte == FLAG) {
                                    state = STOP_R;
                                }
                                else state = START;
                                break;
                            case DATA:
                                break;
                            default: 
                                break;
                        }
                    }
                } 
                config.nRetransmissions--;
            }
            if (state != STOP_R) {
                printf("DEBUG (llopen): Erro ao estabelecer ligação, número de tentativas esgotado\n");
                return -1;
            }
            printf("DEBUG (llopen): Ligação estabelecida com sucesso\n");
            currentRole = LlTx;
            break;  
        }

        case(LlRx): {
            while (state != STOP_R)
            {
                int res = readByteSerialPort(&byte);
                if (res <= 0) continue;
                switch (state)
                {
                    case START:
                        if (byte == FLAG) state = FLAG_RCV;
                        break;
                    case FLAG_RCV:
                        if (byte == A_TRANSMITTER) state = A_RCV;
                        else if (byte != FLAG) state = START;
                        break;
                    case A_RCV:
                        if (byte == C_SET) state = C_RCV;
                        else if (byte == FLAG) state = FLAG_RCV;
                        else state = START;
                        break;
                    case C_RCV:
                        if (byte == (A_TRANSMITTER ^ C_SET)) {
                            state = BCC1_OK;
                        }
                        else if (byte == FLAG) state = FLAG_RCV;
                        else state = START;
                        break;
                    case BCC1_OK:
                        if (byte == FLAG) state = STOP_R;
                        else state = START;
                        break;
                    case DATA:
                        break;
                    case STOP_R:
                        break;
                }
            }
            sendSupervisionFrame(A_RECEIVER, C_UA);
            currentRole = LlRx;
            break;
        }
        default:
            return -1;
            break;
    }
    return 0;
}

////////////////////////////////////////////////
// LLWRITE
////////////////////////////////////////////////
int llwrite(const unsigned char *buf, int bufSize) {

    unsigned char frame[MAX_FRAME_SIZE];
    int frameIndex = 0;
    /* ensure SIGALRM handler is installed for retransmission timeouts */
    signal(SIGALRM, alarmHandler);
    
    frame[frameIndex++] = FLAG;
    frame[frameIndex++] = A_TRANSMITTER;
    frame[frameIndex++] = C_DATA;
    frame[frameIndex++] = A_TRANSMITTER ^ C_DATA;

    unsigned char BCC2 = getBCC2(buf, bufSize);
    frameIndex += byteStuffing(buf, bufSize, &frame[frameIndex]);
    frameIndex += byteStuffing(&BCC2, 1, &frame[frameIndex]);
    frame[frameIndex++] = FLAG;

    /*for (int i = 0; i < frameIndex; i++) {
        printf("DEBUG (llwrite): Frame[%d] = 0x%X\n", i, frame[i]);
    }*/
    
    int tentativas = 0;
    while (tentativas < retransmissions) {
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
        tentativas++;
        
    }

    
    printf("DEBUG (llwrite): Erro ao enviar frame/n");
    return -1;
}

////////////////////////////////////////////////
// LLREAD
////////////////////////////////////////////////
int llread(unsigned char *packet)
{
    LinkLayerState state = START;
    unsigned char frame[MAX_FRAME_SIZE];
    int frameIndex = 0;
    unsigned char byte;
    int tentativas = 0;

    /* ensure SIGALRM handler is installed for read timeouts */
    signal(SIGALRM, alarmHandler);

    //Loop que tenta receber o frame n vezes

    while(tentativas  < retransmissions)
    {
        alarmEnabled = 0;
        alarm(timeout);

        // Loop para ler bytes até receber um frame completo
        printf("DEBUG (llread): A aguardar frame...\n");
        while (!alarmEnabled && state != STOP_R) {
        if (readByteSerialPort(&byte) > 0) {
            switch (state)
            {
                case START:
                    if (byte == FLAG) {
                        state = FLAG_RCV;
                        //printf("DEBUG (llread): Transição para FLAG_RCV\n");
                    }
                    break;
                case FLAG_RCV:
                    if (byte == A_TRANSMITTER) {
                        state = A_RCV;
                        //printf("DEBUG (llread): Transição para A_RCV\n");
                    }
                    break;
                case A_RCV:
                    if (byte == C_DATA) {
                        state = C_RCV;
                        //printf("DEBUG (llread): Transição para C_RCV (Command_DATA)\n");
                    } else if (byte == C_DISC) {
                        //printf("DEBUG (llread): Command_DISC recebido, desconectando...\n");
                        return -2;
                    }
                    break;
                case C_RCV:
                    if (byte == (A_TRANSMITTER ^ C_DATA)) {
                        state = BCC1_OK;
                        //printf("DEBUG (llread): BCC1 OK, transição para DATA\n");
                    }
                    break;
                case BCC1_OK:
                    if (byte != FLAG) {
                        frame[frameIndex++] = byte;
                        state = DATA;
                        //printf("DEBUG (llread): Transição para DATA, dado recebido = 0x%X\n", byte);
                    }
                    break;
                case DATA:
                    if (byte == FLAG) {
                        state = STOP_R;
                        //printf("DEBUG (llread): FLAG de fim recebido, transição para STOP_R\n");
                    } else {
                        frame[frameIndex++] = byte;
                        //printf("DEBUG (llread): Dado adicionado ao frame = 0x%X\n", byte);
                    }
                    break;
                default:
                    state = START;
                    //printf("DEBUG (llread): Estado desconhecido, reiniciando para START\n");
                    break;
            }
        }
    }

    // Processa o frame se ele for corretamente recebido
    if(state == STOP_R) {
        int destuffedSize = byteDestuffing(frame, frameIndex, packet);
        unsigned char BCC2 = getBCC2(packet, destuffedSize - 1);

        //Verifica o BCC2 para garantir integridade dos dados
        if (BCC2 == packet[destuffedSize - 1]) {
            printf("DEBUG (llread): Frame recebido corretamente. Enviando RR...\n");
            if (errorControl == 0) {
                sendSupervisionFrame(A_RECEIVER, C_RR0);
            } else {
                sendSupervisionFrame(A_RECEIVER, C_RR1);
            }
            errorControl = (errorControl + 1) % 2;
            return destuffedSize - 1;
            } else {
                printf("DEBUG (llread): Erro: BCC2 incorreto. Enviando REJ...\n");
                if (errorControl == 0) {
                    sendSupervisionFrame(A_RECEIVER, C_REJ0);
                } else {
                    sendSupervisionFrame(A_RECEIVER, C_REJ1);
                }
                tentativas++;
                state = START;
                frameIndex = 0;
            }
        } else if (alarmEnabled) {
            tentativas++;
            tentativas < retransmissions ? printf("DEBUG (llread): Tempo de espera esgotado, tentando outra vez...\n") :
                printf("DEBUG (llread): Tentativas esgotadas\n");
            
            state = START;  
            frameIndex = 0;
        }
    }

    printf("DEBUG (llread): Não foi possivel receber o frame corretamente\n");
    exit(-1);
}

////////////////////////////////////////////////
// LLCLOSE
////////////////////////////////////////////////
int llclose() {
    LinkLayerState state = START;
    
    if (currentRole == LlTx) {
        signal(SIGALRM, alarmHandler);
        int tentativas = 0;

        while (tentativas < retransmissions && state != STOP_R) {
            printf("DEBUG (llclose): Enviando DISC...\n");
            sendSupervisionFrame(A_TRANSMITTER, C_DISC);
            alarm(timeout);
            alarmEnabled = 0;

            unsigned char byte;

            while (!alarmEnabled && state != STOP_R) {
                int res = readByteSerialPort(&byte);
                if (res > 0) {
                    switch (state) {
                        case START:
                            if (byte == FLAG) state = FLAG_RCV;
                            break;
                        case FLAG_RCV:
                            if (byte == A_RECEIVER) state = A_RCV;
                            else if (byte != FLAG) state = START;
                            break;
                        case A_RCV:
                            if (byte == C_DISC) state = C_RCV;
                            else if (byte == FLAG) state = FLAG_RCV;
                            else state = START;
                            break;
                        case C_RCV:
                            if (byte == (A_RECEIVER ^ C_DISC)) state = BCC1_OK;
                            else if (byte == FLAG) state = FLAG_RCV;
                            else state = START;
                            break;
                        case BCC1_OK:
                            if (byte == FLAG) state = STOP_R;
                            else state = START;
                            break;
                        default: 
                            break;
                    }
                }
            }
            if (state == STOP_R) {
                printf("DEBUG (llclose): DISC de confirmação recebido, enviando UA...\n");
                sendSupervisionFrame(A_TRANSMITTER, C_UA);
            } else {
                tentativas++;
                if (!(tentativas < retransmissions)) return -1;
            }
        }
    }
    else if (currentRole == LlRx) {
        signal(SIGALRM, alarmHandler);
        int tentativas = 0;

        while (tentativas < retransmissions && state != STOP_R) {
            alarm(timeout);
            alarmEnabled = 0;
            //printf("DEBUG (llclose): Aguardando DISC do transmissor (tentativa %d)...\n", tentativas + 1);

            // Loop interno para tentar ler bytes até timeout ou STOP_R
            while (!alarmEnabled && state != STOP_R) {
                unsigned char byte;
                if (readByteSerialPort(&byte) > 0) {
                    //printf("DEBUG (llclose): Byte recebido: 0x%02X\n", byte);
                    switch (state) {
                        case START:
                            if (byte == FLAG) state = FLAG_RCV;
                            //printf("DEBUG (llclose): Flag recebido, aguardando A...\n");
                            break;
                        case FLAG_RCV:
                            if (byte == A_TRANSMITTER) state = A_RCV;
                            //printf("DEBUG (llclose): A recebido, aguardando C...\n");
                            break;
                        case A_RCV:
                            if (byte == C_DISC) state = C_RCV;
                            //printf("DEBUG (llclose): C_DISC recebido, aguardando BCC1...\n");
                            break;
                        case C_RCV:
                            if (byte == (A_TRANSMITTER ^ C_DISC)) state = BCC1_OK;
                            //printf("DEBUG (llclose): BCC1 OK, aguardando FLAG...\n");
                            break;
                        case BCC1_OK:
                            if (byte == FLAG) state = STOP_R;
                            //printf("DEBUG (llclose): FLAG recebido, encerrando recepção\n");
                            break;
                        default:
                            break;
                    }
                }
            }
            if (alarmEnabled) {
                printf("DEBUG (llclose): Timeout na tentativa %d\n", tentativas + 1);
                tentativas++;
            }
        }
        
        if (state != STOP_R) {
            printf("DEBUG (llclose): Erro ao receber DISC, número de tentativas esgotado\n");
            return -1;
        }
        printf("DEBUG (llclose): DISC detectado, enviando DISC de confirmação...\n");
        sendSupervisionFrame(A_RECEIVER, C_DISC);
    }
    closeSerialPort();
    return 0;
}

void sendSupervisionFrame(unsigned char address, unsigned char control) {
    unsigned char frame[5] = {FLAG, address, control, address ^ control, FLAG};
    writeBytesSerialPort(frame, 5);
    //printf("DEBUG (sendSupervisionFrame): A enviar frame de controlo: 0x%X\n", control);
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
            //printf("DEBUG (byteStuffing): FLAG detectado, aplicando stuffing -> 0x7D + 0x5E\n");
        } 
        
        else if (input[i] == 0x7D) {
            output[stuffedIndex++] = 0x7D;
            output[stuffedIndex++] = 0x5D;
            //printf("DEBUG (byteStuffing): 0x7D detectado, aplicando stuffing -> 0x7D + 0x5D\n");
        } 
    
        else {
            output[stuffedIndex++] = input[i];
            //printf("DEBUG (byteStuffing): Byte sem alteração = 0x%X\n", input[i]);
        }
    }
    //printf("DEBUG (byteStuffing): Total de bytes adicionados no out buffer depois do stuffing = %d\n", stuffedIndex);
    return stuffedIndex;
}


int byteDestuffing(const unsigned char *input, int length, unsigned char *output) {

    int destuffedIndex = 0;   
    int finded = FALSE;             

    for (int i = 0; i < length; i++) {
        if (finded) {
            if (input[i] == 0x5E) {
                output[destuffedIndex++] = FLAG;
                //printf("DEBUG (byteDestuffing): 0x7D seguido de 0x5E, convertendo para FLAG\n");
            } 
            else if (input[i] == 0x5D) {
                output[destuffedIndex++] = 0x7D;
                //printf("DEBUG (byteDestuffing): 0x7D seguido de 0x5D, convertendo para 0x7D\n");
            }
            finded = FALSE;
        } 

        else if (input[i] == 0x7D) {
            finded = TRUE;    
            //printf("DEBUG (byteDestuffing): Byte 0x7D detectado, aguardando próximo byte\n");
        } 

        else if (input[i] == FLAG && i == length - 1) { 
            //printf("DEBUG (byteDestuffing): FLAG final detectado, parando processamento\n");
            break;
        } 

        else {
            output[destuffedIndex++] = input[i];
            //printf("DEBUG (byteDestuffing): Byte sem alteração = 0x%X\n", input[i]);
        }
    }
    //printf("DEBUG (byteDestuffing): Tamanho final após destuffing = %d\n", destuffedIndex);
    return destuffedIndex;
}

