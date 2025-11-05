#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <libgen.h>

#define SERVER_IP "127.0.0.1"
#define SERVER_PORT 8080
#define BUFFER_SIZE 1024
#define MAX_FILENAME 256
#define MAX_PATH 1024

// Function to check if file exists
int file_exists(const char* filename) {
    struct stat st;
    return (stat(filename, &st) == 0);
}

// Function to get file extension
const char* get_file_extension(const char* filename) {
    const char* dot = strrchr(filename, '.');
    if (!dot || dot == filename)
        return "";
    return dot + 1;
}

// Function to connect to the server (S1)
int connect_to_server() {
    int sock = 0;
    struct sockaddr_in serv_addr;
    
    if ((sock = socket(AF_INET, SOCK_STREAM, 0)) < 0) {
        printf("Error: Socket creation error\n");
        return -1;
    }
    
    serv_addr.sin_family = AF_INET;
    serv_addr.sin_port = htons(SERVER_PORT);
    
    if (inet_pton(AF_INET, SERVER_IP, &serv_addr.sin_addr) <= 0) {
        printf("Error: Invalid address/ Address not supported\n");
        return -1;
    }
    
    if (connect(sock, (struct sockaddr*)&serv_addr, sizeof(serv_addr)) < 0) {
        printf("Error: Connection failed\n");
        return -1;
    }
    
    return sock;
}

// Function to upload file to the server
void upload_file(const char* filename, const char* dest_path) {
    char buffer[BUFFER_SIZE];
    char cmd[BUFFER_SIZE];
    FILE* file;
    struct stat st;
    long filesize;
    int bytes_read;
    int sock;
    
    // Check if file exists
    if (!file_exists(filename)) {
        printf("Error: File %s not found\n", filename);
        return;
    }
    
    // Check if file has valid extension (.c, .pdf, .txt, .zip)
    const char* ext = get_file_extension(filename);
    if (strcmp(ext, "c") != 0 && strcmp(ext, "pdf") != 0 && strcmp(ext, "txt") != 0 && strcmp(ext, "zip") != 0) {
        printf("Error: Unsupported file extension: %s\n", ext);
        return;
    }
    
    // Connect to server
    sock = connect_to_server();
    if (sock < 0) {
        return;
    }
    
    // Send command to server
    snprintf(cmd, BUFFER_SIZE, "uploadf %s %s", filename, dest_path);
    send(sock, cmd, strlen(cmd), 0);
    
    // Get file size
    stat(filename, &st);
    filesize = st.st_size;
    
    // Send file size to server
    sprintf(buffer, "%ld", filesize);
    send(sock, buffer, strlen(buffer), 0);
    
    // Wait for server to be ready
    memset(buffer, 0, BUFFER_SIZE);
    recv(sock, buffer, BUFFER_SIZE, 0);
    
    if (strcmp(buffer, "READY") != 0) {
        printf("Error: Server not ready to receive file\n");
        close(sock);
        return;
    }
    
    // Send file content
    file = fopen(filename, "rb");
    if (!file) {
        printf("Error: Cannot open file %s\n", filename);
        close(sock);
        return;
    }
    
    while ((bytes_read = fread(buffer, 1, BUFFER_SIZE, file)) > 0) {
        send(sock, buffer, bytes_read, 0);
        memset(buffer, 0, BUFFER_SIZE);
    }
    
    fclose(file);
    
    // Get response from server
    memset(buffer, 0, BUFFER_SIZE);
    recv(sock, buffer, BUFFER_SIZE, 0);
    
    printf("%s\n", buffer);
    
    close(sock);
}

// Function to download file from the server
void download_file(const char* filename) {
    char buffer[BUFFER_SIZE];
    char cmd[BUFFER_SIZE];
    FILE* file;
    long filesize;
    int bytes_read, total_bytes = 0;
    int sock;
    
    // Connect to server
    sock = connect_to_server();
    if (sock < 0) {
        return;
    }
    
    // Send command to server
    snprintf(cmd, BUFFER_SIZE, "downlf %s", filename);
    send(sock, cmd, strlen(cmd), 0);
    
    // Get file size from server
    memset(buffer, 0, BUFFER_SIZE);
    recv(sock, buffer, BUFFER_SIZE, 0);
    
    if (strncmp(buffer, "ERROR", 5) == 0) {
        printf("%s\n", buffer);
        close(sock);
        return;
    }
    
    filesize = atol(buffer);
    
    // Extract filename from path
    char* base_filename = basename((char*)filename);
    
    // Open file for writing
    file = fopen(base_filename, "wb");
    if (!file) {
        printf("Error: Cannot create file %s\n", base_filename);
        close(sock);
        return;
    }
    
    // Tell server we're ready to receive the file content
    strcpy(buffer, "READY");
    send(sock, buffer, strlen(buffer), 0);
    
    // Receive file content
    while (total_bytes < filesize) {
        memset(buffer, 0, BUFFER_SIZE);
        bytes_read = recv(sock, buffer, BUFFER_SIZE, 0);
        
        if (bytes_read <= 0)
            break;
        
        fwrite(buffer, 1, bytes_read, file);
        total_bytes += bytes_read;
    }
    
    fclose(file);
    
    printf("File %s downloaded successfully\n", base_filename);
    
    close(sock);
}

// Function to remove file from the server
void remove_file(const char* filename) {
    char buffer[BUFFER_SIZE];
    char cmd[BUFFER_SIZE];
    int sock;
    
    // Connect to server
    sock = connect_to_server();
    if (sock < 0) {
        return;
    }
    
    // Send command to server
    snprintf(cmd, BUFFER_SIZE, "removef %s", filename);
    send(sock, cmd, strlen(cmd), 0);
    
    // Get response from server
    memset(buffer, 0, BUFFER_SIZE);
    recv(sock, buffer, BUFFER_SIZE, 0);
    
    printf("%s\n", buffer);
    
    close(sock);
}

// Function to download tar file of specified file type
void download_tar(const char* filetype) {
    char buffer[BUFFER_SIZE];
    char cmd[BUFFER_SIZE];
    FILE* file;
    long filesize;
    int bytes_read, total_bytes = 0;
    int sock;
    char tar_filename[MAX_FILENAME];
    
    // Check if file type is valid (.c, .pdf, .txt)
    if (strcmp(filetype, "c") != 0 && strcmp(filetype, "pdf") != 0 && strcmp(filetype, "txt") != 0) {
        printf("Error: Unsupported file type: %s\n", filetype);
        return;
    }
    
    // Connect to server
    sock = connect_to_server();
    if (sock < 0) {
        return;
    }
    
    // Send command to server
    snprintf(cmd, BUFFER_SIZE, "downltar %s", filetype);
    send(sock, cmd, strlen(cmd), 0);
    
    // Get file size from server
    memset(buffer, 0, BUFFER_SIZE);
    recv(sock, buffer, BUFFER_SIZE, 0);
    
    if (strncmp(buffer, "ERROR", 5) == 0) {
        printf("%s\n", buffer);
        close(sock);
        return;
    }
    
    filesize = atol(buffer);
    
    // Determine tar file name based on file type
    if (strcmp(filetype, "c") == 0) {
        strcpy(tar_filename, "cfiles.tar");
    } else if (strcmp(filetype, "pdf") == 0) {
        strcpy(tar_filename, "pdf.tar");
    } else if (strcmp(filetype, "txt") == 0) {
        strcpy(tar_filename, "text.tar");
    }
    
    // Open file for writing
    file = fopen(tar_filename, "wb");
    if (!file) {
        printf("Error: Cannot create file %s\n", tar_filename);
        close(sock);
        return;
    }
    
    // Tell server we're ready to receive the file content
    strcpy(buffer, "READY");
    send(sock, buffer, strlen(buffer), 0);
    
    // Receive file content
    while (total_bytes < filesize) {
        memset(buffer, 0, BUFFER_SIZE);
        bytes_read = recv(sock, buffer, BUFFER_SIZE, 0);
        
        if (bytes_read <= 0)
            break;
        
        fwrite(buffer, 1, bytes_read, file);
        total_bytes += bytes_read;
    }
    
    fclose(file);
    
    printf("Tar file %s downloaded successfully\n", tar_filename);
    
    close(sock);
}

// Function to display filenames in specified path
void display_filenames(const char* pathname) {
    char buffer[BUFFER_SIZE];
    char cmd[BUFFER_SIZE];
    int sock;
    
    // Connect to server
    sock = connect_to_server();
    if (sock < 0) {
        return;
    }
    
    // Send command to server
    snprintf(cmd, BUFFER_SIZE, "dispfnames %s", pathname);
    send(sock, cmd, strlen(cmd), 0);
    
    // Get response from server
    memset(buffer, 0, BUFFER_SIZE);
    recv(sock, buffer, BUFFER_SIZE, 0);
    
    printf("%s\n", buffer);
    
    close(sock);
}

void print_usage() {
    printf("Available commands:\n");
    printf("  uploadf filename destination_path\n");
    printf("  downlf filename\n");
    printf("  removef filename\n");
    printf("  downltar filetype\n");
    printf("  dispfnames pathname\n");
}

int main() {
    char cmd[BUFFER_SIZE];
    char arg1[MAX_PATH];
    char arg2[MAX_PATH];
    
    printf("Welcome to w25clients\n");
    
    while (1) {
        printf("w25clients$ ");
        fflush(stdout);
        
        // Clear buffers
        memset(cmd, 0, sizeof(cmd));
        memset(arg1, 0, sizeof(arg1));
        memset(arg2, 0, sizeof(arg2));
        
        // Get user input
        char input[BUFFER_SIZE];
        if (fgets(input, BUFFER_SIZE, stdin) == NULL) {
            break;
        }
        
        // Remove trailing newline
        input[strcspn(input, "\n")] = 0;
        
        // Parse command
        int args = sscanf(input, "%s %s %s", cmd, arg1, arg2);
        
        if (args < 1) {
            continue;
        }
        
        // Process command
        if (strcmp(cmd, "uploadf") == 0) {
            if (args != 3) {
                printf("Error: Invalid command syntax\n");
                printf("Usage: uploadf filename destination_path\n");
            } else {
                upload_file(arg1, arg2);
            }
        } else if (strcmp(cmd, "downlf") == 0) {
            if (args != 2) {
                printf("Error: Invalid command syntax\n");
                printf("Usage: downlf filename\n");
            } else {
                download_file(arg1);
            }
        } else if (strcmp(cmd, "removef") == 0) {
            if (args != 2) {
                printf("Error: Invalid command syntax\n");
                printf("Usage: removef filename\n");
            } else {
                remove_file(arg1);
            }
        } else if (strcmp(cmd, "downltar") == 0) {
            if (args != 2) {
                printf("Error: Invalid command syntax\n");
                printf("Usage: downltar filetype\n");
            } else {
                download_tar(arg1);
            }
        } else if (strcmp(cmd, "dispfnames") == 0) {
            if (args != 2) {
                printf("Error: Invalid command syntax\n");
                printf("Usage: dispfnames pathname\n");
            } else {
                display_filenames(arg1);
            }
        } else if (strcmp(cmd, "help") == 0) {
            print_usage();
        } else if (strcmp(cmd, "exit") == 0) {
            break;
        } else {
            printf("Error: Unknown command: %s\n", cmd);
            print_usage();
        }
    }
    
    return 0;
}