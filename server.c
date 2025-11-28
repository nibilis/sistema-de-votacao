// Code made by:
//  - Marco Antonio de Camargo, RA: 10418309
//  - Natan Moreira Passos, RA: 10417916
//  - Nicolas Henriques de Almeida, RA: 10418357


#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <pthread.h>
#include <ctype.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <time.h>
#include <stdarg.h>

#define BACKLOG 50
#define MAX_LINE 1024
#define MAX_OPTIONS 64
#define MAX_VOTERS 10000
#define ID_LEN 128

typedef struct {
    char id[ID_LEN];
    int voted; // 0 or 1
    int choice; // index of option
} Voter;

char *options[MAX_OPTIONS];
int opt_count = 0;
int counts[MAX_OPTIONS];
Voter voters[MAX_VOTERS];
int voter_count = 0;

int election_closed = 0;

pthread_mutex_t lock = PTHREAD_MUTEX_INITIALIZER;
FILE *log_file = NULL;

void log_event(const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    pthread_mutex_lock(&lock);
    if (log_file) {
        time_t t = time(NULL);
        char buf[64];
        strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S", localtime(&t));
        fprintf(log_file, "[%s] ", buf);
        vfprintf(log_file, fmt, ap);
        fprintf(log_file, "\n");
        fflush(log_file);
    }
    pthread_mutex_unlock(&lock);
    va_end(ap);
}

void trim_newline(char *s) {
    size_t l = strlen(s);
    while (l>0 && (s[l-1]=='\n' || s[l-1]=='\r')) { s[l-1]=0; l--; }
}

int find_voter(const char *id) {
    for (int i=0;i<voter_count;i++) {
        if (strcmp(voters[i].id, id)==0) return i;
    }
    return -1;
}

int add_voter_if_missing(const char *id) {
    int idx = find_voter(id);
    if (idx!=-1) return idx;
    if (voter_count >= MAX_VOTERS) return -1;
    strncpy(voters[voter_count].id, id, ID_LEN-1);
    voters[voter_count].id[ID_LEN-1]=0;
    voters[voter_count].voted=0;
    voters[voter_count].choice=-1;
    voter_count++;
    return voter_count-1;
}

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

void send_line(int sock, const char *fmt, ...) {
    char buf[2048];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    size_t len = strlen(buf);
    // ensure newline
    if (len==0 || buf[len-1] != '\n') {
        if (len + 2 < sizeof(buf)) {
            buf[len]='\n';
            buf[len+1]=0;
            len++;
        }
    }
    send(sock, buf, strlen(buf), 0);
}

void send_options(int sock) {
    pthread_mutex_lock(&lock);
    // build line: OPTIONS <k> <op1> ... <opk>
    char line[4096];
    int off = 0;
    off += snprintf(line+off, sizeof(line)-off, "OPTIONS %d", opt_count);
    for (int i=0;i<opt_count;i++) {
        off += snprintf(line+off, sizeof(line)-off, " %s", options[i]);
    }
    pthread_mutex_unlock(&lock);
    send_line(sock, "%s", line);
}

void send_score(int sock, int final) {
    pthread_mutex_lock(&lock);
    // SCORE <k> <op1>:<count1> ...
    char line[4096];
    int off = 0;
    off += snprintf(line+off, sizeof(line)-off, "%s %d", final ? "CLOSED FINAL" : "SCORE", opt_count);
    for (int i=0;i<opt_count;i++) {
        off += snprintf(line+off, sizeof(line)-off, " %s:%d", options[i], counts[i]);
    }
    pthread_mutex_unlock(&lock);
    send_line(sock, "%s", line);
}

void *client_thread(void *arg) {
    int sock = *(int*)arg;
    free(arg);
    char buf[MAX_LINE];
    char voter_id[ID_LEN] = "";
    int greeted = 0;

    // send nothing initially; wait for HELLO
    while (1) {
        ssize_t r = read_line(sock, buf, sizeof(buf));
        if (r<=0) {
            // disconnect
            log_event("Client disconnected (no more data). Voter=%s", voter_id);
            close(sock);
            return NULL;
        }
        trim_newline(buf);
        // parse command
        if (strncmp(buf, "HELLO ", 6)==0) {
            char *id = buf+6;
            trim_newline(id);
            strncpy(voter_id, id, ID_LEN-1);
            voter_id[ID_LEN-1]=0;
            greeted = 1;
            send_line(sock, "WELCOME %s", voter_id);
            log_event("HELLO from %s", voter_id);
            continue;
        }
        if (!greeted) {
            send_line(sock, "ERR You must HELLO <VOTER_ID> first");
            continue;
        }
        if (strcmp(buf, "LIST")==0) {
            send_options(sock);
            continue;
        }
        if (strcmp(buf, "SCORE")==0) {
            if (election_closed) {
                send_score(sock, 1);
            } else {
                send_score(sock, 0);
            }
            continue;
        }
        if (strncmp(buf, "VOTE ", 5)==0) {
            if (election_closed) {
                send_line(sock, "ERR CLOSED");
                continue;
            }
            char *opt = buf+5;
            trim_newline(opt);
            pthread_mutex_lock(&lock);
            int vidx = add_voter_if_missing(voter_id);
            if (vidx<0) {
                pthread_mutex_unlock(&lock);
                send_line(sock, "ERR SERVER_FULL");
                continue;
            }
            if (voters[vidx].voted) {
                pthread_mutex_unlock(&lock);
                send_line(sock, "ERR DUPLICATE");
                log_event("Duplicate vote attempt by %s", voter_id);
                continue;
            }
            int found = -1;
            for (int i=0;i<opt_count;i++) {
                if (strcmp(options[i], opt)==0) { found = i; break; }
            }
            if (found==-1) {
                pthread_mutex_unlock(&lock);
                send_line(sock, "ERR INVALID_OPTION");
                log_event("Invalid option %s by %s", opt, voter_id);
                continue;
            }
            // record vote
            voters[vidx].voted = 1;
            voters[vidx].choice = found;
            counts[found]++;
            log_event("VOTE %s %s", voter_id, options[found]);
            pthread_mutex_unlock(&lock);
            send_line(sock, "OK VOTED %s", options[found]);
            continue;
        }
        if (strcmp(buf, "BYE")==0) {
            send_line(sock, "BYE");
            log_event("BYE from %s", voter_id);
            close(sock);
            return NULL;
        }
        if (strcmp(buf, "ADMIN CLOSE")==0) {
            // require admin id
            if (strcmp(voter_id, "ADMIN")!=0) {
                send_line(sock, "ERR NOT_ADMIN");
                continue;
            }
            pthread_mutex_lock(&lock);
            if (!election_closed) {
                election_closed = 1;
                // write final result file
                FILE *f = fopen("resultado_final.txt", "w");
                if (f) {
                    fprintf(f, "FINAL %d\n", opt_count);
                    for (int i=0;i<opt_count;i++) {
                        fprintf(f, "%s:%d\n", options[i], counts[i]);
                    }
                    fclose(f);
                }
                log_event("ADMIN closed election");
            }
            // respond with final scoreboard
            send_score(sock, 1);
            pthread_mutex_unlock(&lock);
            continue;
        }

        send_line(sock, "ERR UNKNOWN_COMMAND");
    }
    return NULL;
}

int main(int argc, char **argv) {
    if (argc < 4) {
        fprintf(stderr, "Usage: %s <port> <option1> <option2> <option3> [...]\nMinimum 3 options required.\n", argv[0]);
        return 1;
    }
    int port = atoi(argv[1]);
    opt_count = argc - 2;
    if (opt_count > MAX_OPTIONS) opt_count = MAX_OPTIONS;
    for (int i=0;i<opt_count;i++) {
        options[i] = strdup(argv[i+2]);
        counts[i]=0;
    }

    log_file = fopen("eleicao.log", "a");
    if (!log_file) {
        perror("fopen eleicao.log");
        return 1;
    }
    log_event("Server starting on port %d with %d options", port, opt_count);

    int listenfd;
    if ((listenfd = socket(AF_INET, SOCK_STREAM, 0)) < 0) {
        perror("socket");
        return 1;
    }
    int opt = 1;
    setsockopt(listenfd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    struct sockaddr_in addr;
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port = htons(port);

    if (bind(listenfd, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
        perror("bind");
        return 1;
    }
    if (listen(listenfd, BACKLOG) < 0) {
        perror("listen");
        return 1;
    }
    log_event("Listening on port %d", port);
    while (1) {
        struct sockaddr_in cli;
        socklen_t len = sizeof(cli);
        int newsock = accept(listenfd, (struct sockaddr*)&cli, &len);
        if (newsock < 0) {
            perror("accept");
            continue;
        }
        char ip[INET_ADDRSTRLEN];
        inet_ntop(AF_INET, &cli.sin_addr, ip, sizeof(ip));
        log_event("Accepted connection from %s:%d", ip, ntohs(cli.sin_port));
        pthread_t tid;
        int *pclient = malloc(sizeof(int));
        *pclient = newsock;
        if (pthread_create(&tid, NULL, client_thread, pclient) != 0) {
            perror("pthread_create");
            close(newsock);
            free(pclient);
            continue;
        }
        pthread_detach(tid);
    }

    fclose(log_file);
    return 0;
}
