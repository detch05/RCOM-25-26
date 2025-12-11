#include <stdio.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <stdlib.h>
#include <unistd.h>
#include <netdb.h>
#include <string.h>



typedef struct URL {
    char host[128];
    char name[64];
    char password[64];
    char path[64];
    char file[256];
} URL;

#define FTP_PORT 21

int handle_URL(char *link, URL *url) {
    const char *prefix = "ftp://";
    int prefix_len = strlen(prefix);

    if (strncmp(link, prefix, prefix_len) != 0) {
        fprintf(stderr, "Erro: URL deve começar com ftp://\n");
        return -1;
    }

    char *ptr = link + prefix_len;

    // Defaults
    strcpy(url->name, "rcom");
    strcpy(url->password, "rcom");

    char *at = strchr(ptr, '@');
    char *slash;

    if (at) {
        // USER:PASS@HOST/FILE
        char userpass[128];
        int up_len = at - ptr;
        strncpy(userpass, ptr, up_len);
        userpass[up_len] = '\0';

        char *colon = strchr(userpass, ':');
        if (!colon) {
            fprintf(stderr, "Erro: falta ':' entre user e password\n");
            return -1;
        }

        *colon = '\0';
        char *user = userpass;
        char *pass = colon + 1;

        if (strlen(user) == 0 || strlen(pass) == 0) {
            fprintf(stderr, "Erro: user ou password vazios\n");
            return -1;
        }

        strncpy(url->name, user, sizeof(url->name));
        strncpy(url->password, pass, sizeof(url->password));

        ptr = at + 1;
    }

    // Agora ptr aponta para HOST/FILE (com ou sem user/pass antes)
    slash = strchr(ptr, '/');
    if (!slash) {
        fprintf(stderr, "Erro: falta caminho e ficheiro na URL\n");
        return -1;
    }

    // HOST
    int host_len = slash - ptr;
    if (host_len <= 0) {
        fprintf(stderr, "Erro: host inválido\n");
        return -1;
    }

    strncpy(url->host, ptr, host_len);
    url->host[host_len] = '\0';

    // PATH + FILE
    char *pathfile = slash + 1;

    if (strlen(pathfile) == 0) {
        fprintf(stderr, "Erro: falta ficheiro\n");
        return -1;
    }

    char *last = strrchr(pathfile, '/');
    if (!last) {
        url->path[0] = '\0';
        strncpy(url->file, pathfile, sizeof(url->file));
    } else {
        int plen = last - pathfile;
        strncpy(url->path, pathfile, plen);
        url->path[plen] = '\0';

        strncpy(url->file, last + 1, sizeof(url->file));

        if (strlen(url->file) == 0) {
            fprintf(stderr, "Erro: ficheiro vazio\n");
            return -1;
        }
    }

    return 0;
}


int create_socket(int *sockfd, char *ip, int port) {

    struct sockaddr_in server_addr;

    /*server address handling*/
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = inet_addr(ip); 
    server_addr.sin_port = htons(FTP_PORT); 

    if ((*sockfd = socket(AF_INET, SOCK_STREAM, 0)) < 0) {
        perror("socket()");
        exit(-1);
    }

    if (connect(*sockfd,
                (struct sockaddr *) &server_addr,
                sizeof(server_addr)) < 0) {
        perror("connect()");
        exit(-1);
    }

    return 0;
}

int parse_pasv(const char *buf, int *h1, int *h2, int *h3, int *h4, int *p1, int *p2) {
    const char *start = strchr(buf, '(');
    const char *end   = strchr(buf, ')');

    if (!start || !end || end < start) {
        fprintf(stderr, "Erro: formato PASV inválido\n");
        return -1;
    }

    char inside[128];
    int len = end - start - 1;
    strncpy(inside, start + 1, len);
    inside[len] = '\0';

    // Agora inside contém: "h1,h2,h3,h4,p1,p2"
    if (sscanf(inside, "%d,%d,%d,%d,%d,%d", h1, h2, h3, h4, p1, p2) != 6) {
        fprintf(stderr, "Erro: não foi possível extrair valores PASV\n");
        return -1;
    }

    return 0;
}


int main(int argc, char *argv[]) {
    if (argc != 2) {
        fprintf(stderr, "Usage: %s ftp://...\n", argv[0]);
        exit(-1);
    }

    URL url;
    memset(&url, 0, sizeof(url));

    if (handle_URL(argv[1], &url) < 0) {
        fprintf(stderr, "Error to handle URL\n");
        exit(-1);
    }

    printf("USER=%s\nPASS=%s\nHOST=%s\nPATH=%s\nFILE=%s\n",
           url.name, url.password, url.host, url.path, url.file);

    
    struct hostent *h = gethostbyname(url.host);

    if (h == NULL) {
        printf("Erro DNS\n");
        exit(1);
    }

    char *ip = inet_ntoa(*((struct in_addr *) h->h_addr));
    printf("IP = %s\n", ip);
    

    // Socket creation

    int sockfd;
    if (create_socket(&sockfd, ip, FTP_PORT) < 0) {
        fprintf(stderr, "Error to create socket\n");
        exit(-1);
    } else {
        printf("Conection established to %s\n", url.host);
    }

    usleep(1000000); // wait for 1 second to receive server welcome message


    char buf[500];


    size_t bytes_read = read(sockfd, buf, 500);
    buf[bytes_read] = '\0';
    printf("%s", buf);

    if (strncmp(buf, "220", 3) != 0) {
        printf("Error: Unexpected reply 220.\n");
        exit(-1);
    }


    // Login with USER and PASS
    char user_cmd[256];
    sprintf(user_cmd, "USER %s\r\n", url.name);
    write(sockfd, user_cmd, strlen(user_cmd));

    size_t bytes_user = read(sockfd, buf, sizeof(buf) - 1);
    buf[bytes_user] = '\0';
    printf("%s", buf);

    if (strncmp(buf, "331", 3) != 0) {
        printf("Error: Unexpected reply 331.\n");
        exit(-1);
    }

    char pass_cmd[256];
    sprintf(pass_cmd, "PASS %s\r\n", url.password);
    write(sockfd, pass_cmd, strlen(pass_cmd));

    size_t bytes_pass = read(sockfd, buf, sizeof(buf) - 1);
    buf[bytes_pass] = '\0';
    printf("%s", buf);
    
    if (strncmp(buf, "230", 3) != 0) {
        printf("Error: Unexpected reply 230.\n");
        exit(-1);
    }

    printf("Logged in with USER: %s and PASS: %s.\n", url.name, url.password);

    // ------------------------------------------

    // Enter Passive Mode
    char pasv_cmd[7] = "PASV\r\n";
    write(sockfd, pasv_cmd, strlen(pasv_cmd));

    size_t bytes_passive = read(sockfd, buf, sizeof(buf) - 1);
    buf[bytes_passive] = '\0';
    printf("%s", buf);

    if (strncmp(buf, "227", 3) != 0) {
        printf("Error: Unexpected reply 227.\n");
        exit(-1);
    }

    printf("Entered Passive Mode.\n");

    int h1,h2,h3,h4,p1,p2;
    parse_pasv(buf, &h1, &h2, &h3, &h4, &p1, &p2);

    char ip_server[32];
    sprintf(ip_server, "%d.%d.%d.%d", h1, h2, h3, h4);
    int port_server = p1 * 256 + p2;

    printf("Data Connection IP: %s Port: %d\n", ip_server, port_server);

    // ------------------------------------------

    int sockserver;
    if (create_socket(&sockserver, ip_server, port_server) < 0) {
        fprintf(stderr, "Error to create server socket\n");
        exit(-1);
    }

    char retr_cmd[512];
    if (strlen(url.path) > 0)
        sprintf(retr_cmd, "RETR %s/%s\r\n", url.path, url.file);
    else
        sprintf(retr_cmd, "RETR %s\r\n", url.file);
        
    write(sockfd, retr_cmd, strlen(retr_cmd));

    size_t bytes_retr = read(sockfd, buf, sizeof(buf) - 1);
    buf[bytes_retr] = '\0';

    if (strncmp(buf, "125", 3) != 0 && strncmp(buf, "150", 3) != 0) {
        printf("Error: expected reply 150 or 125.\n");
        exit(-1);
    }

    FILE *file = fopen(url.file, "wb");
    if (!file) {
        perror("Error to create local file");
        exit(-1);
    }

    printf("Downloading file: %s\n", url.file);
    while ((bytes_read = read(sockserver, buf, sizeof(buf))) > 0) {
        size_t bytes_written = fwrite(buf, 1, bytes_read, file);
        if (bytes_written < bytes_read) {
            perror("Error writing to file");
            fclose(file);
            exit(-1);
        }
    }

    fclose(file);
    printf("%s downloaded successfully.\n", url.file);

    if (close(sockserver) < 0) {
        perror("Error closing data socket");
        exit(-1);
    }

    size_t bytes_final = read(sockfd, buf, sizeof(buf) - 1);
    buf[bytes_final] = '\0';

    if (strncmp(buf, "226", 3) != 0) {
        printf("Error: expected reply 226.\n");
        exit(-1);
    }

    char quit_cmd[7] = "QUIT\r\n";
    write(sockfd, quit_cmd, strlen(quit_cmd));

    if (strncmp(buf, "221", 3) != 0) {
        printf("Error: Unexpected reply 221.\n");
        exit(-1);
    }

    if (close(sockfd) < 0) {
        perror("Error closing control socket");
        exit(-1);
    }

    printf("Connection closed successfully.\n");

    return 0;
}

