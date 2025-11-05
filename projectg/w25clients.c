#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <fcntl.h>

#define PORT 8080
#define BUFFER_SIZE 1024
#define PATH_MAX_SIZE 4096

void send_file(int sock, const char *filepath);
void receive_file(int sock, const char *filepath);

int main() {
    int sock = socket(AF_INET, SOCK_STREAM, 0);
    struct sockaddr_in server_addr = {0};
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = inet_addr("127.0.0.1");
    server_addr.sin_port = htons(PORT);
    if (connect(sock, (struct sockaddr*)&server_addr, sizeof(server_addr)) < 0) {
        printf("Client: Failed to connect to S1\n");
        return 1;
    }
    printf("Client: Connected to S1 on port %d\n", PORT);

    char command[BUFFER_SIZE];
    while (1) {
        printf("w25clients$ ");
        if (!fgets(command, BUFFER_SIZE, stdin)) {
            printf("Client: Error reading input\n");
            continue;
        }
        command[strcspn(command, "\n")] = 0;
        char cmd[10], arg1[256], arg2[256];
        sscanf(command, "%s %s %s", cmd, arg1, arg2);

        if (strcmp(cmd, "uploadf") != 0 && strcmp(cmd, "downlf") != 0 &&
            strcmp(cmd, "removef") != 0 && strcmp(cmd, "downltar") != 0 &&
            strcmp(cmd, "dispfnames") != 0) {
            printf("Client: Invalid command\n");
            continue;
        }

        send(sock, command, strlen(command), 0);
        printf("Client: Sent command: %s\n", command);

        if (strcmp(cmd, "uploadf") == 0) {
            send_file(sock, arg1);
            printf("Client: File %s sent to S1\n", arg1);
            char response[BUFFER_SIZE];
            int bytes_received = recv(sock, response, BUFFER_SIZE - 1, 0);
            if (bytes_received > 0) {
                response[bytes_received] = '\0';
                printf("Client: Received response: %s\n", response);
            } else {
                printf("Client: No response received from S1\n");
            }
        } else if (strcmp(cmd, "downlf") == 0) {
            char *filename = strrchr(arg1, '/') ? strrchr(arg1, '/') + 1 : arg1;
            receive_file(sock, filename);
            printf("Client: File %s downloaded\n", filename);
        } else if (strcmp(cmd, "removef") == 0) {
            char response[BUFFER_SIZE];
            int bytes_received = recv(sock, response, BUFFER_SIZE - 1, 0);
            if (bytes_received > 0) {
                response[bytes_received] = '\0';
                printf("Client: Received response: %s\n", response);
            } else {
                printf("Client: No response received from S1\n");
            }
        } else if (strcmp(cmd, "downltar") == 0) {
            char tarfile[PATH_MAX_SIZE];
            int n = snprintf(tarfile, sizeof(tarfile), "%s.tar", arg1 + 1);
            if (n >= sizeof(tarfile)) {
                printf("Client: Error: Tarfile name too long\n");
                continue;
            }
            receive_file(sock, tarfile);
            printf("Client: Tar file %s downloaded\n", tarfile);
        } else if (strcmp(cmd, "dispfnames") == 0) {
            char filelist[BUFFER_SIZE];
            int bytes_received = recv(sock, filelist, BUFFER_SIZE - 1, 0);
            if (bytes_received > 0) {
                filelist[bytes_received] = '\0';
                printf("Client: Received file list:\n%s", filelist);
            } else {
                printf("Client: No file list received from S1\n");
            }
        }
    }
    close(sock);
    printf("Client: Connection closed\n");
    return 0;
}

void send_file(int sock, const char *filepath) {
    int fd = open(filepath, O_RDONLY);
    off_t file_size = 0;
    if (fd < 0) {
        printf("Client: Error opening file %s\n", filepath);
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
                printf("Client: Error sending file data\n");
                break;
            }
        }
    }
    close(fd);
}

void receive_file(int sock, const char *filepath) {
    off_t file_size;
    int bytes_received = recv(sock, &file_size, sizeof(file_size), 0);
    if (bytes_received <= 0 || file_size == 0) {
        printf("Client: No file data received\n");
        return;
    }
    int fd = open(filepath, O_WRONLY | O_CREAT | O_TRUNC, 0666);
    if (fd < 0) {
        printf("Client: Error creating file %s\n", filepath);
        return;
    }
    char buffer[BUFFER_SIZE];
    off_t total_bytes = 0;
    int bytes;
    while (total_bytes < file_size && (bytes = recv(sock, buffer, BUFFER_SIZE, 0)) > 0) {
        if (write(fd, buffer, bytes) < 0) {
            printf("Client: Error writing file data\n");
            break;
        }
        total_bytes += bytes;
    }
    close(fd);
}
