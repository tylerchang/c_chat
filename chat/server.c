/** A really simple chat server implementation in C **/

#include <stdio.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <pthread.h>

// struct to represent a client
struct client
{
    struct sockaddr_in addr;
    int sockfd;
    pthread_t thread;
    char *name;
    struct client *nxt;
};

pthread_mutex_t conn_loc;
struct client **conn;
size_t connsz;
size_t conns;
void connect_add(struct client *c)
{
    // Add the new client to the list of active connections
    pthread_mutex_lock(&conn_loc);
    if (conns == connsz)
    {
        conn = realloc(conn, sizeof(struct client *) * connsz << 1);
        connsz = connsz << 1;
    }
    conn[conns] = c;
    conns++;
    pthread_mutex_unlock(&conn_loc);
}

void connect_del(struct client *c)
{
    // Remove the client from the list of active connections
    pthread_mutex_lock(&conn_loc);
    size_t cs = conns;
    for (size_t i = 0, j = 0; i < cs; i++, j++)
    {
        if (conn[i] == c)
        {
            j--;
            conns--;
        }
        else
        {
            conn[j] = conn[i];
        }
    }
    pthread_mutex_unlock(&conn_loc);
    close(c->sockfd);
    free(c->name);
}
void connect_broadcast(char *username, char *msg)
{
    // broadcast a message to all clients
    size_t buflen = 8 + strlen(username) + strlen(msg);
    char *buf = malloc(sizeof(char) * buflen);
    strncpy(buf, "MESG", 4);
    uint16_t len = strlen(username);
    len = htobe16(len);
    memcpy(buf + 4, &len, 2);
    strncpy(buf + 6, username, strlen(username));
    len = strlen(msg);
    len = htobe16(len);
    memcpy(buf + 6 + strlen(username), &len, 2);
    strncpy(buf + 8 + strlen(username), msg, strlen(msg));
    pthread_mutex_lock(&conn_loc);
    for (size_t i = 0; i < conns; i++)
    {
        printf("Sending %s %s to %s\n", username, msg, conn[i]->name);
        if (send(conn[i]->sockfd, buf, buflen, 0) < 0)
        {
            connect_del(conn[i]);
            i--;
        }
    }
    pthread_mutex_unlock(&conn_loc);
    free(buf);
}
void killall()
{
    pthread_mutex_lock(&conn_loc);
    while (connsz > 0)
    {
        connect_del(conn[0]);
    }
    pthread_mutex_unlock(&conn_loc);
    free(conn);
}

void sendack(struct client *c)
{
    char buf[4];
    strncpy(buf, "ACKC", 4);
    send(c->sockfd, buf, 4, 0);
}

int clnt_read(struct client *c)
{
    // Figure out the message type
    char mtype[4];
    int r;
    r = recv(c->sockfd, mtype, 4, MSG_WAITALL);
    if (r < 4)
    {
        // bad read, kill the connection
        printf("short read %d: %s sent an invalid message. Killing the connection.\n", r, c->name);
        connect_del(c);
        return 0;
    }
    // try to parse the bit pattern
    if (strncmp(mtype, "CNCT", 4) == 0 && c->name == NULL)
    {
        //// Connection message, should set the username of the client
        // get the length of the username
        uint16_t len;
        r = recv(c->sockfd, &len, 2, MSG_WAITALL);
        if (r < 2)
        {
            printf("%s sent an invalid message. Killing the connection.\n", c->name);
            connect_del(c);
            return 0;
        }
        len = be16toh(len);
        // get the username
        char *name = malloc(len + 1);
        name[len] = '\0';
        r = recv(c->sockfd, name, len, MSG_WAITALL);
        if (r < len)
        {
            printf("%s sent an invalid message. Killing the connection.\n", c->name);
            free(name);
            connect_del(c);
        }
        // everything seems ok, set the name in the connection
        c->name = name;
        sendack(c);
        return 1;
    }
    if (strncmp(mtype, "MESG", 4) == 0 && c->name != NULL)
    {
        //// Message message, should relay to the other users
        // get the length of the username
        uint16_t len;
        r = recv(c->sockfd, &len, 2, MSG_WAITALL);
        if (r != 2)
        {
            printf("%s sent an invalid message. Killing the connection.\n", c->name);
            connect_del(c);
            return 0;
        }
        len = be16toh(len);
        // get the username
        char *name = malloc(len + 1);
        name[len] = '\0';
        r = recv(c->sockfd, name, len, MSG_WAITALL);
        if (r != len)
        {
            printf("%s sent an invalid message. Killing the connection.\n", c->name);
            connect_del(c);
            free(name);
            return 0;
        }
        // get the message length
        r = recv(c->sockfd, &len, 2, MSG_WAITALL);
        if (r != 2)
        {
            printf("%s sent an invalid message. Killing the connection.\n", c->name);
            connect_del(c);
            free(name);
            return 0;
        }
        len = be16toh(len);
        // get the message text
        char *msg = malloc(len + 1);
        msg[len] = '\0';
        r = recv(c->sockfd, msg, len, MSG_WAITALL);
        if (r != len)
        {
            printf("%s sent an invalid message. Killing the connection.\n", c->name);
            connect_del(c);
            free(name);
            free(msg);
            return 0;
        }

        // relay the message to everybody
        connect_broadcast(name, msg);

        free(name);
        free(msg);
        return 1;
    }
    if (strncmp(mtype, "QUIT", 4) == 0 && c->name != NULL)
    {
        // close the connection
        printf("%s has disconnected.\n", c->name);
        connect_del(c);
        return 0;
    }
    // Protocol violation, kill the connection
    printf("Proto: %s sent an invalid or out of order message. Killing the connection.\n", c->name);
    connect_del(c);
    return 0;
}

void *handle_client(void *args)
{
    struct client *c = (struct client *)args;
    connect_add(c);
    while (clnt_read(c))
    {
    }
}

int main(int argc, char **argv)
{
    if (argc != 2)
    {
        printf("Usage: %s <port>\n", argv[0]);
        return 1;
    }
    // Struct to describe where the server should listen for connections at
    struct sockaddr_in myaddr;
    myaddr.sin_family = AF_INET;
    myaddr.sin_addr.s_addr = INADDR_ANY;
    myaddr.sin_port = htons(atoi(argv[1]));

    // Make the socket
    int sockfd = socket(AF_INET, SOCK_STREAM, 0);
    if (sockfd < 0)
    {
        printf("Could not generate a socket...\n");
        return 1;
    }

    // Bind the socket to the port(s)
    if (bind(sockfd, (struct sockaddr *)&myaddr, sizeof(myaddr)) < 0)
    {
        printf("Could not bind to the port\n");
        return 1;
    }

    // Listen for a client to connect, allow up to 5 to be waiting for an accept
    listen(sockfd, 5);

    // Get the list initialized
    connsz = 2;
    conns = 0;
    conn = malloc(sizeof(struct client *) * connsz);

    // Start trying to accept/service clients
    while (1)
    {
        struct client *clnt = malloc(sizeof(struct client));
        bzero(&(clnt->addr), sizeof(struct sockaddr_in));
        clnt->sockfd = -1;
        clnt->name = NULL;
        // accept the next client connection
        socklen_t other_len = sizeof(struct sockaddr_in);
        clnt->sockfd = accept(sockfd, (struct sockaddr *)&(clnt->addr), &other_len);
        if (clnt->sockfd < 0)
        {
            printf("Tried to accept a connection but failed...\n");
            free(clnt);
            continue;
        }
        // pass the struct to a thread that will handle communication
        if (pthread_create(&(clnt->thread), 0, &handle_client, clnt))
        {
            printf("Could not create a thread for a client...\n");
            close(clnt->sockfd);
            free(clnt);
            continue;
        }
    }
    // close the socket and stop running the program
    close(sockfd);
    killall();
    return 0;
}
