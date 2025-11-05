#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <dirent.h>
#include <fcntl.h>
#include <errno.h>
#include <libgen.h>

#define PORT 8082
#define BUFFER_SIZE 1024
#define MAX_FILENAME 256
#define MAX_PATH 1024

// Function to create directory recursively
void create_directory_recursive(const char* path) {
    char temp[MAX_PATH];
    char* p = NULL;
    size_t len;
    
    snprintf(temp, sizeof(temp), "%s", path);
    len = strlen(temp);
    
    if (temp[len - 1] == '/')
        temp[len - 1] = 0;
    
    for (p = temp + 1; *p; p++) {
        if (*p == '/') {
            *p = 0;
            mkdir(temp, 0755);
            *p = '/';
        }
    }
    
    mkdir(temp, 0755);
}

// Function to get file extension
const char* get_file_extension(const char* filename) {
    const char* dot = strrchr(filename, '.');
    if (!dot || dot == filename)
        return "";
    return dot + 1;
}

// Function to receive file from S1
void receive_file(int client_sock, char* filename, char* dest_path) {
    char buffer[BUFFER_SIZE];
    char full_path[MAX_PATH];
    char response[BUFFER_SIZE];
    FILE* file;
    long filesize;
    int bytes_read, total_bytes = 0;
    
    // Convert S1 path to S3 path
    if (strncmp(dest_path, "~/S1", 4) == 0) {
        dest_path[3] = '3';  // Replace S1 with S3
    }
    
    // Ensure destination directory exists
    create_directory_recursive(dest_path);
    
    // Extract filename from the full path
    char* base_filename = basename(filename);
    
    // Append filename to destination path
    sprintf(full_path, "%s/%s", dest_path, base_filename);
    
    // Tell S1 we're ready to receive
    strcpy(response, "READY");
    send(client_sock, response, strlen(response), 0);
    
    // Get file size
    memset(buffer, 0, BUFFER_SIZE);
    recv(client_sock, buffer, BUFFER_SIZE, 0);
    filesize = atol(buffer);
    
    // Open file for writing
    file = fopen(full_path, "wb");
    if (!file) {
        snprintf(response, BUFFER_SIZE, "ERROR: Cannot create file %s", full_path);
        send(client_sock, response, strlen(response), 0);
        return;
    }
    
    // Tell S1 we're ready to receive file content
    strcpy(response, "READY");
    send(client_sock, response, strlen(response), 0);
    
    // Receive file content
    total_bytes = 0;
    
    while (total_bytes < filesize) {
        memset(buffer, 0, BUFFER_SIZE);
        bytes_read = recv(client_sock, buffer, BUFFER_SIZE, 0);
        
        if (bytes_read <= 0)
            break;
        
        fwrite(buffer, 1, bytes_read, file);
        total_bytes += bytes_read;
    }
    
    fclose(file);
    
    // Send success response
    snprintf(response, BUFFER_SIZE, "File %s received and stored in S3", base_filename);
    send(client_sock, response, strlen(response), 0);
}

// Function to send file to S1
void send_file(int client_sock, char* filename) {
    char buffer[BUFFER_SIZE];
    char response[BUFFER_SIZE];
    FILE* file;
    struct stat st = {0};
    long filesize;
    int bytes_read;
    
    // Check if file exists
    if (stat(filename, &st) == -1) {
        snprintf(response, BUFFER_SIZE, "ERROR: File %s not found", filename);
        send(client_sock, response, strlen(response), 0);
        return;
    }
    
    filesize = st.st_size;
    
    // Send file size to S1
    sprintf(buffer, "%ld", filesize);
    send(client_sock, buffer, strlen(buffer), 0);
    
    // Wait for S1 to be ready
    memset(buffer, 0, BUFFER_SIZE);
    recv(client_sock, buffer, BUFFER_SIZE, 0);
    
    if (strcmp(buffer, "READY") != 0) {
        return;
    }
    
    // Send file content
    file = fopen(filename, "rb");
    if (!file) {
        snprintf(response, BUFFER_SIZE, "ERROR: Cannot open file %s", filename);
        send(client_sock, response, strlen(response), 0);
        return;
    }
    
    while ((bytes_read = fread(buffer, 1, BUFFER_SIZE, file)) > 0) {
        send(client_sock, buffer, bytes_read, 0);
        memset(buffer, 0, BUFFER_SIZE);
    }
    
    fclose(file);
}

// Function to remove file
void remove_file(int client_sock, char* filename) {
    char response[BUFFER_SIZE];
    
    // Check if file exists and remove it
    if (remove(filename) != 0) {
        snprintf(response, BUFFER_SIZE, "ERROR: Failed to remove file %s", filename);
    } else {
        snprintf(response, BUFFER_SIZE, "File %s removed successfully", filename);
    }
    
    send(client_sock, response, strlen(response), 0);
}

// Function to send tar of files
void send_tar(int client_sock, char* filetype) {
    char buffer[BUFFER_SIZE];
    char tar_path[MAX_PATH];
    char cmd[BUFFER_SIZE];
    FILE* file;
    struct stat st = {0};
    long filesize;
    int bytes_read;
    
    // Create tar file
    sprintf(tar_path, "/tmp/text.tar");
    sprintf(cmd, "find ~/S3 -name \"*.txt\" -type f | tar -cf %s -T -", tar_path);
    
    if (system(cmd) != 0) {
        snprintf(buffer, BUFFER_SIZE, "ERROR: Failed to create tar of .txt files");
        send(client_sock, buffer, strlen(buffer), 0);
        return;
    }
    
    // Get file size
    if (stat(tar_path, &st) == -1) {
        snprintf(buffer, BUFFER_SIZE, "ERROR: Failed to get tar file size");
        send(client_sock, buffer, strlen(buffer), 0);
        return;
    }
    
    filesize = st.st_size;
    
    // Send file size to S1
    sprintf(buffer, "%ld", filesize);
    send(client_sock, buffer, strlen(buffer), 0);
    
    // Wait for S1 to be ready
    memset(buffer, 0, BUFFER_SIZE);
    recv(client_sock, buffer, BUFFER_SIZE, 0);
    
    if (strcmp(buffer, "READY") != 0) {
        return;
    }
    
    // Send file content
    file = fopen(tar_path, "rb");
    if (!file) {
        snprintf(buffer, BUFFER_SIZE, "ERROR: Cannot open tar file");
        send(client_sock, buffer, strlen(buffer), 0);
        return;
    }
    
    while ((bytes_read = fread(buffer, 1, BUFFER_SIZE, file)) > 0) {
        send(client_sock, buffer, bytes_read, 0);
        memset(buffer, 0, BUFFER_SIZE);
    }
    
    fclose(file);
    remove(tar_path);  // Clean up
}

// Function to list files in directory
void list_files(int client_sock, char* pathname, char* filetype) {
    char response[BUFFER_SIZE * 4] = {0};
    DIR* dir;
    struct dirent* ent;
    
    // Check if directory exists
    dir = opendir(pathname);
    if (!dir) {
        snprintf(response, BUFFER_SIZE, "ERROR: Directory %s not found", pathname);
        send(client_sock, response, strlen(response), 0);
        return;
    }
    
    // Get files with the specified extension
    while ((ent = readdir(dir)) != NULL) {
        if (ent->d_type == DT_REG) {
            const char* ext = get_file_extension(ent->d_name);
            if (strcmp(ext, filetype) == 0) {
                if (strlen(response) > 0) {
                    strcat(response, ",");
                }
                strcat(response, ent->d_name);
            }
        }
    }
    
    closedir(dir);
    
    // Send response to S1
    if (strlen(response) == 0) {
        strcpy(response, "No files found");
    }
    
    send(client_sock, response, strlen(response), 0);
}

// Function to handle client (S1)
void handle_client(int client_sock) {
    char buffer[BUFFER_SIZE];
    char response[BUFFER_SIZE];
    char cmd[32], arg1[MAX_PATH], arg2[MAX_PATH];
    int read_size;
    
    while (1) {
        // Clear buffers
        memset(buffer, 0, BUFFER_SIZE);
        memset(cmd, 0, sizeof(cmd));
        memset(arg1, 0, sizeof(arg1));
        memset(arg2, 0, sizeof(arg2));
        
        // Receive command from S1
        read_size = recv(client_sock, buffer, BUFFER_SIZE, 0);
        
        if (read_size <= 0) {
            // S1 disconnected or error
            break;
        }
        
        printf("Received command from S1: %s\n", buffer);
        
        // Parse command
        int args = sscanf(buffer, "%s %s %s", cmd, arg1, arg2);
        
        if (strcmp(cmd, "RECV_FILE") == 0) {
            if (args < 3) {
                strcpy(response, "ERROR: Invalid command syntax");
                send(client_sock, response, strlen(response), 0);
            } else {
                receive_file(client_sock, arg1, arg2);
            }
        } else if (strcmp(cmd, "SEND_FILE") == 0) {
            if (args < 2) {
                strcpy(response, "ERROR: Invalid command syntax");
                send(client_sock, response, strlen(response), 0);
            } else {
                send_file(client_sock, arg1);
            }
        } else if (strcmp(cmd, "REMOVE_FILE") == 0) {
            if (args < 2) {
                strcpy(response, "ERROR: Invalid command syntax");
                send(client_sock, response, strlen(response), 0);
            } else {
                remove_file(client_sock, arg1);
            }
        } else if (strcmp(cmd, "SEND_TAR") == 0) {
            if (args < 2) {
                strcpy(response, "ERROR: Invalid command syntax");
                send(client_sock, response, strlen(response), 0);
            } else {
                send_tar(client_sock, arg1);
            }
        } else if (strcmp(cmd, "LIST_FILES") == 0) {
            if (args < 3) {
                strcpy(response, "ERROR: Invalid command syntax");
                send(client_sock, response, strlen(response), 0);
            } else {
                list_files(client_sock, arg1, arg2);
            }
        } else {
            // Unknown command
            strcpy(response, "ERROR: Unknown command");
            send(client_sock, response, strlen(response), 0);
        }
    }
    
    // Clean up
    close(client_sock);
}

int main() {
    int server_fd, client_sock;
    struct sockaddr_in address;
    int opt = 1;
    int addrlen = sizeof(address);
    
    // Creating socket file descriptor
    if ((server_fd = socket(AF_INET, SOCK_STREAM, 0)) == 0) {
        perror("socket failed");
        exit(EXIT_FAILURE);
    }
    
    // Set socket options
    if (setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR | SO_REUSEPORT, &opt, sizeof(opt))) {
        perror("setsockopt failed");
        exit(EXIT_FAILURE);
    }
    
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = INADDR_ANY;
    address.sin_port = htons(PORT);
    
    // Bind socket to port
    if (bind(server_fd, (struct sockaddr*)&address, sizeof(address)) < 0) {
        perror("bind failed");
        exit(EXIT_FAILURE);
    }
    
    // Listen for connections
    if (listen(server_fd, 10) < 0) {
        perror("listen failed");
        exit(EXIT_FAILURE);
    }
    
    printf("Server S3 started. Listening on port %d...\n", PORT);
    
    // Create ~/S3 directory if it doesn't exist
    char s3_dir[MAX_PATH];
    snprintf(s3_dir, sizeof(s3_dir), "%s/S3", getenv("HOME"));
    mkdir(s3_dir, 0755);
    
    // Accept connections
    while (1) {
        if ((client_sock = accept(server_fd, (struct sockaddr*)&address, (socklen_t*)&addrlen)) < 0) {
            perror("accept failed");
            continue;
        }
        
        printf("S1 connected\n");
        
        // Handle client (S1)
        handle_client(client_sock);
    }
    
    return 0;
}