#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <arpa/inet.h>
#include <dirent.h>
#include <time.h>
#include <errno.h>

#define PORT_S3 8088
#define BUFFER_SIZE 4096
#define MAX_PATH 1024
#define S3_DIR "./S3"
#define LOG_FILE "S3.log"

void log_message(const char *msg);
void receive_file(int socket, const char *filepath);
void send_file(int socket, const char *filepath);
void create_directory(const char *path);
void list_files(const char *path, char *file_list, size_t list_size);
void tar_files(const char *filetype, const char *output_tar);

int main() {
    int server_fd, client_socket;
    struct sockaddr_in address;
    int addrlen = sizeof(address);

    mkdir(S3_DIR, 0777);
    FILE *log_fp = fopen(LOG_FILE, "a");
    if (!log_fp) { perror("Log file failed"); exit(EXIT_FAILURE); }
    fclose(log_fp);

    if ((server_fd = socket(AF_INET, SOCK_STREAM, 0)) == 0) {
        perror("Socket failed"); log_message("Socket failed"); exit(EXIT_FAILURE);
    }

    address.sin_family = AF_INET;
    address.sin_addr.s_addr = INADDR_ANY;
    address.sin_port = htons(PORT_S3);

    if (bind(server_fd, (struct sockaddr *)&address, sizeof(address)) < 0) {
        perror("Bind failed"); log_message("Bind failed"); close(server_fd); exit(EXIT_FAILURE);
    }

    if (listen(server_fd, 10) < 0) {
        perror("Listen failed"); log_message("Listen failed"); close(server_fd); exit(EXIT_FAILURE);
    }

    log_message("S3 started, listening on port 9082");
    printf("S3 listening on port %d...\n", PORT_S3);

    while (1) {
        if ((client_socket = accept(server_fd, (struct sockaddr *)&address, (socklen_t*)&addrlen)) < 0) {
            perror("Accept failed"); log_message("Accept failed"); continue;
        }

        log_message("New connection from S1");
        char buffer[BUFFER_SIZE] = {0};
        int valread = read(client_socket, buffer, BUFFER_SIZE - 1);
        if (valread <= 0) { close(client_socket); continue; }
        buffer[valread] = '\0';
        log_message("Received command: "); log_message(buffer);

        char command[BUFFER_SIZE], arg1[BUFFER_SIZE], arg2[BUFFER_SIZE];
        sscanf(buffer, "%s %s %s", command, arg1, arg2);

        if (strcmp(command, "uploadf") == 0) {
            char filepath[MAX_PATH];
            snprintf(filepath, MAX_PATH, "%s/%s", S3_DIR, arg2);
            log_message("Attempting to create directory and receive file at: ");
            log_message(filepath);
            create_directory(filepath);
            receive_file(client_socket, filepath);
        } else if (strcmp(command, "downlf") == 0) {
            char filepath[MAX_PATH];
            snprintf(filepath, MAX_PATH, "%s/%s", S3_DIR, arg1);
            send_file(client_socket, filepath);
        } else if (strcmp(command, "removef") == 0) {
            char filepath[MAX_PATH];
            snprintf(filepath, MAX_PATH, "%s/%s", S3_DIR, arg1);
            if (remove(filepath) == 0) {
                send(client_socket, "SUCCESS: File removed", 21, 0);
            } else {
                send(client_socket, "ERROR: Removal failed", 21, 0);
            }
        } else if (strcmp(command, "downltar") == 0) {
            tar_files("txt", "text.tar");
            send_file(client_socket, "text.tar");
            remove("text.tar");
        } else if (strcmp(command, "dispfnames") == 0) {
            char file_list[BUFFER_SIZE] = {0};
            list_files(arg1, file_list, BUFFER_SIZE);
            send(client_socket, file_list, strlen(file_list), 0);
        }
        close(client_socket);
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
        log_message("File open failed");
        off_t error_size = -1;
        send(socket, &error_size, sizeof(off_t), 0);
        send(socket, "ERROR: File not found", 21, 0);
        return;
    }

    struct stat st;
    fstat(fd, &st);
    off_t file_size = st.st_size;
    send(socket, &file_size, sizeof(off_t), 0);

    char buffer[BUFFER_SIZE];
    int valread;
    while ((valread = read(fd, buffer, BUFFER_SIZE)) > 0) {
        send(socket, buffer, valread, 0);
    }
    close(fd);
    log_message("File sent successfully");
}

void create_directory(const char *path) {
    char tmp[MAX_PATH];
    snprintf(tmp, MAX_PATH, "%s", path);
    char *p = tmp + strlen(S3_DIR) + 1;
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
        return;
    }

    struct dirent *entry;
    size_t offset = 0;
    while ((entry = readdir(dir)) != NULL) {
        if (entry->d_type == DT_REG && strstr(entry->d_name, ".txt")) {
            offset += snprintf(file_list + offset, list_size - offset, "%s\n", entry->d_name);
            if (offset >= list_size) break;
        }
    }
    closedir(dir);
}

void tar_files(const char *filetype, const char *output_tar) {
    char cmd[BUFFER_SIZE];
    snprintf(cmd, BUFFER_SIZE, "find %s -name '*.%s' | xargs tar -cvf %s", S3_DIR, filetype, output_tar);
    system(cmd);
}
