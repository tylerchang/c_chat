#include <stdio.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <string.h>
#include <stdlib.h> // for strtol
#include <pthread.h>

int threadCondition = 1;

struct client_info
{
    int sockfd;
    char username[256];
};

int quit(int sockfd, char username[])
{
    char buf[4];
    buf[0] = 'Q';
    buf[1] = 'U';
    buf[2] = 'I';
    buf[3] = 'T';

    int bytesSent = send(sockfd, buf, 4, 0);
    threadCondition = -1;
    return 1;
}

void *send_thread(void *clnt)
{

    struct client_info *client;
    client = (struct client_info *)clnt;

    while (1)
    {
        char buf[256];
        char input[256];
        buf[0] = 'M';
        buf[1] = 'E';
        buf[2] = 'S';
        buf[3] = 'G';
        //Convert to 16 bit binary
        uint16_t lengthU = strlen(client->username);
        uint8_t highU = (lengthU >> 8) & 0xff;
        uint8_t lowU = lengthU & 0xff;
        buf[4] = highU;
        buf[5] = lowU;
        int int_lengthU = strlen(client->username);
        int current = 6;

        for (int i = 0; i < int_lengthU; i++)
        {
            buf[current] = client->username[i];
            current++;
        }
        fgets(input, 256, stdin);

        // check for quit statement
        char quit_message[] = "\\quit";
        if (strncmp(input, quit_message, 5) == 0)
        {
            quit(client->sockfd, client->username);
            return (void *)1;
        }

        uint16_t lengthM = strlen(input);
        uint8_t highM = (lengthM >> 8) & 0xff;
        uint8_t lowM = lengthM & 0xff;
        int int_lengthM = strlen(input);
        buf[current++] = highM;
        buf[current++] = lowM;
        int msgLength = strlen(input);

        for (int i = 0; i < msgLength; i++)
        {
            buf[current++] = input[i];
        }

        int bytesSent = send(client->sockfd, buf, current, 0);
        printf("\n");

        if (bytesSent != current)
        {
            printf("Could not send the message\n");
        }
    }
}

void *recv_thread(void *clnt)
{
    struct client_info *client;
    client = (struct client_info *)clnt;

    while (threadCondition == 1)
    {
        char buf[256];
        int bytesRecv = recv(client->sockfd, buf, 256, 0);
        int lengthOfUsername = buf[5]; //small endian
        int lengthOfMessage = buf[5 + lengthOfUsername + 2];

        char username[lengthOfUsername];
        char message[lengthOfMessage - 1];

        printf("\n");

        if (threadCondition == 1)
        {
            for (int i = 0; i < lengthOfUsername; i++)
            {
                username[i] = buf[6 + i];
                printf("%c", username[i]);
            }
            printf(" >> ");
            for (int i = 0; i < lengthOfMessage; i++)
            {
                message[i] = buf[8 + lengthOfUsername + i];
                printf("%c", message[i]);
            }
        }
    }
}

// Sends CNCT and receives ACKC, returns 1 if successfully, -1 if not
int send_CNCT(int sockfd, char username[])
{
    char buf[50];
    buf[0] = 'C';
    buf[1] = 'N';
    buf[2] = 'C';
    buf[3] = 'T';
    //Convert to 16 bit binary
    uint16_t length = strlen(username);
    uint8_t high = (length >> 8) & 0xff;
    uint8_t low = length & 0xff;
    buf[4] = high;
    buf[5] = low;
    int int_length = strlen(username);

    for (int i = 6; i < 6 + int_length; i++)
    {
        buf[i] = username[i - 6];
    }

    int bytesSent = send(sockfd, buf, 6 + int_length, 0);

    // If send() is a success, it should return the number of bytes sent
    // Check if the bytes returned is equal to the number of bytes we sent
    if (bytesSent == (6 + int_length))
    {
        //recieve the ACKC message from the server
        char ackc[4];

        char comp[] = "ACKC";
        int bytesRecv = recv(sockfd, ackc, 4, 0);
        ackc[4] = '\0';
        if (bytesRecv == 4 && strncmp(ackc, comp, 4) == 0)
        {
            printf("Connected to the server as %s\n", username);
            return 1;
        }
        else
        {
            return -1;
        }
    }

    return 0;
}

int main(int argc, char *argv[])
{
    int sockfd;                  // the socket to be connected to the server
    struct sockaddr_in servaddr; // the server to connect to

    sockfd = socket(AF_INET, SOCK_STREAM, 0);

    if (sockfd == -1)
    {
        printf("Could not create socket");
        return -1;
    }

    bzero(&servaddr, sizeof(servaddr));
    servaddr.sin_family = AF_INET;
    servaddr.sin_addr.s_addr = inet_addr(argv[1]);
    char *p;
    long port = strtol(argv[2], &p, 10);
    servaddr.sin_port = htons(port);

    // Ask the OS to make the wire to the other machine
    if (connect(sockfd, (struct sockaddr *)&servaddr, sizeof(servaddr)) != 0)
    {
        printf("Usage: ./chatc <host> <port> <username>");
        return 1;
    }

    int result = send_CNCT(sockfd, argv[3]);

    // if CNCT is sent and ACKC is received properly, result should be 1
    if (result == 1)
    {
        int err;
        struct client_info client;
        client.sockfd = sockfd;
        strcpy(client.username, argv[3]);

        pthread_t sender;
        pthread_t receiver;

        err = pthread_create(&sender, NULL, &send_thread, (void *)&client);
        err = pthread_create(&receiver, NULL, &recv_thread, (void *)&client);
        err = pthread_join(sender, NULL);
        err = pthread_join(receiver, NULL);
    }
    else
    {
        printf("Unsuccessful request to message\n");
    }

    close(sockfd);

    return 0;
}
