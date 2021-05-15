/* Por Prof. Daniel Batista <batista@ime.usp.br>
 * Em 4/4/2021
 * 
 * Um código simples de um servidor de eco a ser usado como base para
 * o EP1. Ele recebe uma linha de um cliente e devolve a mesma linha.
 * Teste ele assim depois de compilar:
 * 
 * ./ep1-servidor-exemplo 8000
 * 
 * Com este comando o servidor ficará escutando por conexões na porta
 * 8000 TCP (Se você quiser fazer o servidor escutar em uma porta
 * menor que 1024 você precisará ser root ou ter as permissões
 * necessáfias para rodar o código com 'sudo').
 *
 * Depois conecte no servidor via telnet. Rode em outro terminal:
 * 
 * telnet 127.0.0.1 8000
 * 
 * Escreva sequências de caracteres seguidas de ENTER. Você verá que o
 * telnet exibe a mesma linha em seguida. Esta repetição da linha é
 * enviada pelo servidor. O servidor também exibe no terminal onde ele
 * estiver rodando as linhas enviadas pelos clientes.
 * 
 * Obs.: Você pode conectar no servidor remotamente também. Basta
 * saber o endereço IP remoto da máquina onde o servidor está rodando
 * e não pode haver nenhum firewall no meio do caminho bloqueando
 * conexões na porta escolhida.
 */

#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <string.h>
#include <netdb.h>
#include <sys/types.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <time.h>
#include <unistd.h>

#include <fcntl.h>

#define LISTENQ 1
#define MAXDATASIZE 100
#define MAXLINE 4096

#define PACKET_TYPE 0
#define PACKET_REM_LEN 1

#define ID_LEN 10

enum mqttPacketType {
    CONNECT = 1,
    CONNACK,
    PUBLISH,
    PUBACK,
    PUBREC,
    PUBREL,
    PUBCOMP,
    SUBSCRIBE,
    SUBACK,
    UNSUBSCRIBE,
    UNSUBACK,
    PINGREQ,
    PINGRESP,
    DISCONNECT
};

struct mqttPacketFixedHeader {
    uint8_t packetType;
    uint8_t remainingLength;
};

struct mqttResponsePacket {
    char *packet;
    int packetLength;
};

struct mqttPacketFixedHeader *currentPacketInfo;

void getPacketInfo(char headerFirstByte, char headerSecondByte) {
    currentPacketInfo->packetType = (uint8_t) headerFirstByte >> 4;
    currentPacketInfo->remainingLength = (uint8_t) headerSecondByte;
}

char *generateClientId(){
    char *id;

    id = malloc(sizeof(char) * (ID_LEN + 1));
    srand(time(NULL));
    for (int i = 0; i < ID_LEN; i++) {
        id[i] = (char) (97 + (rand() % 26));
    }
    id[ID_LEN] = 0;
    return id;
}

struct mqttResponsePacket *formatConnackResponse() {
    enum connackPacketBytesDisposition {
        ACK_FLAGS_POS = 2,
        REASON_CODE_POS,
        PROPERTIES_LEN_POS,
        ASSIGN_CLIENT_IDENTIFIER_POS,
        ID_LEN_POS_1,
        ID_LEN_POS_2,
        ID_POS
    };
    const int SUCCESS_REASON_CODE = 0x00, SUCCESS_ACK_FLAGS = 0, 
        ASSIGN_CLIENT_IDENTIFIER_CODE = 0x12, 
        PROPERTIES_LEN = 3 + ID_LEN, PACKET_LEN = 2 + 2 + 1 + PROPERTIES_LEN;
    char packet[PACKET_LEN];
    struct mqttResponsePacket *response;

    // Fixed header do pacote de tipo CONNACK 
    packet[PACKET_TYPE] = CONNACK << 4;
    packet[PACKET_REM_LEN] = PACKET_LEN - 2;

    // Variable header do pacote de tipo CONNACK
    packet[ACK_FLAGS_POS] = SUCCESS_ACK_FLAGS;
    packet[REASON_CODE_POS] = SUCCESS_REASON_CODE;
    
    // Properties do pacote do tipo CONNACK
    packet[PROPERTIES_LEN_POS] = PROPERTIES_LEN;
    packet[ASSIGN_CLIENT_IDENTIFIER_POS] = ASSIGN_CLIENT_IDENTIFIER_CODE;
    packet[ID_LEN_POS_1] = 0; packet[ID_LEN_POS_2] = ID_LEN;
    strncpy(packet + ID_POS, generateClientId(), ID_LEN);

    response = malloc(sizeof(struct mqttResponsePacket));
    response->packet = packet; response->packetLength = PACKET_LEN;
    return response;
}

struct mqttResponsePacket *handleConnectRequest(char *packet) {
    char protocolName[4];
    int protocolVersion;

    strncpy(protocolName, packet + 4, sizeof(protocolName));
    protocolVersion = (int) packet[8];
    printf("protocol %s version %d\n", protocolName, protocolVersion);
    return formatConnackResponse();
}

void handlePublishRequest(char *packet) {
    enum publishPacketBytesDisposition {
        TOPIC_LEN_POS_1 = 2,
        TOPIC_LEN_POS_2,
        TOPIC_POS,
    };
    int topicLen, messageLen, messagePos;
    char *topic, *message;

    topicLen = (int)packet[TOPIC_LEN_POS_1] + (int)packet[TOPIC_LEN_POS_2];
    topic = malloc(sizeof(char) * (topicLen + 1));
    strncpy(topic, packet + TOPIC_POS, topicLen); topic[topicLen] = '\0';

    messageLen = currentPacketInfo->remainingLength - 2 - topicLen - 1; 
    message = malloc(sizeof(char) * (messageLen + 1));
    messagePos = currentPacketInfo->remainingLength - messageLen + 2;
    strncpy(message, packet + messagePos, messageLen); message[messageLen] = '\0';

    printf("topic '%s', message len %d and message '%s' and messagePos %d \n", topic, messageLen, message, messagePos);
}

struct mqttResponsePacket *formatSubackResponse(short int messageIdentifier) {
    enum subackPacketBytesDisposition {
        MSG_IDENTIFIER_POS_1 = 2,
        MSG_IDENTIFIER_POS_2,
        PROPERTIES_LEN_POS,
        REASON_CODE_POS,
    };
    const int SUCCESS_REASON_CODE = 0x00, PACKET_LEN = 6;
    char packet[PACKET_LEN];
    struct mqttResponsePacket *response;

    // Fixed header do pacote de tipo SUBACK 
    packet[PACKET_TYPE] = SUBACK << 4;
    packet[PACKET_REM_LEN] = PACKET_LEN - 2;

    // Variable header do pacote de tipo SUBACK
    messageIdentifier = htons(messageIdentifier);
    memcpy(packet + MSG_IDENTIFIER_POS_1, (void *) &messageIdentifier, 2);
    packet[PROPERTIES_LEN_POS] = 0;

    // Payload do pacote de tipo SUBACK
    packet[REASON_CODE_POS] = SUCCESS_REASON_CODE;

    response = malloc(sizeof(struct mqttResponsePacket));
    response->packet = packet; response->packetLength = PACKET_LEN;
    return response;
}

struct mqttResponsePacket *handleSubscribeRequest(char *packet) {
    enum subscribePacketBytesDisposition {
        MSG_IDENTIFIER_POS_1 = 2,
        MSG_IDENTIFIER_POS_2,
        PROPERTIES_LEN_POS,
        TOPIC_LEN_POS_1,
        TOPIC_LEN_POS_2,
        TOPIC_POS,
    };
    int topicLen, messageIdentifier;
    char *topic;

    messageIdentifier = (int)packet[MSG_IDENTIFIER_POS_1] + (int)packet[MSG_IDENTIFIER_POS_2];
    
    topicLen = (int)packet[TOPIC_LEN_POS_1] + (int)packet[TOPIC_LEN_POS_2];
    topic = malloc(sizeof(char) * (topicLen + 1));
    strncpy(topic, packet + TOPIC_POS, topicLen); topic[topicLen] = '\0';

    printf("topic '%s', message identifier %d\n", topic, messageIdentifier);
    return formatSubackResponse(messageIdentifier);
}

struct mqttResponsePacket *handlePingRequest() {
    struct mqttResponsePacket *response;
    char packet[2];

    // Fixed header do pacote de tipo PINGRESP 
    packet[PACKET_TYPE] = PINGRESP << 4;
    packet[PACKET_REM_LEN] = 0;

    response = malloc(sizeof(struct mqttResponsePacket));
    response->packet = packet; response->packetLength = 2;
    return response;
}

int main (int argc, char **argv) {
    /* Os sockets. Um que será o socket que vai escutar pelas conexões
     * e o outro que vai ser o socket específico de cada conexão */
    int listenfd, connfd;
    /* Informações sobre o socket (endereço e porta) ficam nesta struct */
    struct sockaddr_in servaddr;
    /* Retorno da função fork para saber quem é o processo filho e
     * quem é o processo pai */
    pid_t childpid;
    /* Armazena linhas recebidas do cliente */
    char recvline[MAXLINE + 1];
    /* Armazena o tamanho da string lida do cliente */
    ssize_t n;
   
    if (argc != 2) {
        fprintf(stderr,"Uso: %s <Porta>\n",argv[0]);
        fprintf(stderr,"Vai rodar um servidor de echo na porta <Porta> TCP\n");
        exit(1);
    }

    /* Criação de um socket. É como se fosse um descritor de arquivo.
     * É possível fazer operações como read, write e close. Neste caso o
     * socket criado é um socket IPv4 (por causa do AF_INET), que vai
     * usar TCP (por causa do SOCK_STREAM), já que o MQTT funciona sobre
     * TCP, e será usado para uma aplicação convencional sobre a Internet
     * (por causa do número 0) */
    if ((listenfd = socket(AF_INET, SOCK_STREAM, 0)) == -1) {
        perror("socket :(\n");
        exit(2);
    }

    /* Agora é necessário informar os endereços associados a este
     * socket. É necessário informar o endereço / interface e a porta,
     * pois mais adiante o socket ficará esperando conexões nesta porta
     * e neste(s) endereços. Para isso é necessário preencher a struct
     * servaddr. É necessário colocar lá o tipo de socket (No nosso
     * caso AF_INET porque é IPv4), em qual endereço / interface serão
     * esperadas conexões (Neste caso em qualquer uma -- INADDR_ANY) e
     * qual a porta. Neste caso será a porta que foi passada como
     * argumento no shell (atoi(argv[1]))
     */
    bzero(&servaddr, sizeof(servaddr));
    servaddr.sin_family      = AF_INET;
    servaddr.sin_addr.s_addr = htonl(INADDR_ANY);
    servaddr.sin_port        = htons(atoi(argv[1]));
    if (bind(listenfd, (struct sockaddr *)&servaddr, sizeof(servaddr)) == -1) {
        perror("bind :(\n");
        exit(3);
    }

    /* Como este código é o código de um servidor, o socket será um
     * socket passivo. Para isto é necessário chamar a função listen
     * que define que este é um socket de servidor que ficará esperando
     * por conexões nos endereços definidos na função bind. */
    if (listen(listenfd, LISTENQ) == -1) {
        perror("listen :(\n");
        exit(4);
    }

    printf("[Servidor no ar. Aguardando conexões na porta %s]\n",argv[1]);
    printf("[Para finalizar, pressione CTRL+c ou rode um kill ou killall]\n");
   
    /* O servidor no final das contas é um loop infinito de espera por
     * conexões e processamento de cada uma individualmente */
	for (;;) {
        /* O socket inicial que foi criado é o socket que vai aguardar
         * pela conexão na porta especificada. Mas pode ser que existam
         * diversos clientes conectando no servidor. Por isso deve-se
         * utilizar a função accept. Esta função vai retirar uma conexão
         * da fila de conexões que foram aceitas no socket listenfd e
         * vai criar um socket específico para esta conexão. O descritor
         * deste novo socket é o retorno da função accept. */
        if ((connfd = accept(listenfd, (struct sockaddr *) NULL, NULL)) == -1 ) {
            perror("accept :(\n");
            exit(5);
        }
      
        /* Agora o servidor precisa tratar este cliente de forma
         * separada. Para isto é criado um processo filho usando a
         * função fork. O processo vai ser uma cópia deste. Depois da
         * função fork, os dois processos (pai e filho) estarão no mesmo
         * ponto do código, mas cada um terá um PID diferente. Assim é
         * possível diferenciar o que cada processo terá que fazer. O
         * filho tem que processar a requisição do cliente. O pai tem
         * que voltar no loop para continuar aceitando novas conexões.
         * Se o retorno da função fork for zero, é porque está no
         * processo filho. */
        if ( (childpid = fork()) == 0) {
            /**** PROCESSO FILHO ****/
            printf("[Uma conexão aberta]\n");
            /* Já que está no processo filho, não precisa mais do socket
             * listenfd. Só o processo pai precisa deste socket. */
            close(listenfd);
         
            /* Agora pode ler do socket e escrever no socket. Isto tem
             * que ser feito em sincronia com o cliente. Não faz sentido
             * ler sem ter o que ler. Ou seja, neste caso está sendo
             * considerado que o cliente vai enviar algo para o servidor.
             * O servidor vai processar o que tiver sido enviado e vai
             * enviar uma resposta para o cliente (Que precisará estar
             * esperando por esta resposta) 
             */

            /* ========================================================= */
            /* ========================================================= */
            /*                         EP1 INÍCIO                        */
            /* ========================================================= */
            /* ========================================================= */
            /* TODO: É esta parte do código que terá que ser modificada
             * para que este servidor consiga interpretar comandos MQTT  */
            struct mqttResponsePacket *response;
            currentPacketInfo = malloc(sizeof(struct mqttPacketFixedHeader));
            
            int flags = fcntl(connfd, F_GETFL, 0); fcntl(connfd, F_SETFL, flags | O_NONBLOCK);
            int isClientConnected = 1;

            while (isClientConnected) {
                if ((n=read(connfd, recvline, MAXLINE)) > 0) {
                    getPacketInfo(recvline[PACKET_TYPE], recvline[PACKET_REM_LEN]);
                    recvline[n]=0;

                    switch (currentPacketInfo->packetType) {
                        case CONNECT:
                            response = handleConnectRequest(recvline);
                            break;

                        case PUBLISH:
                            handlePublishRequest(recvline);
                            break;

                        case SUBSCRIBE:
                            response = handleSubscribeRequest(recvline);
                            break;

                        case PINGREQ:
                            response = handlePingRequest();
                            break;

                        case DISCONNECT:
                            isClientConnected = 0;
                            break;

                    }
                    if (response != NULL) {
                        write(connfd, response->packet, response->packetLength);
                        free(response);
                    }
                }
            }
            /* ========================================================= */
            /* ========================================================= */
            /*                         EP1 FIM                           */
            /* ========================================================= */
            /* ========================================================= */

            /* Após ter feito toda a troca de informação com o cliente,
             * pode finalizar o processo filho */
            printf("[Uma conexão fechada]\n");
            exit(0);
        }
        else
            /**** PROCESSO PAI ****/
            /* Se for o pai, a única coisa a ser feita é fechar o socket
             * connfd (ele é o socket do cliente específico que será tratado
             * pelo processo filho) */
            close(connfd);
    }
    exit(0);
}
