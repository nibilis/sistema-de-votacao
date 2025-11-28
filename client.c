// client.c
// Simple interactive voting client
// Compile: gcc -o client client.c
// Usage: ./client <server_ip> <port>
// Then use commands:
// HELLO <VOTER_ID>
// LIST
// VOTE <OPTION>
// SCORE
// BYE
// Example: HELLO user1

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <arpa/inet.h>

#define MAX_LINE 2048

ssize_t read_line(int sock, char *buf, size_t maxlen) {
    size_t idx = 0;
    while (idx < maxlen-1) {
        char c;
        ssize_t r = recv(sock, &c, 1, 0);
        if (r<=0) {
            if (r==0) return 0;
            if (errno == EINTR) continue;
            return -1;
        }
        buf[idx++]=c;
        if (c=='\n') break;
    }
    buf[idx]=0;
    return idx;
}

int main(int argc, char **argv) {
    if (argc < 3) {
        fprintf(stderr, "Usage: %s <server_ip> <port>\n", argv[0]);
        return 1;
    }
    char *server_ip = argv[1];
    int port = atoi(argv[2]);
    int sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock < 0) { perror("socket"); return 1; }
    struct sockaddr_in addr;
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    if (inet_pton(AF_INET, server_ip, &addr.sin_addr) <= 0) {
        perror("inet_pton");
        return 1;
    }
    if (connect(sock, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
        perror("connect");
        return 1;
    }
    printf("Connected to %s:%d\n", server_ip, port);
    char line[MAX_LINE];
    char buf[MAX_LINE];
    while (1) {
        printf("> ");
        if (!fgets(line, sizeof(line), stdin)) break;
        size_t l = strlen(line);
        if (l==0) continue;
        if (line[l-1] != '\n') {
            // ensure newline
            if (l+1 < sizeof(line)) { line[l]='\n'; line[l+1]=0; }
        }
        printf("Sending %s\n", line);
        if (send(sock, line, strlen(line), 0) < 0) {
            perror("send");
            break;
        }
        // read response(s). Server responds per command with one line.
        ssize_t r = read_line(sock, buf, sizeof(buf));
        if (r<=0) {
            printf("Server closed connection.\n");
            break;
        }
        printf("%s", buf);
        // If BYE then exit
        if (strncmp(buf, "BYE", 3)==0) break;
    }
    close(sock);
    return 0;
}
