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
    // 1. Verificar prefixo "ftp://"
    const char *prefix = "ftp://";
    int prefix_len = strlen(prefix);

    if (strncmp(link, prefix, prefix_len) != 0) {
        fprintf(stderr, "Erro: URL deve começar com ftp://\n");
        return -1;
    }

    // Avançar para depois de "ftp://"
    char *ptr = link + prefix_len;

    // 2. Separar user e password → user:pass@
    char *at = strchr(ptr, '@');
    if (!at) {
        fprintf(stderr, "Erro: URL deve conter user e password (falta '@')\n");
        return -1;
    }

    char userpass[128];
    int up_len = at - ptr;
    strncpy(userpass, ptr, up_len);
    userpass[up_len] = '\0';

    // Dividir user:pass
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

    // 3. Host e Path → host/path/file
    ptr = at + 1;

    // Procurar primeira '/'
    char *slash = strchr(ptr, '/');
    if (!slash) {
        fprintf(stderr, "Erro: URL deve conter caminho e ficheiro (falta '/')\n");
        return -1;
    }

    // Extrair host
    int host_len = slash - ptr;
    if (host_len <= 0) {
        fprintf(stderr, "Erro: host inválido\n");
        return -1;
    }

    strncpy(url->host, ptr, host_len);
    url->host[host_len] = '\0';

    // 4. Extrair path + file
    char *pathfile = slash + 1;
    if (strlen(pathfile) == 0) {
        fprintf(stderr, "Erro: falta caminho/ficheiro\n");
        return -1;
    }

    // Encontrar último '/' → separa path do file
    char *last_slash = strrchr(pathfile, '/');
    if (!last_slash) {
        // Não há diretórios, só ficheiro
        url->path[0] = '\0';
        strncpy(url->file, pathfile, sizeof(url->file));
    } else {
        // Path = tudo antes do último slash
        int p_len = last_slash - pathfile;
        strncpy(url->path, pathfile, p_len);
        url->path[p_len] = '\0';

        // File = depois do slash final
        strncpy(url->file, last_slash + 1, sizeof(url->file));

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






    return 0;
}
