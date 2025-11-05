#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <dirent.h>

#define PORT 8081
#define BUFFER_SIZE 1024
#define PATH_MAX_SIZE 4096

void send_file(int sock, const char *filepath);
void receive_file(int sock, const char *filepath);

int main() {
    char s2_dir[PATH_MAX_SIZE];
    int n = snprintf(s2_dir, sizeof(s2_dir), "%s/S2", getenv("HOME"));
    if (n >= sizeof(s2_dir)) {
        printf("S2: Error: Directory path too long\n");
        return 1;
    }
    mkdir(s2_dir, 0777);
    printf("S2: Directory %s created\n", s2_dir);
    int server_sock = socket(AF_INET, SOCK_STREAM, 0);
    struct sockaddr_in server_addr = {0};
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = INADDR_ANY;
    server_addr.sin_port = htons(PORT);
    bind(server_sock, (struct sockaddr*)&server_addr, sizeof(server_addr));
    listen(server_sock, 5);
    printf("S2: Listening on port %d\n", PORT);
    while (1) {
        int s1_sock = accept(server_sock, NULL, NULL);
        printf("S2: S1 connected\n");
        char buffer[BUFFER_SIZE];
        int bytes_received = recv(s1_sock, buffer, BUFFER_SIZE - 1, 0);
        if (bytes_received <= 0) {
            close(s1_sock);
            printf("S2: Connection with S1 closed\n");
            continue;
        }
        buffer[bytes_received] = '\0';
        printf("S2: Received command: %s\n", buffer);
        char command[10], arg1[256], arg2[256];
        sscanf(buffer, "%s %s %s", command, arg1, arg2);

        if (strcmp(command, "uploadf") == 0) {
            char filepath[PATH_MAX_SIZE];
            n = snprintf(filepath, sizeof(filepath), "%s/%s/%s", getenv("HOME"), arg2, arg1);
            if (n >= sizeof(filepath)) {
                printf("S2: Error: Filepath too long\n");
            } else {
                char dirpath[PATH_MAX_SIZE];
                snprintf(dirpath, sizeof(dirpath), "%s/%s", getenv("HOME"), arg2);
                mkdir(dirpath, 0777);
                receive_file(s1_sock, filepath);
                printf("S2: File %s stored\n", filepath);
            }
        } else if (strcmp(command, "downlf") == 0) {
            char filepath[PATH_MAX_SIZE];
            n = snprintf(filepath, sizeof(filepath), "%s/%s", getenv("HOME"), arg1);
            if (n >= sizeof(filepath)) {
                printf("S2: Error: Filepath too long\n");
            } else {
                send_file(s1_sock, filepath);
                printf("S2: File %s sent to S1\n", filepath);
            }
        } else if (strcmp(command, "removef") == 0) {
            char filepath[PATH_MAX_SIZE];
            n = snprintf(filepath, sizeof(filepath), "%s/%s", getenv("HOME"), arg1);
            if (n >= sizeof(filepath)) {
                printf("S2: Error: Filepath too long\n");
            } else {
                remove(filepath);
                printf("S2: File %s deleted\n", filepath);
            }
        } else if (strcmp(command, "downltar") == 0) {
            char tarfile[PATH_MAX_SIZE], cmd[PATH_MAX_SIZE];
            n = snprintf(tarfile, sizeof(tarfile), "%s/S2/pdf.tar", getenv("HOME"));
            if (n >= sizeof(tarfile)) {
                printf("S2: Error: Tarfile path too long\n");
            } else {
                n = snprintf(cmd, sizeof(cmd), "tar -cvf %s %s/S2/*.pdf", tarfile, getenv("HOME"));
                if (n >= sizeof(cmd)) {
                    printf("S2: Error: Tar command too long\n");
                } else {
                    system(cmd);
                    printf("S2: Created tar file %s\n", tarfile);
                    send_file(s1_sock, tarfile);
                    printf("S2: Sent %s to S1\n", tarfile);
                    remove(tarfile);
                    printf("S2: Deleted %s\n", tarfile);
                }
            }
        } else if (strcmp(command, "dispfnames") == 0) {
            char filepath[PATH_MAX_SIZE];
            n = snprintf(filepath, sizeof(filepath), "%s/%s", getenv("HOME"), arg1);
            if (n >= sizeof(filepath)) {
                printf("S2: Error: Filepath too long\n");
                send(s1_sock, "", 1, 0);
            } else {
                DIR *dir = opendir(filepath);
                if (!dir) {
                    printf("S2: Error: Directory %s not found\n", filepath);
                    send(s1_sock, "", 1, 0);
                } else {
                    struct dirent *entry;
                    char filelist[BUFFER_SIZE] = "";
                    while ((entry = readdir(dir)) != NULL) {
                        if (strstr(entry->d_name, ".pdf")) {
                            strncat(filelist, entry->d_name, BUFFER_SIZE - strlen(filelist) - 1);
                            strncat(filelist, "\n", BUFFER_SIZE - strlen(filelist) - 1);
                        }
                    }
                    closedir(dir);
                    send(s1_sock, filelist, strlen(filelist), 0);
                    printf("S2: Sent file list to S1\n");
                }
            }
        }
        close(s1_sock);
        printf("S2: Connection with S1 closed\n");
    }
    return 0;
}

void send_file(int sock, const char *filepath) {
    int fd = open(filepath, O_RDONLY);
    off_t file_size = 0;
    if (fd < 0) {
        printf("S2: Error opening file %s\n", filepath);
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
                printf("S2: Error sending file data\n");
                break;
            }
        }
    }
    close(fd);
}

void receive_file(int sock, const char *filepath) {
    off_t file_size;
    if (recv(sock, &file_size, sizeof(file_size), 0) <= 0) {
        printf("S2: Error receiving file size\n");
        return;
    }
    if (file_size == 0) {
        printf("S2: No file data to receive\n");
        return;
    }
    int fd = open(filepath, O_WRONLY | O_CREAT | O_TRUNC, 0666);
    if (fd < 0) {
        printf("S2: Error creating file %s\n", filepath);
        return;
    }
    char buffer[BUFFER_SIZE];
    off_t total_bytes = 0;
    int bytes;
    while (total_bytes < file_size && (bytes = recv(sock, buffer, BUFFER_SIZE, 0)) > 0) {
        if (write(fd, buffer, bytes) < 0) {
            printf("S2: Error writing file data\n");
            break;
        }
        total_bytes += bytes;
    }
    close(fd);
}
