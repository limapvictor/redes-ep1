# EP1 de Redes - MQTT Server
* ### Instalação
    Para compilar o MQTT Server em um terminal **GNU/Linux** com o *GCC compiler*, entre na pasta em que o código fonte está localizado
    e rode o comando :
    ```shell
    make
    ```
    *Caso você não possua o GCC instalado em seu computador, instale usando o comando:*
    ```shell
    sudo apt install build-essential
    ```
* ### Execução 
    Para rodar o MQTT Server em seu computador, entre na pasta de instalação do mesmo e rode o comando:
    ```shell
    ./mqtt-server <port number>
    ```
    Com isso, o servidor escutará por requisições na porta especificada. Como estamos trabalhando com MQTT,
    recomenda-se usar a porta 1883, padrão do protocolo. Com isso, não será necessário especificar a porta nos clientes,
    além de que o *debug* por meio do [Wireshark](https://www.wireshark.org/) será facilitado.

    O servidor implementado é uma versão *lite* de um Mosquitto, suportando os comandos básicos do MQTT, sem inclusão de flags 
    como QOS e outras. Com ele, basicamente se consegue conectar/desconectar no servidor e publicar/ouvir mensagens em um tópico específico.
    O servidor foi desenvolvido se observando a utilização dos dois comandos abaixos presentes na biblioteca *Mosquitto Clients*, então se recomenda a utilização dele com os mesmos comandos: 
    ```shell
    mosquitto_pub -d -h <server machine IP> -t <topic> -m "<message>" -V mqttv5
    ```
    ```shell
    mosquitto_sub -v -d -h <server machine IP> -t <topic> -V mqttv5
    ```
    *Caso você não possua a biblioteca Mosquitto Clients instalada em seu computador, instale usando o comando:*
     ```shell
    sudo apt install mosquitto-clients
    ```
* ### Remoção
    Para remover o MQTT Server de seu computador, entre na pasta em que o programa está instalado, pare a execução 
    do mesmo caso (se estiver rodando) e rode o comando:
    ```shell
    make clean
    ```