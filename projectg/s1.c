#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <dirent.h>

#define PORT 8080
#define S2_PORT 8081
#define S3_PORT 8082
#define S4_PORT 8083
#define BUFFER_SIZE 1024
#define PATH_MAX_SIZE 4096
#define CMD_SIZE 4200

void send_file(int sock, const char *filepath);
void receive_file(int sock, const char *filepath);
int connect_to_server(int port);

void prcclient(int client_sock) {
    char buffer[BUFFER_SIZE];
    while (1) {
        memset(buffer, 0, BUFFER_SIZE);
        int bytes_received = recv(client_sock, buffer, BUFFER_SIZE - 1, 0);
        if (bytes_received <= 0) {
            printf("S1: Client disconnected\n");
            break;
        }
        buffer[bytes_received] = '\0';
        printf("S1: Received command: %s\n", buffer);
        char command[10], arg1[256], arg2[256];
        sscanf(buffer, "%s %s %s", command, arg1, arg2);

        if (strcmp(command, "uploadf") == 0) {
            char filepath[PATH_MAX_SIZE];
            int n = snprintf(filepath, sizeof(filepath), "%s/%s/%s", getenv("HOME"), arg2, arg1);
            if (n >= sizeof(filepath)) {
                printf("S1: Error: Filepath too long\n");
                send(client_sock, "Error: Filepath too long", 25, 0);
            } else {
                char dirpath[PATH_MAX_SIZE];
                snprintf(dirpath, sizeof(dirpath), "%s/%s", getenv("HOME"), arg2);
                mkdir(dirpath, 0777);
                receive_file(client_sock, filepath);
                printf("S1: File %s received from client\n", filepath);
                char *ext = strrchr(arg1, '.');
                if (ext && strcmp(ext, ".c") != 0) {
                    int target_sock = connect_to_server(
                        strcmp(ext, ".pdf") == 0 ? S2_PORT :
                        strcmp(ext, ".txt") == 0 ? S3_PORT : S4_PORT);
                    send(target_sock, buffer, strlen(buffer), 0);
                    send_file(target_sock, filepath);
                    printf("S1: File %s forwarded to S%d\n", filepath, 
                           strcmp(ext, ".pdf") == 0 ? 2 : strcmp(ext, ".txt") == 0 ? 3 : 4);
                    remove(filepath);
                    printf("S1: File %s deleted locally\n", filepath);
                    close(target_sock);
                } else {
                    printf("S1: File %s stored locally\n", filepath);
                }
                send(client_sock, "File uploaded", 14, 0);
                printf("S1: Sent 'File uploaded' to client\n");
            }
        } else if (strcmp(command, "downlf") == 0) {
            char filepath[PATH_MAX_SIZE];
            int n = snprintf(filepath, sizeof(filepath), "%s/%s", getenv("HOME"), arg1);
            if (n >= sizeof(filepath)) {
                printf("S1: Error: Filepath too long\n");
                send(client_sock, "Error: Filepath too long", 25, 0);
            } else {
                char *ext = strrchr(arg1, '.');
                if (ext && strcmp(ext, ".c") != 0) {
                    int target_sock = connect_to_server(
                        strcmp(ext, ".pdf") == 0 ? S2_PORT :
                        strcmp(ext, ".txt") == 0 ? S3_PORT : S4_PORT);
                    send(target_sock, buffer, strlen(buffer), 0);
                    receive_file(target_sock, filepath);
                    printf("S1: File %s received from S%d\n", filepath, 
                           strcmp(ext, ".pdf") == 0 ? 2 : strcmp(ext, ".txt") == 0 ? 3 : 4);
                    send_file(client_sock, filepath);
                    printf("S1: File %s sent to client\n", filepath);
                    remove(filepath);
                    printf("S1: File %s deleted locally\n", filepath);
                    close(target_sock);
                } else {
                    send_file(client_sock, filepath);
                    printf("S1: File %s sent to client from local storage\n", filepath);
                }
            }
        } else if (strcmp(command, "removef") == 0) {
            char filepath[PATH_MAX_SIZE];
            int n = snprintf(filepath, sizeof(filepath), "%s/%s", getenv("HOME"), arg1);
            if (n >= sizeof(filepath)) {
                printf("S1: Error: Filepath too long\n");
                send(client_sock, "Error: Filepath too long", 25, 0);
            } else {
                char *ext = strrchr(arg1, '.');
                if (ext && strcmp(ext, ".c") != 0) {
                    int target_sock = connect_to_server(
                        strcmp(ext, ".pdf") == 0 ? S2_PORT :
                        strcmp(ext, ".txt") == 0 ? S3_PORT : S4_PORT);
                    send(target_sock, buffer, strlen(buffer), 0);
                    printf("S1: Requested S%d to delete %s\n", 
                           strcmp(ext, ".pdf") == 0 ? 2 : strcmp(ext, ".txt") == 0 ? 3 : 4, filepath);
                    close(target_sock);
                } else {
                    remove(filepath);
                    printf("S1: File %s deleted locally\n", filepath);
                }
                send(client_sock, "File removed", 13, 0);
                printf("S1: Sent 'File removed' to client\n");
            }
        } else if (strcmp(command, "downltar") == 0) {
            char tarfile[PATH_MAX_SIZE], cmd[CMD_SIZE];
            if (strcmp(arg1, ".c") == 0) {
                int n = snprintf(tarfile, sizeof(tarfile), "%s/S1/cfiles.tar", getenv("HOME"));
                if (n >= sizeof(tarfile)) {
                    printf("S1: Error: Tarfile path too long\n");
                    send(client_sock, "Error: Tarfile path too long", 29, 0);
                } else {
                    n = snprintf(cmd, sizeof(cmd), "tar -cvf %s %s/S1/*.c", tarfile, getenv("HOME"));
                    if (n >= sizeof(cmd)) {
                        printf("S1: Error: Tar command too long\n");
                        send(client_sock, "Error: Tar command too long", 28, 0);
                    } else {
                        system(cmd);
                        printf("S1: Created tar file %s\n", tarfile);
                        send_file(client_sock, tarfile);
                        printf("S1: Sent %s to client\n", tarfile);
                        remove(tarfile);
                        printf("S1: Deleted %s locally\n", tarfile);
                    }
                }
            } else {
                int target_sock = connect_to_server(
                    strcmp(arg1, ".pdf") == 0 ? S2_PORT : S3_PORT);
                int n = snprintf(tarfile, sizeof(tarfile), "%s/S1/%s.tar", getenv("HOME"), arg1 + 1);
                if (n >= sizeof(tarfile)) {
                    printf("S1: Error: Tarfile path too long\n");
                    send(client_sock, "Error: Tarfile path too long", 29, 0);
                } else {
                    send(target_sock, buffer, strlen(buffer), 0);
                    receive_file(target_sock, tarfile); // Fixed typo here
                    printf("S1: Received tar file %s from S%d\n", tarfile, 
                           strcmp(arg1, ".pdf") == 0 ? 2 : 3);
                    send_file(client_sock, tarfile);
                    printf("S1: Sent %s to client\n", tarfile);
                    remove(tarfile);
                    printf("S1: Deleted %s locally\n", tarfile);
                }
                close(target_sock);
            }
        } else if (strcmp(command, "dispfnames") == 0) {
            char filepath[PATH_MAX_SIZE];
            int n = snprintf(filepath, sizeof(filepath), "%s/%s", getenv("HOME"), arg1);
            if (n >= sizeof(filepath)) {
                printf("S1: Error: Filepath too long\n");
                send(client_sock, "Error: Filepath too long", 25, 0);
            } else {
                DIR *dir = opendir(filepath);
                char filelist[BUFFER_SIZE] = "";
                if (dir) {
                    struct dirent *entry;
                    while ((entry = readdir(dir)) != NULL) {
                        if (strstr(entry->d_name, ".c")) {
                            strncat(filelist, entry->d_name, BUFFER_SIZE - strlen(filelist) - 1);
                            strncat(filelist, "\n", BUFFER_SIZE - strlen(filelist) - 1);
                        }
                    }
                    closedir(dir);
                    printf("S1: Collected local .c files\n");
                }
                for (int port = S2_PORT; port <= S4_PORT; port++) {
                    int target_sock = connect_to_server(port);
                    send(target_sock, buffer, strlen(buffer), 0);
                    char temp[BUFFER_SIZE];
                    int bytes = recv(target_sock, temp, BUFFER_SIZE - 1, 0);
                    if (bytes > 0) {
                        temp[bytes] = '\0';
                        strncat(filelist, temp, BUFFER_SIZE - strlen(filelist) - 1);
                        printf("S1: Received file list from S%d\n", port - 8080 + 1);
                    }
                    close(target_sock);
                }
                send(client_sock, filelist, strlen(filelist), 0);
                printf("S1: Sent file list to client\n");
            }
        }
    }
    close(client_sock);
    printf("S1: Client connection closed\n");
}

int connect_to_server(int port) {
    int sock = socket(AF_INET, SOCK_STREAM, 0);
    struct sockaddr_in addr = {0};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = inet_addr("127.0.0.1");
    addr.sin_port = htons(port);
    if (connect(sock, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
        printf("S1: Failed to connect to server on port %d\n", port);
        return -1;
    }
    printf("S1: Connected to server on port %d\n", port);
    return sock;
}

void send_file(int sock, const char *filepath) {
    int fd = open(filepath, O_RDONLY);
    off_t file_size = 0;
    if (fd < 0) {
        printf("S1: Error opening file %s\n", filepath);
        send(sock, &file_size, sizeof(file_size), 0);
        return;
    }
    file_size = lseek(fd, 0, SEEK_END);
    lseek(fd, 0, SEEK_SET);
    send(sock, &file_size, sizeof(file_size), 0);
    if (file_size > 0) {
        char buffer[BUFFER_SIZE];
        int bytes;
        while ((bytes = read(fd, buffer, BUFFER_SIZE)) > 0) {
            if (send(sock, buffer, bytes, 0) < 0) {
                printf("S1: Error sending file data\n");
                break;
            }
        }
    }
    close(fd);
}

void receive_file(int sock, const char *filepath) {
    off_t file_size;
    if (recv(sock, &file_size, sizeof(file_size), 0) <= 0) {
        printf("S1: Error receiving file size\n");
        return;
    }
    if (file_size == 0) {
        printf("S1: No file data to receive\n");
        return;
    }
    int fd = open(filepath, O_WRONLY | O_CREAT | O_TRUNC, 0666);
    if (fd < 0) {
        printf("S1: Error creating file %s\n", filepath);
        return;
    }
    char buffer[BUFFER_SIZE];
    off_t total_bytes = 0;
    int bytes;
    while (total_bytes < file_size && (bytes = recv(sock, buffer, BUFFER_SIZE, 0)) > 0) {
        if (write(fd, buffer, bytes) < 0) {
            printf("S1: Error writing file data\n");
            break;
        }
        total_bytes += bytes;
    }
    close(fd);
}

int main() {
    char s1_dir[PATH_MAX_SIZE];
    int n = snprintf(s1_dir, sizeof(s1_dir), "%s/S1", getenv("HOME"));
    if (n >= sizeof(s1_dir)) {
        printf("S1: Error: Directory path too long\n");
        return 1;
    }
    mkdir(s1_dir, 0777);
    printf("S1: Directory %s created\n", s1_dir);
    int server_sock = socket(AF_INET, SOCK_STREAM, 0);
    struct sockaddr_in server_addr = {0};
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = INADDR_ANY;
    server_addr.sin_port = htons(PORT);
    bind(server_sock, (struct sockaddr*)&server_addr, sizeof(server_addr));
    listen(server_sock, 5);
    printf("S1: Listening on port %d\n", PORT);
    while (1) {
        int client_sock = accept(server_sock, NULL, NULL);
        printf("S1: Client connected\n");
        if (fork() == 0) {
            close(server_sock);
            prcclient(client_sock);
            exit(0);
        }
        close(client_sock);
    }
    return 0;
}
