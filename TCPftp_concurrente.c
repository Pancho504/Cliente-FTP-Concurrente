/* TCPftp_concurrente.c - Cliente FTP Concurrente Final y Robusto */

#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <errno.h>
#include <netdb.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <sys/wait.h>
#include <signal.h>

extern int errno;

int errexit(const char *format, ...);
int connectTCP(const char *host, const char *service);
int passiveTCP(const char *service, int qlen);

#define LINELEN 128

/* Manejador de señales para evitar procesos zombies */
void reaper(int sig) {
    int status;
    while (wait3(&status, WNOHANG, (struct rusage *)0) >= 0);
}

/* Envia comando al servidor y muestra la respuesta */
void sendCmd(int s, char *cmd, char *res) {
    int n;
    n = strlen(cmd);
    cmd[n] = '\r';
    cmd[n + 1] = '\n';
    n = write(s, cmd, n + 2);
    n = read(s, res, LINELEN);
    res[n] = '\0';
    printf("%s", res);
}

/* * pasivo: Negocia modo PASV.
 * CORRECCIÓN IMPORTANTE: Lee el socket en un bucle until (while) hasta
 * encontrar el código 227. Esto descarta mensajes '226' viejos que
 * llegan de transferencias concurrentes finalizadas.
 */
int pasivo(int s) {
    int sdata;
    int nport;
    char cmd[128], res[128], *p;
    char host[64], port[8];
    int h1, h2, h3, h4, p1, p2;
    int n;

    sprintf(cmd, "PASV");
    
    /* 1. Enviamos comando manualmente (sin usar sendCmd) */
    n = strlen(cmd);
    cmd[n] = '\r';
    cmd[n + 1] = '\n';
    write(s, cmd, n + 2);

    /* 2. Bucle de limpieza: Leemos hasta encontrar el 227 */
    while (1) {
        n = read(s, res, LINELEN);
        if (n <= 0) return -1; // Error de socket
        res[n] = '\0';
        
        // Imprimimos lo que llega para ver si descartamos basura (DEBUG útil)
        printf("%s", res); 

        // Si encontramos el código 227, rompemos el ciclo. ¡Éxito!
        if (strncmp(res, "227", 3) == 0) break;
    }

    /* 3. Parseo de la IP y Puerto */
    p = strchr(res, '(');
    if (p == NULL) return -1;
    sscanf(p + 1, "%d,%d,%d,%d,%d,%d", &h1, &h2, &h3, &h4, &p1, &p2);
    snprintf(host, 64, "%d.%d.%d.%d", h1, h2, h3, h4);
    nport = p1 * 256 + p2;
    snprintf(port, 8, "%d", nport);
    sdata = connectTCP(host, port);

    return sdata;
}

void ayuda() {
    printf("Cliente FTP Concurrente. Comandos disponibles:\n \
    help       - despliega este texto\n \
    dir        - lista el directorio actual del servidor\n \
    get <arch> - descarga archivo (CONCURRENTE)\n \
    put <arch> - sube archivo (CONCURRENTE)\n \
    pput <arch> - sube archivo modo activo (CONCURRENTE)\n \
    cd <dir>   - cambia al directorio dir en el servidor\n \
    quit       - finaliza la sesion FTP\n\n");
}

int main(int argc, char *argv[]) {
    char *host = "localhost";
    char *service = "ftp";
    char cmd[128], res[128];
    char data[LINELEN + 1];
    char user[32], *pass, prompt[64], *ucmd, *arg;
    int s, sdata, n;
    FILE *fp;
    struct sockaddr_in addrSvr;
    unsigned int alen;
    pid_t pid;

    (void) signal(SIGCHLD, reaper);

    switch (argc) {
        case 1:
            host = "localhost";
            break;
        case 3:
            service = argv[2];
            /* FALL THROUGH */
        case 2:
            host = argv[1];
            break;
        default:
            fprintf(stderr, "Uso: TCPftp [host [port]]\n");
            exit(1);
    }

    s = connectTCP(host, service);
    n = read(s, res, LINELEN);
    res[n] = '\0';
    printf("%s", res);

    while (1) {
        printf("Please enter your username: ");
        scanf("%s", user);
        sprintf(cmd, "USER %s", user);
        sendCmd(s, cmd, res);

        pass = getpass("Enter your password: ");
        sprintf(cmd, "PASS %s", pass);
        sendCmd(s, cmd, res);
        if (strstr(res, "230") != NULL) break; 
    }

    fgets(prompt, sizeof(prompt), stdin); 
    ayuda();

    while (1) {
        printf("ftp> ");
        if (fgets(prompt, sizeof(prompt), stdin) != NULL) {
            prompt[strcspn(prompt, "\n")] = 0;
            ucmd = strtok(prompt, " ");

            if (ucmd == NULL) continue;

            if (strcmp(ucmd, "dir") == 0) {
                sdata = pasivo(s);
                if (sdata < 0) { printf("Error conectando PASV\n"); continue; }

                sprintf(cmd, "LIST");
                sendCmd(s, cmd, res);
                while ((n = recv(sdata, data, LINELEN, 0)) > 0) {
                    fwrite(data, 1, n, stdout);
                }
                close(sdata);
                
                // Leemos confirmación del servidor
                n = read(s, res, LINELEN);
                res[n] = '\0';
                printf("%s", res);

            } else if (strcmp(ucmd, "get") == 0) {
                arg = strtok(NULL, " ");
                if(arg == NULL) { printf("Falta nombre de archivo\n"); continue; }

                sdata = pasivo(s);
                if (sdata < 0) { printf("Error conectando PASV\n"); continue; }

                sprintf(cmd, "RETR %s", arg);
                sendCmd(s, cmd, res);

                if (strstr(res, "550") != NULL) { close(sdata); continue; }

                if ((pid = fork()) == 0) {
                    /* HIJO */
                    printf("\n[Hijo] Iniciando transferencia... (Simulando latencia de 10s)\n");
                    sleep(1); // Mantenemos el sleep para tu prueba

                    fp = fopen(arg, "wb");
                    if (fp != NULL) {
                        while ((n = recv(sdata, data, LINELEN, 0)) > 0) {
                            fwrite(data, 1, n, fp);
                        }
                        fclose(fp);
                        printf("\n[Hijo %d] Transferencia '%s' finalizada.\n", getpid(), arg);
                        printf("ftp> "); 
                        fflush(stdout);
                    }
                    close(sdata);
                    exit(0);
                } else {
                    /* PADRE */
                    close(sdata);
                    printf("[Padre] Descarga '%s' iniciada en segundo plano (PID: %d)\n", arg, pid);
                }

            } else if (strcmp(ucmd, "put") == 0) {
                arg = strtok(NULL, " ");
                if(arg == NULL) { printf("Falta nombre de archivo\n"); continue; }
                if (access(arg, R_OK) != 0) { perror("Acceso archivo local"); continue; }

                sdata = pasivo(s);
                if (sdata < 0) { printf("Error conectando PASV\n"); continue; }

                sprintf(cmd, "STOR %s", arg);
                sendCmd(s, cmd, res);

                if ((pid = fork()) == 0) {
                    fp = fopen(arg, "r");
                    if (fp != NULL) {
                        while ((n = fread(data, 1, LINELEN, fp)) > 0) {
                            send(sdata, data, n, 0);
                        }
                        fclose(fp);
                        printf("\n[Hijo %d] Subida '%s' finalizada.\n", getpid(), arg);
                        printf("ftp> "); 
                        fflush(stdout);
                    }
                    close(sdata);
                    exit(0);
                } else {
                    close(sdata);
                    printf("[Padre] Subida '%s' iniciada en segundo plano (PID: %d)\n", arg, pid);
                }

            } else if (strcmp(ucmd, "pput") == 0) {
                arg = strtok(NULL, " ");
                if(arg == NULL) { printf("Falta nombre de archivo\n"); continue; }
                if (access(arg, R_OK) != 0) { perror("Acceso archivo local"); continue; }

                int s_listen;
                struct sockaddr_in addr_dyn;
                socklen_t dlen = sizeof(addr_dyn);

                s_listen = passiveTCP("0", 5); 
                getsockname(s_listen, (struct sockaddr *)&addr_dyn, &dlen);
                int local_port = ntohs(addr_dyn.sin_port);

                char lname[64], ip_ftp[64], *ip;
                gethostname(lname, 64);
                struct hostent *hent = gethostbyname(lname);
                ip = inet_ntoa(*((struct in_addr*) hent->h_addr_list[0]));
                
                strcpy(ip_ftp, ip);
                for(int i=0; i<strlen(ip_ftp); i++) if(ip_ftp[i]=='.') ip_ftp[i]=',';

                int p1 = local_port / 256;
                int p2 = local_port % 256;

                sprintf(cmd, "PORT %s,%d,%d", ip_ftp, p1, p2);
                sendCmd(s, cmd, res);

                sprintf(cmd, "STOR %s", arg);
                sendCmd(s, cmd, res);

                if ((pid = fork()) == 0) {
                    alen = sizeof(addrSvr);
                    sdata = accept(s_listen, (struct sockaddr *)&addrSvr, &alen);
                    close(s_listen);

                    fp = fopen(arg, "r");
                    if (fp && sdata >= 0) {
                        while ((n = fread(data, 1, LINELEN, fp)) > 0) {
                            send(sdata, data, n, 0);
                        }
                        fclose(fp);
                        printf("\n[Hijo %d] Subida PPUT '%s' terminada.\n", getpid(), arg);
                        printf("ftp> "); 
                        fflush(stdout);
                    }
                    if(sdata >= 0) close(sdata);
                    exit(0);
                } else {
                    close(s_listen);
                    printf("[Padre] PPUT iniciado PID %d en puerto %d\n", pid, local_port);
                }

            } else if (strcmp(ucmd, "cd") == 0) {
                arg = strtok(NULL, " ");
                sprintf(cmd, "CWD %s", arg);
                sendCmd(s, cmd, res);

            } else if (strcmp(ucmd, "quit") == 0) {
                sprintf(cmd, "QUIT");
                sendCmd(s, cmd, res);
                exit(0);

            } else if (strcmp(ucmd, "help") == 0) {
                ayuda();

            } else {
                printf("Comando no reconocido.\n");
            }
        }
    }
}
