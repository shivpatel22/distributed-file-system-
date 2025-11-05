#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <fcntl.h>
#include <errno.h>
#include <sys/stat.h>

#define PORT_S1 8086
#define BUFFER_SIZE 4096
#define SERVER_IP "127.0.0.1"
#define MAX_PATH 1024

void send_file(int socket, const char *filepath);
void receive_file(int socket, const char *filename);
int read_full_response(int socket, char *buffer, size_t size);

int main() {
    int sock;
    struct sockaddr_in serv_addr;

    printf("Welcome to w25clients\n");
    while (1) {
        char command[BUFFER_SIZE];
        printf("w25clients$ ");
        if (!fgets(command, BUFFER_SIZE, stdin)) break;
        command[strcspn(command, "\n")] = 0;

        if ((sock = socket(AF_INET, SOCK_STREAM, 0)) < 0) {
            perror("Socket failed");
            continue;
        }

        serv_addr.sin_family = AF_INET;
        serv_addr.sin_port = htons(PORT_S1);
        inet_pton(AF_INET, SERVER_IP, &serv_addr.sin_addr);

        if (connect(sock, (struct sockaddr *)&serv_addr, sizeof(serv_addr)) < 0) {
            perror("Connection failed");
            close(sock);
            continue;
        }

        char cmd[BUFFER_SIZE], arg1[BUFFER_SIZE], arg2[BUFFER_SIZE];
        int args = sscanf(command, "%s %s %s", cmd, arg1, arg2);

        if (strcmp(cmd, "uploadf") == 0 && args == 3) {
            struct stat st;
            if (stat(arg1, &st) < 0) {
                printf("ERROR: File %s does not exist\n", arg1);
                close(sock);
                continue;
            }
            if (send(sock, command, strlen(command), 0) < 0) {
                printf("ERROR: Failed to send command\n");
                close(sock);
                continue;
            }
            usleep(10000); // Small delay for server processing
            send_file(sock, arg1);
            char response[BUFFER_SIZE] = {0};
            int valread = read(sock, response, BUFFER_SIZE - 1);
            if (valread > 0) {
                response[valread] = '\0';
                printf("%s\n", response);
            } else {
                printf("ERROR: No response from server\n");
            }
        } else if (strcmp(cmd, "downlf") == 0 && args == 2) {
            if (send(sock, command, strlen(command), 0) < 0) {
                printf("ERROR: Failed to send command\n");
                close(sock);
                continue;
            }
            char filename[MAX_PATH];
            snprintf(filename, MAX_PATH, "%s", strrchr(arg1, '/') ? strrchr(arg1, '/') + 1 : arg1);
            receive_file(sock, filename);
        } else if (strcmp(cmd, "removef") == 0 && args == 2) {
            if (send(sock, command, strlen(command), 0) < 0) {
                printf("ERROR: Failed to send command\n");
                close(sock);
                continue;
            }
            char response[BUFFER_SIZE] = {0};
            int valread = read(sock, response, BUFFER_SIZE - 1);
            if (valread > 0) {
                response[valread] = '\0';
                printf("%s\n", response);
            } else {
                printf("ERROR: No response from server\n");
            }
        } else if (strcmp(cmd, "downltar") == 0 && args == 2) {
            // Validate and send the command
            if (strcmp(arg1, "c") != 0 && strcmp(arg1, "pdf") != 0 && strcmp(arg1, "txt") != 0 && strcmp(arg1, "zip") != 0) {
                printf("ERROR: Invalid filetype (use: c, pdf, txt, zip)\n");
                close(sock);
                continue;
            }
            if (send(sock, command, strlen(command), 0) < 0) {
                printf("ERROR: Failed to send command\n");
                close(sock);
                continue;
            }

            // Determine the expected tar filename
            char filename[MAX_PATH];
            if (strcmp(arg1, "c") == 0) {
                snprintf(filename, MAX_PATH, "cfiles.tar");
            } else if (strcmp(arg1, "pdf") == 0) {
                snprintf(filename, MAX_PATH, "pdf.tar");
            } else if (strcmp(arg1, "txt") == 0) {
                snprintf(filename, MAX_PATH, "text.tar");
            } else if (strcmp(arg1, "zip") == 0) {
                snprintf(filename, MAX_PATH, "zip.tar");
            }

            // Receive the tar file
            printf("Attempting to download %s...\n", filename);
            receive_file(sock, filename);

            // Verify the file exists after download
            struct stat st;
            if (stat(filename, &st) == 0) {
                printf("SUCCESS: Tar file saved as %s (size: %ld bytes)\n", filename, (long)st.st_size);
            } else {
                printf("ERROR: Tar file %s not found after download\n", filename);
            }
        } else if (strcmp(cmd, "dispfnames") == 0 && args == 2) {
            if (send(sock, command, strlen(command), 0) < 0) {
                printf("ERROR: Failed to send command\n");
                close(sock);
                continue;
            }
            char response[BUFFER_SIZE] = {0};
            int valread = read(sock, response, BUFFER_SIZE - 1);
            if (valread > 0) {
                response[valread] = '\0';
                printf("%s", response);
            } else {
                printf("ERROR: No response from server\n");
            }
            printf("\n");
        } else if (strcmp(cmd, "exit") == 0) {
            close(sock);
            printf("Exiting client...\n");
            break;
        } else {
            printf("Invalid command or arguments\n");
            printf("Commands:\n");
            printf("  uploadf <filename> <destination_path>\n");
            printf("  downlf <filepath>\n");
            printf("  removef <filepath>\n");
            printf("  downltar <filetype> (e.g., c, pdf, txt, zip)\n");
            printf("  dispfnames <path>\n");
            printf("  exit\n");
        }
        close(sock);
    }
    return 0;
}

void send_file(int socket, const char *filepath) {
    int fd = open(filepath, O_RDONLY);
    if (fd < 0) {
        printf("ERROR: File not found - %s\n", strerror(errno));
        off_t error_size = -1;
        send(socket, &error_size, sizeof(off_t), 0);
        return;
    }

    struct stat st;
    fstat(fd, &st);
    off_t file_size = st.st_size;
    if (send(socket, &file_size, sizeof(off_t), 0) != sizeof(off_t)) {
        printf("ERROR: Failed to send file size\n");
        close(fd);
        return;
    }

    char buffer[BUFFER_SIZE];
    int valread;
    while ((valread = read(fd, buffer, BUFFER_SIZE)) > 0) {
        if (send(socket, buffer, valread, 0) != valread) {
            printf("ERROR: Failed to send file data\n");
            close(fd);
            return;
        }
    }
    if (valread < 0) {
        printf("ERROR: Failed to read file - %s\n", strerror(errno));
    }
    close(fd);
    printf("File sent successfully\n");
}

void receive_file(int socket, const char *filename) {
    char ready_signal[6] = {0};
    int valread = read(socket, ready_signal, 5);
    if (valread != 5 || strcmp(ready_signal, "READY") != 0) {
        char buffer[BUFFER_SIZE] = {0};
        valread = read(socket, buffer, BUFFER_SIZE - 1);
        if (valread > 0) {
            buffer[valread] = '\0';
            printf("%s\n", buffer);
        } else {
            printf("ERROR: Failed to receive READY signal or error message\n");
        }
        return;
    }

    off_t file_size;
    if (read(socket, &file_size, sizeof(off_t)) != sizeof(off_t)) {
        printf("ERROR: Failed to receive file size\n");
        return;
    }

    if (file_size == -1) {
        char buffer[BUFFER_SIZE] = {0};
        valread = read(socket, buffer, BUFFER_SIZE - 1);
        if (valread > 0) {
            buffer[valread] = '\0';
            printf("%s\n", buffer);
        }
        return;
    }

    int fd = open(filename, O_WRONLY | O_CREAT | O_TRUNC, 0666);
    if (fd < 0) {
        printf("ERROR: Failed to create file %s - %s\n", filename, strerror(errno));
        return;
    }

    char buffer[BUFFER_SIZE];
    off_t received = 0;
    while (received < file_size && (valread = read(socket, buffer, BUFFER_SIZE)) > 0) {
        if (write(fd, buffer, valread) != valread) {
            printf("ERROR: Failed to write file %s - %s\n", filename, strerror(errno));
            close(fd);
            return;
        }
        received += valread;
    }
    close(fd);

    if (received == file_size) {
        printf("SUCCESS: File downloaded as %s\n", filename);
    } else {
        printf("ERROR: Incomplete file download - received %ld of %ld bytes\n", received, file_size);
    }
}

int read_full_response(int socket, char *buffer, size_t size) {
    int total_read = 0;
    int valread;
    while ((valread = read(socket, buffer + total_read, size - total_read - 1)) > 0) {
        total_read += valread;
        if (total_read >= size - 1) break;
        // Check if the message ends (e.g., with a newline or no more data expected)
        if (strchr(buffer, '\n')) break;
    }
    if (total_read > 0) {
        buffer[total_read] = '\0';
    }
    return total_read;
}
