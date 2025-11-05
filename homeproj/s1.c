#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <sys/wait.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <arpa/inet.h>
#include <dirent.h>
#include <time.h>
#include <errno.h>

#define PORT_S1 8086
#define PORT_S2 8087
#define PORT_S3 8088
#define PORT_S4 8089
#define BUFFER_SIZE 4096
#define MAX_PATH 1024
#define S1_DIR "./S1"
#define S2_IP "127.0.0.1"
#define S3_IP "127.0.0.1"
#define S4_IP "127.0.0.1"
#define LOG_FILE "S1.log"

void log_message(const char *msg);
void send_to_server(const char *ip, int port, const char *command, const char *filepath, char *response, size_t resp_size, int is_file_transfer);
void receive_file(int socket, const char *filepath);
void send_file(int socket, const char *filepath);
void prcclient(int client_socket);
int validate_path(const char *path);
void create_directory(const char *path);
void list_files(const char *path, char *file_list, size_t list_size);
void tar_files(const char *filetype, const char *output_tar);

int main() {
    int server_fd, client_socket;
    struct sockaddr_in address;
    int addrlen = sizeof(address);

    mkdir(S1_DIR, 0777);
    FILE *log_fp = fopen(LOG_FILE, "a");
    if (!log_fp) { perror("Log file failed"); exit(EXIT_FAILURE); }
    fclose(log_fp);

    if ((server_fd = socket(AF_INET, SOCK_STREAM, 0)) == 0) {
        perror("Socket failed"); log_message("Socket failed"); exit(EXIT_FAILURE);
    }

    address.sin_family = AF_INET;
    address.sin_addr.s_addr = INADDR_ANY;
    address.sin_port = htons(PORT_S1);

    int opt = 1;
    setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
    if (bind(server_fd, (struct sockaddr *)&address, sizeof(address)) < 0) {
        perror("Bind failed"); log_message("Bind failed"); close(server_fd); exit(EXIT_FAILURE);
    }

    if (listen(server_fd, 10) < 0) {
        perror("Listen failed"); log_message("Listen failed"); close(server_fd); exit(EXIT_FAILURE);
    }

    log_message("S1 started, listening on port 9085");
    printf("S1 server listening on port %d\n", PORT_S1);

    while (1) {
        if ((client_socket = accept(server_fd, (struct sockaddr *)&address, (socklen_t*)&addrlen)) < 0) {
            perror("Accept failed"); log_message("Accept failed"); continue;
        }

        char client_ip[INET_ADDRSTRLEN];
        inet_ntop(AF_INET, &address.sin_addr, client_ip, INET_ADDRSTRLEN);
        char msg[BUFFER_SIZE];
        snprintf(msg, BUFFER_SIZE, "New connection from %s:%d", client_ip, ntohs(address.sin_port));
        log_message(msg);

        if (fork() == 0) {
            close(server_fd);
            prcclient(client_socket);
            close(client_socket);
            exit(0);
        }
        close(client_socket);
        while (waitpid(-1, NULL, WNOHANG) > 0);
    }
    close(server_fd);
    return 0;
}

void log_message(const char *msg) {
    FILE *log_fp = fopen(LOG_FILE, "a");
    if (!log_fp) return;
    time_t now; time(&now);
    char *time_str = ctime(&now); time_str[strlen(time_str) - 1] = '\0';
    fprintf(log_fp, "[%s] %s\n", time_str, msg);
    fclose(log_fp);
}

void send_to_server(const char *ip, int port, const char *command, const char *filepath, char *response, size_t resp_size, int is_file_transfer) {
    int sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock < 0) {
        snprintf(response, resp_size, "ERROR: Socket failed");
        log_message("Socket creation failed for secondary server");
        return;
    }

    struct sockaddr_in serv_addr = {0};
    serv_addr.sin_family = AF_INET;
    serv_addr.sin_port = htons(port);
    inet_pton(AF_INET, ip, &serv_addr.sin_addr);

    if (connect(sock, (struct sockaddr *)&serv_addr, sizeof(serv_addr)) < 0) {
        snprintf(response, resp_size, "ERROR: Connection failed");
        log_message("Connection to secondary server failed");
        close(sock);
        return;
    }

    send(sock, command, strlen(command), 0);
    usleep(10000); // Small delay for command processing

    if (filepath && is_file_transfer) {
        int fd = open(filepath, O_RDONLY);
        if (fd < 0) {
            snprintf(response, resp_size, "ERROR: File not found");
            log_message("Failed to open file for forwarding");
            close(sock);
            return;
        }
        struct stat st;
        fstat(fd, &st);
        off_t file_size = st.st_size;
        send(sock, &file_size, sizeof(off_t), 0);
        char buffer[BUFFER_SIZE];
        int valread;
        while ((valread = read(fd, buffer, BUFFER_SIZE)) > 0) {
            send(sock, buffer, valread, 0);
        }
        close(fd);
        log_message("File forwarded to secondary server");
    }

    int valread_resp = read(sock, response, resp_size - 1);
    if (valread_resp > 0) {
        response[valread_resp] = '\0';
    } else {
        snprintf(response, resp_size, "ERROR: No response from secondary server");
    }
    close(sock);
}

void receive_file(int socket, const char *filepath) {
    off_t file_size;
    ssize_t size_read = read(socket, &file_size, sizeof(off_t));
    if (size_read != sizeof(off_t)) {
        char msg[BUFFER_SIZE];
        snprintf(msg, BUFFER_SIZE, "Failed to receive file size, read %zd bytes", size_read);
        log_message(msg);
        send(socket, "ERROR: Size not received", 24, 0);
        return;
    }

    log_message("Received file size successfully");
    char buffer[BUFFER_SIZE] = {0};
    int fd = open(filepath, O_WRONLY | O_CREAT | O_TRUNC, 0666);
    if (fd < 0) {
        char msg[BUFFER_SIZE];
        snprintf(msg, BUFFER_SIZE, "File open failed: %s", strerror(errno));
        log_message(msg);
        send(socket, "ERROR: File creation failed", 27, 0);
        return;
    }

    off_t received = 0;
    int valread;
    while (received < file_size && (valread = read(socket, buffer, BUFFER_SIZE)) > 0) {
        if (write(fd, buffer, valread) != valread) {
            log_message("Write failed");
            close(fd);
            send(socket, "ERROR: Write failed", 19, 0);
            return;
        }
        received += valread;
    }
    close(fd);
    if (received == file_size) {
        log_message("File received and written successfully");
        send(socket, "SUCCESS: File uploaded", 22, 0);
    } else {
        char msg[BUFFER_SIZE];
        snprintf(msg, BUFFER_SIZE, "Incomplete file transfer: received %ld of %ld", received, file_size);
        log_message(msg);
        send(socket, "ERROR: Incomplete transfer", 26, 0);
    }
}

void send_file(int socket, const char *filepath) {
    int fd = open(filepath, O_RDONLY);
    if (fd < 0) {
        log_message("File open failed for sending");
        off_t error_size = -1;
        send(socket, &error_size, sizeof(off_t), 0);
        send(socket, "ERROR: File not found", 21, 0);
        return;
    }

    struct stat st;
    fstat(fd, &st);
    off_t file_size = st.st_size;
    send(socket, "READY", 5, 0); // Signal client to prepare for file size
    usleep(10000); // Small delay
    send(socket, &file_size, sizeof(off_t), 0);

    char buffer[BUFFER_SIZE];
    int valread;
    while ((valread = read(fd, buffer, BUFFER_SIZE)) > 0) {
        send(socket, buffer, valread, 0);
    }
    close(fd);
    log_message("File sent successfully");
}

int validate_path(const char *path) {
    return (path && strncmp(path, S1_DIR, strlen(S1_DIR)) == 0);
}

void create_directory(const char *path) {
    char tmp[MAX_PATH];
    snprintf(tmp, MAX_PATH, "%s", path);
    char *p = tmp + strlen(S1_DIR) + 1;
    while (*p) {
        if (*p == '/') {
            *p = '\0';
            mkdir(tmp, 0777);
            *p = '/';
        }
        p++;
    }
    mkdir(tmp, 0777);
    log_message("Directory created or exists");
}

void list_files(const char *path, char *file_list, size_t list_size) {
    DIR *dir = opendir(path);
    if (!dir) {
        snprintf(file_list, list_size, "ERROR: Directory not found");
        log_message("Directory open failed");
        return;
    }

    struct dirent *entry;
    size_t offset = 0;
    while ((entry = readdir(dir)) != NULL) {
        if (entry->d_type == DT_REG) {
            offset += snprintf(file_list + offset, list_size - offset, "%s\n", entry->d_name);
            if (offset >= list_size) break;
        }
    }
    closedir(dir);
    if (offset == 0) snprintf(file_list, list_size, "No files found\n");
    log_message("File list generated");
}

void tar_files(const char *filetype, const char *output_tar) {
    char cmd[BUFFER_SIZE];
    snprintf(cmd, BUFFER_SIZE, "find %s -name \"*.%s\" -type f | tar -cvf %s -T - 2>/dev/null", S1_DIR, filetype, output_tar);
    if (system(cmd) != 0 || access(output_tar, F_OK) != 0) {
        log_message("Tar creation failed or no files found");
        FILE *fp = fopen(output_tar, "w"); // Create empty tar if no files
        if (fp) fclose(fp);
    } else {
        log_message("Tar created successfully");
    }
}

void prcclient(int client_socket) {
    char buffer[BUFFER_SIZE] = {0};
    char response[BUFFER_SIZE] = {0};

    while (1) {
        int valread = read(client_socket, buffer, BUFFER_SIZE - 1);
        if (valread <= 0) {
            log_message("Client disconnected");
            break;
        }
        buffer[valread] = '\0';
        log_message("Received command: "); log_message(buffer);

        char command[BUFFER_SIZE], arg1[BUFFER_SIZE], arg2[BUFFER_SIZE];
        int args = sscanf(buffer, "%s %s %s", command, arg1, arg2);

        if (strcmp(command, "uploadf") == 0 && args == 3) {
            if (!validate_path(arg2)) {
                send(client_socket, "ERROR: Invalid path", 19, 0);
                continue;
            }
            char filepath[MAX_PATH];
            snprintf(filepath, MAX_PATH, "%s/%s", arg2, arg1);
            log_message("Attempting to receive file at: ");
            log_message(filepath);
            create_directory(arg2);
            receive_file(client_socket, filepath);

            if (strstr(arg1, ".c")) {
                // File already stored in S1, response sent in receive_file
            } else if (strstr(arg1, ".pdf")) {
                log_message("Forwarding .pdf file to S2");
                send_to_server(S2_IP, PORT_S2, buffer, filepath, response, BUFFER_SIZE, 1);
                send(client_socket, response, strlen(response), 0);
                remove(filepath);
            } else if (strstr(arg1, ".txt")) {
                log_message("Forwarding .txt file to S3");
                send_to_server(S3_IP, PORT_S3, buffer, filepath, response, BUFFER_SIZE, 1);
                send(client_socket, response, strlen(response), 0);
                remove(filepath);
            } else if (strstr(arg1, ".zip")) {
                log_message("Forwarding .zip file to S4");
                send_to_server(S4_IP, PORT_S4, buffer, filepath, response, BUFFER_SIZE, 1);
                send(client_socket, response, strlen(response), 0);
                remove(filepath);
            } else {
                send(client_socket, "ERROR: Invalid file type", 24, 0);
                remove(filepath);
            }
        } else if (strcmp(command, "downlf") == 0 && args == 2) {
            if (!validate_path(arg1)) {
                send(client_socket, "ERROR: Invalid path", 19, 0);
                continue;
            }
            if (strstr(arg1, ".c")) {
                send_file(client_socket, arg1);
            } else if (strstr(arg1, ".pdf")) {
                send_to_server(S2_IP, PORT_S2, buffer, NULL, response, BUFFER_SIZE, 0);
                if (strncmp(response, "READY", 5) == 0) {
                    send(client_socket, "READY", 5, 0);
                    int sock = socket(AF_INET, SOCK_STREAM, 0);
                    struct sockaddr_in serv_addr = { .sin_family = AF_INET, .sin_port = htons(PORT_S2) };
                    inet_pton(AF_INET, S2_IP, &serv_addr.sin_addr);
                    connect(sock, (struct sockaddr *)&serv_addr, sizeof(serv_addr));
                    send(sock, buffer, strlen(buffer), 0);
                    receive_file(client_socket, arg1); // Forward file to client
                    close(sock);
                } else {
                    send(client_socket, response, strlen(response), 0);
                }
            } else if (strstr(arg1, ".txt")) {
                send_to_server(S3_IP, PORT_S3, buffer, NULL, response, BUFFER_SIZE, 0);
                if (strncmp(response, "READY", 5) == 0) {
                    send(client_socket, "READY", 5, 0);
                    int sock = socket(AF_INET, SOCK_STREAM, 0);
                    struct sockaddr_in serv_addr = { .sin_family = AF_INET, .sin_port = htons(PORT_S3) };
                    inet_pton(AF_INET, S3_IP, &serv_addr.sin_addr);
                    connect(sock, (struct sockaddr *)&serv_addr, sizeof(serv_addr));
                    send(sock, buffer, strlen(buffer), 0);
                    receive_file(client_socket, arg1); // Forward file to client
                    close(sock);
                } else {
                    send(client_socket, response, strlen(response), 0);
                }
            } else if (strstr(arg1, ".zip")) {
                send_to_server(S4_IP, PORT_S4, buffer, NULL, response, BUFFER_SIZE, 0);
                if (strncmp(response, "READY", 5) == 0) {
                    send(client_socket, "READY", 5, 0);
                    int sock = socket(AF_INET, SOCK_STREAM, 0);
                    struct sockaddr_in serv_addr = { .sin_family = AF_INET, .sin_port = htons(PORT_S4) };
                    inet_pton(AF_INET, S4_IP, &serv_addr.sin_addr);
                    connect(sock, (struct sockaddr *)&serv_addr, sizeof(serv_addr));
                    send(sock, buffer, strlen(buffer), 0);
                    receive_file(client_socket, arg1); // Forward file to client
                    close(sock);
                } else {
                    send(client_socket, response, strlen(response), 0);
                }
            } else {
                send(client_socket, "ERROR: Invalid file type", 24, 0);
            }
        } else if (strcmp(command, "removef") == 0 && args == 2) {
            if (!validate_path(arg1)) {
                send(client_socket, "ERROR: Invalid path", 19, 0);
                continue;
            }
            if (strstr(arg1, ".c")) {
                if (remove(arg1) == 0) {
                    send(client_socket, "SUCCESS: File removed", 21, 0);
                } else {
                    send(client_socket, "ERROR: Removal failed", 21, 0);
                }
            } else if (strstr(arg1, ".pdf")) {
                send_to_server(S2_IP, PORT_S2, buffer, NULL, response, BUFFER_SIZE, 0);
                send(client_socket, response, strlen(response), 0);
            } else if (strstr(arg1, ".txt")) {
                send_to_server(S3_IP, PORT_S3, buffer, NULL, response, BUFFER_SIZE, 0);
                send(client_socket, response, strlen(response), 0);
            } else if (strstr(arg1, ".zip")) {
                send_to_server(S4_IP, PORT_S4, buffer, NULL, response, BUFFER_SIZE, 0);
                send(client_socket, response, strlen(response), 0);
            }
        } else if (strcmp(command, "downltar") == 0 && args == 2) {
            if (strcmp(arg1, "c") == 0) { // Changed from ".c" to "c"
                tar_files("c", "cfiles.tar");
                if (access("cfiles.tar", F_OK) == 0) {
                    send_file(client_socket, "cfiles.tar");
                    remove("cfiles.tar");
                } else {
                    send(client_socket, "ERROR: No files to tar", 22, 0);
                }
            } else if (strcmp(arg1, "pdf") == 0) { // Changed from ".pdf" to "pdf"
                send_to_server(S2_IP, PORT_S2, buffer, NULL, response, BUFFER_SIZE, 0);
                if (strncmp(response, "READY", 5) == 0) {
                    send(client_socket, "READY", 5, 0);
                    int sock = socket(AF_INET, SOCK_STREAM, 0);
                    struct sockaddr_in serv_addr = { .sin_family = AF_INET, .sin_port = htons(PORT_S2) };
                    inet_pton(AF_INET, S2_IP, &serv_addr.sin_addr);
                    connect(sock, (struct sockaddr *)&serv_addr, sizeof(serv_addr));
                    send(sock, buffer, strlen(buffer), 0);
                    receive_file(client_socket, "pdf.tar"); // Forward tar to client
                    close(sock);
                } else {
                    send(client_socket, response, strlen(response), 0);
                }
            } else if (strcmp(arg1, "txt") == 0) { // Changed from ".txt" to "txt"
                send_to_server(S3_IP, PORT_S3, buffer, NULL, response, BUFFER_SIZE, 0);
                if (strncmp(response, "READY", 5) == 0) {
                    send(client_socket, "READY", 5, 0);
                    int sock = socket(AF_INET, SOCK_STREAM, 0);
                    struct sockaddr_in serv_addr = { .sin_family = AF_INET, .sin_port = htons(PORT_S3) };
                    inet_pton(AF_INET, S3_IP, &serv_addr.sin_addr);
                    connect(sock, (struct sockaddr *)&serv_addr, sizeof(serv_addr));
                    send(sock, buffer, strlen(buffer), 0);
                    receive_file(client_socket, "text.tar"); // Forward tar to client
                    close(sock);
                } else {
                    send(client_socket, response, strlen(response), 0);
                }
            } else if (strcmp(arg1, "zip") == 0) { // Changed from ".zip" to "zip"
                send_to_server(S4_IP, PORT_S4, buffer, NULL, response, BUFFER_SIZE, 0);
                if (strncmp(response, "READY", 5) == 0) {
                    send(client_socket, "READY", 5, 0);
                    int sock = socket(AF_INET, SOCK_STREAM, 0);
                    struct sockaddr_in serv_addr = { .sin_family = AF_INET, .sin_port = htons(PORT_S4) };
                    inet_pton(AF_INET, S4_IP, &serv_addr.sin_addr);
                    connect(sock, (struct sockaddr *)&serv_addr, sizeof(serv_addr));
                    send(sock, buffer, strlen(buffer), 0);
                    receive_file(client_socket, "zip.tar"); // Forward tar to client
                    close(sock);
                } else {
                    send(client_socket, response, strlen(response), 0);
                }
            } else {
                send(client_socket, "ERROR: Invalid filetype", 23, 0);
            }
        } else if (strcmp(command, "dispfnames") == 0 && args == 2) {
            if (!validate_path(arg1)) {
                send(client_socket, "ERROR: Invalid path", 19, 0);
                continue;
            }
            char file_list[BUFFER_SIZE * 4] = "Files in directory:\n"; // Larger buffer
            size_t offset = strlen(file_list);

            // List files from S1
            list_files(arg1, file_list + offset, BUFFER_SIZE * 4 - offset);
            offset = strlen(file_list);

            // Aggregate from secondary servers
            char temp_response[BUFFER_SIZE];
            send_to_server(S2_IP, PORT_S2, buffer, NULL, temp_response, BUFFER_SIZE, 0);
            if (strncmp(temp_response, "ERROR", 5) != 0) {
                offset += snprintf(file_list + offset, BUFFER_SIZE * 4 - offset, "%s", temp_response);
            }

            send_to_server(S3_IP, PORT_S3, buffer, NULL, temp_response, BUFFER_SIZE, 0);
            if (strncmp(temp_response, "ERROR", 5) != 0) {
                offset += snprintf(file_list + offset, BUFFER_SIZE * 4 - offset, "%s", temp_response);
            }

            send_to_server(S4_IP, PORT_S4, buffer, NULL, temp_response, BUFFER_SIZE, 0);
            if (strncmp(temp_response, "ERROR", 5) != 0) {
                offset += snprintf(file_list + offset, BUFFER_SIZE * 4 - offset, "%s", temp_response);
            }

            if (offset == strlen("Files in directory:\n")) {
                snprintf(file_list, BUFFER_SIZE * 4, "No files found\n");
            }
            send(client_socket, file_list, strlen(file_list), 0);
            log_message("File list sent to client");
        } else {
            send(client_socket, "ERROR: Invalid command", 22, 0);
        }
    }
}
