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
#include <dirent.h>
#include <sys/un.h>


#define LISTENQ 1
#define MAXDATASIZE 100
#define MAXLINE 4096

#define PACKET_TYPE_POS 0
#define PACKET_REM_LEN_POS 1

#define ID_LEN 10
#define MAXPATH 256

const char SERVER_DIR[] = "/tmp/mqtt-server/";

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

struct mqttPacketFixedHeader currentPacketInfo;
struct mqttResponsePacket response;
char *clientId;

int subscriberfd;
char *subscribedTopic;

void getPacketInfo(char headerFirstByte, char headerSecondByte) {
    currentPacketInfo.packetType = (uint8_t) headerFirstByte >> 4;
    currentPacketInfo.remainingLength = (uint8_t) headerSecondByte;
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

void formatConnackResponse() {
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
        PROPERTIES_LEN = 3 + ID_LEN, PACKET_LEN = ID_POS + ID_LEN;
    short int idLenNet;
    char packet[PACKET_LEN];

    // Fixed header do pacote de tipo CONNACK 
    packet[PACKET_TYPE_POS] = CONNACK << 4;
    packet[PACKET_REM_LEN_POS] = PACKET_LEN - 2;

    // Variable header do pacote de tipo CONNACK
    packet[ACK_FLAGS_POS] = SUCCESS_ACK_FLAGS;
    packet[REASON_CODE_POS] = SUCCESS_REASON_CODE;
    
    // Properties do pacote do tipo CONNACK
    packet[PROPERTIES_LEN_POS] = PROPERTIES_LEN;
    packet[ASSIGN_CLIENT_IDENTIFIER_POS] = ASSIGN_CLIENT_IDENTIFIER_CODE;
    idLenNet = htons(ID_LEN); 
    memcpy(&packet[ID_LEN_POS_1], &idLenNet, 2);
    clientId = generateClientId();
    strncpy(&packet[ID_POS], clientId, ID_LEN);

    response.packet = packet; response.packetLength = PACKET_LEN;
}

void handleConnectRequest(char *packet) {
    char protocolName[4];
    uint8_t protocolVersion;

    strncpy(protocolName, &packet[4], sizeof(protocolName));
    protocolVersion = (uint8_t) packet[8];
    printf("protocol %s version %d\n", protocolName, protocolVersion);
    formatConnackResponse();
}

void sentMessageToSubscribers(char *message, int messageLen, char *topic, int topicLen) {
    DIR *serverDir;
    struct dirent *currentFile;
    int sockfd;
    struct sockaddr_un subscriberaddr;
    char subscriberPath[MAXPATH + 1];

    serverDir = opendir(SERVER_DIR);
    if (serverDir) {
        while ((currentFile = readdir(serverDir)) != NULL) {
            if (strncmp(currentFile->d_name, topic, topicLen) == 0) {
                if ((sockfd = socket(AF_LOCAL, SOCK_STREAM, 0)) < 0)
                    continue;
                
                bzero(&subscriberaddr, sizeof(subscriberaddr));
                subscriberaddr.sun_family = AF_UNIX;
                memcpy(subscriberPath, SERVER_DIR, strlen(SERVER_DIR) + 1); 
                strcat(subscriberPath, currentFile->d_name); 
                subscriberPath[strlen(SERVER_DIR) + strlen(currentFile->d_name)] = '\0';
                strncpy(subscriberaddr.sun_path, subscriberPath, strlen(subscriberPath));

                if (connect(sockfd, (struct sockaddr *) &subscriberaddr, sizeof(subscriberaddr)) < 0)
                    continue;

                write(sockfd, message, messageLen);
            }
        }
        closedir(serverDir);
    }
}

void handlePublishRequest(char *packet) {
    enum publishPacketBytesDisposition {
        TOPIC_LEN_POS_1 = 2,
        TOPIC_LEN_POS_2,
        TOPIC_POS,
    };
    short int topicLen, messageLen, messagePos;
    char *topic, *message;

    //Decifrando topic do pacote do tipo PUBLISH
    memcpy(&topicLen, &packet[TOPIC_LEN_POS_1], 2);
    topicLen = ntohs(topicLen);
    topic = malloc(sizeof(char) * (topicLen + 1));
    strncpy(topic, &packet[TOPIC_POS], topicLen); topic[topicLen] = '\0';

    //Decifrando message do pacote do tipo PUBLISH
    messagePos = TOPIC_POS + topicLen + 1;
    messageLen = currentPacketInfo.remainingLength - messagePos + 2; 
    message = malloc(sizeof(char) * (messageLen + 1));
    strncpy(message, &packet[messagePos], messageLen); message[messageLen] = '\0';

    printf("topic '%s', message len %d and message '%s' and messagePos %d \n", topic, messageLen, message, messagePos);
    sentMessageToSubscribers(message, messageLen, topic, topicLen);
}

char *getSubscriberWatcherPath(char *topic, int topicLen) {
    char *path;

    path = malloc(sizeof(char) * (MAXPATH + 1));
    memcpy(path, SERVER_DIR, strlen(SERVER_DIR));
    strcat(path, topic); strcat(path, "_"); strcat(path, clientId);
    path[strlen(SERVER_DIR) + topicLen + 1 + ID_LEN] = '\0';
    return path; 
}

void createTopicWatcherForSubscriber(char *topic, int topicLen) {
    struct sockaddr_un subscriberaddr;
    char *watcherPath;

    if ((subscriberfd = socket(AF_UNIX, SOCK_STREAM | SOCK_NONBLOCK, 0)) < 0)
        perror("Error on sub sock: \n");

    watcherPath = getSubscriberWatcherPath(topic, topicLen);
    unlink(watcherPath);

    bzero(&subscriberaddr, sizeof(subscriberaddr));
    subscriberaddr.sun_family = AF_UNIX;
    strncpy(subscriberaddr.sun_path, watcherPath, strlen(watcherPath));

    if (bind(subscriberfd, (struct sockaddr *) &subscriberaddr, sizeof(subscriberaddr)) == -1)
        perror("Error on sub bind: \n");

    if (listen(subscriberfd, 20) == -1)
        printf("Error on sub listen: \n");
    printf("Socket created !\n");
}

void formatSubackResponse(short int messageIdentifier) {
    enum subackPacketBytesDisposition {
        MSG_IDENTIFIER_POS_1 = 2,
        MSG_IDENTIFIER_POS_2,
        PROPERTIES_LEN_POS,
        REASON_CODE_POS,
    };
    const int SUCCESS_REASON_CODE = 0x00, PACKET_LEN = REASON_CODE_POS + 1;
    char packet[PACKET_LEN];

    // Fixed header do pacote de tipo SUBACK 
    packet[PACKET_TYPE_POS] = SUBACK << 4;
    packet[PACKET_REM_LEN_POS] = PACKET_LEN - 2;

    // Variable header do pacote de tipo SUBACK
    messageIdentifier = htons(messageIdentifier);
    memcpy(&packet[MSG_IDENTIFIER_POS_1], &messageIdentifier, 2);
    packet[PROPERTIES_LEN_POS] = 0;

    // Payload do pacote de tipo SUBACK
    packet[REASON_CODE_POS] = SUCCESS_REASON_CODE;

    response.packet = packet; response.packetLength = PACKET_LEN;
}

void handleSubscribeRequest(char *packet) {
    enum subscribePacketBytesDisposition {
        MSG_IDENTIFIER_POS_1 = 2,
        MSG_IDENTIFIER_POS_2,
        PROPERTIES_LEN_POS,
        TOPIC_LEN_POS_1,
        TOPIC_LEN_POS_2,
        TOPIC_POS,
    };
    short int topicLen, messageIdentifier;
    char *topic;

    //Decifrando message identifier do pacote do tipo SUBSCRIBE
    memcpy(&messageIdentifier, &packet[MSG_IDENTIFIER_POS_1], 2);
    messageIdentifier = ntohs(messageIdentifier);
    
    //Decifrando topic do pacote do tipo SUBSCRIBE
    memcpy(&topicLen, &packet[TOPIC_LEN_POS_1], 2);
    topicLen = ntohs(topicLen);
    topic = malloc(sizeof(char) * (topicLen + 1));
    strncpy(topic, &packet[TOPIC_POS], topicLen); topic[topicLen] = '\0';
    subscribedTopic = topic;

    printf("topic '%s' with '%d bytes, message identifier %d\n", topic, topicLen, messageIdentifier);
    createTopicWatcherForSubscriber(topic, topicLen);
    formatSubackResponse(messageIdentifier);
}

void formatPublishResponse(char message[]) {
    char packet[MAXLINE + 1];
    const int TOPIC_LEN_POS_1 = 2, TOPIC_POS = 4; 
    int PROPERTIES_LEN_POS, MESSAGE_POS;
    short int topicLen, topicLenNet, messageLen, packetLen;

    //Variable header com o topic do pacote de tipo PUBLISH
    topicLen = strlen(subscribedTopic); topicLenNet = htons(topicLen);
    memcpy(&packet[TOPIC_LEN_POS_1], &topicLenNet, 2);
    memcpy(&packet[TOPIC_POS], subscribedTopic, topicLen);

    PROPERTIES_LEN_POS = TOPIC_POS + topicLen;
    packet[PROPERTIES_LEN_POS] = 0;

    //Payload com a message do pacote de tipo PUBLISH
    MESSAGE_POS = PROPERTIES_LEN_POS + 1;
    messageLen = strlen(message);
    strncpy(&packet[MESSAGE_POS], message, messageLen);

    //Fixed header do pacote de tipo PUBLISH
    packetLen = MESSAGE_POS + messageLen;
    packet[PACKET_TYPE_POS] = PUBLISH << 4;
    packet[PACKET_REM_LEN_POS] = packetLen - 2;
    printf("message in packet with %d: %d bytes as %s\n", packetLen, messageLen, message);

    response.packet = packet; response.packetLength = packetLen;
}

void getNewMessageFromTopic(int subscriberClientfd) {
    int connfd;
    char message[MAXLINE + 1];
    ssize_t n;

    if ((connfd = accept(subscriberfd, (struct sockaddr *) NULL, NULL)) >= 0) {
        if ((n=read(connfd, message, MAXLINE)) > 0) {
            message[n] = '\0';
            formatPublishResponse(message);
            write(subscriberClientfd, response.packet, response.packetLength);
        }
        close(connfd);
    }
}

void handlePingRequest() {
    char packet[2];

    // Fixed header do pacote de tipo PINGRESP 
    packet[PACKET_TYPE_POS] = PINGRESP << 4;
    packet[PACKET_REM_LEN_POS] = 0;

    response.packet = packet; response.packetLength = 2;
}

void handleDisconnectRequest(short int isSubscriber) {
    if (isSubscriber) {
        close(subscriberfd);
        unlink(getSubscriberWatcherPath(subscribedTopic, strlen(subscribedTopic)));
    }
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
            int flags = fcntl(connfd, F_GETFL, 0); fcntl(connfd, F_SETFL, flags | O_NONBLOCK);
            short int isClientConnected = 1, isSubscriber = 0;

            while (isClientConnected) {
                if ((n=read(connfd, recvline, MAXLINE)) > 0) {
                    getPacketInfo(recvline[PACKET_TYPE_POS], recvline[PACKET_REM_LEN_POS]);
                    recvline[n]=0;

                    switch (currentPacketInfo.packetType) {
                        case CONNECT:
                            handleConnectRequest(recvline);
                            write(connfd, response.packet, response.packetLength);
                            break;

                        case PUBLISH:
                            handlePublishRequest(recvline);
                            break;

                        case SUBSCRIBE:
                            handleSubscribeRequest(recvline);
                            isSubscriber = 1;
                            write(connfd, response.packet, response.packetLength);
                            break;

                        case PINGREQ:
                            handlePingRequest();
                            write(connfd, response.packet, response.packetLength);
                            break;

                        case DISCONNECT:
                            handleDisconnectRequest(isSubscriber);
                            isClientConnected = 0;
                            break;

                    }
                }

                if (isSubscriber) 
                    getNewMessageFromTopic(connfd);
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
