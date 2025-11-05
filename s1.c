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
#include <sys/wait.h>
#include <libgen.h>

#define PORT 8080
#define S2_PORT 8081
#define S3_PORT 8082
#define S4_PORT 8083
#define BUFFER_SIZE 1024
#define MAX_FILENAME 256
#define MAX_PATH 1024

// Structure to store file information
typedef struct {
    char filename[MAX_FILENAME];
    char extension[10];
} FileInfo;

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

// Function to connect to S2, S3 or S4
int connect_to_server(int port) {
    int sock = 0;
    struct sockaddr_in serv_addr;
    
    if ((sock = socket(AF_INET, SOCK_STREAM, 0)) < 0) {
        perror("Socket creation error");
        return -1;
    }
    
    serv_addr.sin_family = AF_INET;
    serv_addr.sin_port = htons(port);
    
    if (inet_pton(AF_INET, "127.0.0.1", &serv_addr.sin_addr) <= 0) {
        perror("Invalid address/ Address not supported");
        return -1;
    }
    
    if (connect(sock, (struct sockaddr*)&serv_addr, sizeof(serv_addr)) < 0) {
        perror("Connection Failed");
        return -1;
    }
    
    return sock;
}

// Function to upload file to appropriate server based on extension
void upload_file(int client_sock, char* filename, char* dest_path) {
    char buffer[BUFFER_SIZE];
    char full_path[MAX_PATH];
    char response[BUFFER_SIZE];
    char cmd[BUFFER_SIZE];
    const char* ext;
    FILE* file;
    struct stat st = {0};
    long filesize;
    int bytes_read, total_bytes = 0;
    int server_sock = -1;
    int port = -1;
    
    // First, receive the file from client
    memset(buffer, 0, BUFFER_SIZE);
    recv(client_sock, buffer, BUFFER_SIZE, 0);
    filesize = atol(buffer);
    
    snprintf(full_path, sizeof(full_path), "%s", dest_path);
    create_directory_recursive(full_path);
    
    // Extract filename from the full path
    char* base_filename = basename(filename);
    
    // Append filename to destination path
    strcat(full_path, "/");
    strcat(full_path, base_filename);
    
    // Open file for writing
    file = fopen(full_path, "wb");
    if (!file) {
        snprintf(response, BUFFER_SIZE, "ERROR: Cannot create file %s", full_path);
        send(client_sock, response, strlen(response), 0);
        return;
    }
    
    // Tell client we're ready to receive the file content
    strcpy(response, "READY");
    send(client_sock, response, strlen(response), 0);
    
    // Receive file content
    total_bytes = 0;
    memset(buffer, 0, BUFFER_SIZE);
    
    while (total_bytes < filesize && (bytes_read = recv(client_sock, buffer, BUFFER_SIZE, 0)) > 0) {
        fwrite(buffer, 1, bytes_read, file);
        total_bytes += bytes_read;
        
        if (total_bytes >= filesize)
            break;
        
        memset(buffer, 0, BUFFER_SIZE);
    }
    
    fclose(file);
    
    // Get file extension
    ext = get_file_extension(base_filename);
    
    // Determine if file needs to be transferred to another server
    if (strcmp(ext, "c") == 0) {
        // .c files stay on S1
        snprintf(response, BUFFER_SIZE, "File %s uploaded successfully to S1", base_filename);
    } else {
        // Determine which server to transfer the file to
        if (strcmp(ext, "pdf") == 0) {
            port = S2_PORT;
            server_sock = connect_to_server(port);
            snprintf(cmd, BUFFER_SIZE, "RECV_FILE %s %s", full_path, dest_path);
        } else if (strcmp(ext, "txt") == 0) {
            port = S3_PORT;
            server_sock = connect_to_server(port);
            snprintf(cmd, BUFFER_SIZE, "RECV_FILE %s %s", full_path, dest_path);
        } else if (strcmp(ext, "zip") == 0) {
            port = S4_PORT;
            server_sock = connect_to_server(port);
            snprintf(cmd, BUFFER_SIZE, "RECV_FILE %s %s", full_path, dest_path);
        } else {
            snprintf(response, BUFFER_SIZE, "ERROR: Unsupported file extension: %s", ext);
            send(client_sock, response, strlen(response), 0);
            return;
        }
        
        if (server_sock < 0) {
            snprintf(response, BUFFER_SIZE, "ERROR: Failed to connect to server for extension %s", ext);
            send(client_sock, response, strlen(response), 0);
            return;
        }
        
        // Send command to server
        send(server_sock, cmd, strlen(cmd), 0);
        
        // Wait for server to be ready
        memset(buffer, 0, BUFFER_SIZE);
        recv(server_sock, buffer, BUFFER_SIZE, 0);
        
        if (strcmp(buffer, "READY") != 0) {
            snprintf(response, BUFFER_SIZE, "ERROR: Server not ready to receive file");
            send(client_sock, response, strlen(response), 0);
            close(server_sock);
            return;
        }
        
        // Send file size
        sprintf(buffer, "%ld", filesize);
        send(server_sock, buffer, strlen(buffer), 0);
        
        // Wait for server to be ready
        memset(buffer, 0, BUFFER_SIZE);
        recv(server_sock, buffer, BUFFER_SIZE, 0);
        
        if (strcmp(buffer, "READY") != 0) {
            snprintf(response, BUFFER_SIZE, "ERROR: Server not ready to receive file content");
            send(client_sock, response, strlen(response), 0);
            close(server_sock);
            return;
        }
        
        // Send file content
        file = fopen(full_path, "rb");
        if (!file) {
            snprintf(response, BUFFER_SIZE, "ERROR: Cannot open file %s for reading", full_path);
            send(client_sock, response, strlen(response), 0);
            close(server_sock);
            return;
        }
        
        while ((bytes_read = fread(buffer, 1, BUFFER_SIZE, file)) > 0) {
            send(server_sock, buffer, bytes_read, 0);
            memset(buffer, 0, BUFFER_SIZE);
        }
        
        fclose(file);
        
        // Get response from server
        memset(buffer, 0, BUFFER_SIZE);
        recv(server_sock, buffer, BUFFER_SIZE, 0);
        
        close(server_sock);
        
        // Delete the file from S1 as it has been transferred
        remove(full_path);
        
        // Construct response for client
        if (strncmp(buffer, "ERROR", 5) == 0) {
            snprintf(response, BUFFER_SIZE, "%s", buffer);
        } else {
            if (strcmp(ext, "pdf") == 0) {
                snprintf(response, BUFFER_SIZE, "File %s uploaded successfully to S1", base_filename);
            } else if (strcmp(ext, "txt") == 0) {
                snprintf(response, BUFFER_SIZE, "File %s uploaded successfully to S1", base_filename);
            } else if (strcmp(ext, "zip") == 0) {
                snprintf(response, BUFFER_SIZE, "File %s uploaded successfully to S1", base_filename);
            }
        }
    }
    
    // Send response to client
    send(client_sock, response, strlen(response), 0);
}

// Function to download file from appropriate server based on path
void download_file(int client_sock, char* filename) {
    char buffer[BUFFER_SIZE];
    char response[BUFFER_SIZE];
    char cmd[BUFFER_SIZE];
    const char* ext;
    FILE* file;
    struct stat st = {0};
    long filesize;
    int bytes_read, bytes_sent;
    int server_sock = -1;
    
    // Extract filename and extension
    char* base_filename = basename(filename);
    ext = get_file_extension(base_filename);
    
    if (strcmp(ext, "c") == 0) {
        // Handle .c files locally
        if (stat(filename, &st) == -1) {
            snprintf(response, BUFFER_SIZE, "ERROR: File %s not found", filename);
            send(client_sock, response, strlen(response), 0);
            return;
        }
        
        filesize = st.st_size;
        
        // Send file size to client
        sprintf(buffer, "%ld", filesize);
        send(client_sock, buffer, strlen(buffer), 0);
        
        // Wait for client to be ready
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
            bytes_sent = send(client_sock, buffer, bytes_read, 0);
            if (bytes_sent < 0) {
                break;
            }
            memset(buffer, 0, BUFFER_SIZE);
        }
        
        fclose(file);
    } else {
        // Determine which server to get the file from
        if (strcmp(ext, "pdf") == 0) {
            server_sock = connect_to_server(S2_PORT);
        } else if (strcmp(ext, "txt") == 0) {
            server_sock = connect_to_server(S3_PORT);
        } else if (strcmp(ext, "zip") == 0) {
            server_sock = connect_to_server(S4_PORT);
        } else {
            snprintf(response, BUFFER_SIZE, "ERROR: Unsupported file extension: %s", ext);
            send(client_sock, response, strlen(response), 0);
            return;
        }
        
        if (server_sock < 0) {
            snprintf(response, BUFFER_SIZE, "ERROR: Failed to connect to server for extension %s", ext);
            send(client_sock, response, strlen(response), 0);
            return;
        }
        
        // Replace S1 with S2, S3, or S4 in the path
        char modified_path[MAX_PATH];
        strcpy(modified_path, filename);
        
        if (strncmp(modified_path, "~/S1", 4) == 0) {
            if (strcmp(ext, "pdf") == 0) {
                modified_path[3] = '2';  // Replace S1 with S2
            } else if (strcmp(ext, "txt") == 0) {
                modified_path[3] = '3';  // Replace S1 with S3
            } else if (strcmp(ext, "zip") == 0) {
                modified_path[3] = '4';  // Replace S1 with S4
            }
        }
        
        // Send request to server
        snprintf(cmd, BUFFER_SIZE, "SEND_FILE %s", modified_path);
        send(server_sock, cmd, strlen(cmd), 0);
        
        // Get file size from server
        memset(buffer, 0, BUFFER_SIZE);
        recv(server_sock, buffer, BUFFER_SIZE, 0);
        
        if (strncmp(buffer, "ERROR", 5) == 0) {
            send(client_sock, buffer, strlen(buffer), 0);
            close(server_sock);
            return;
        }
        
        filesize = atol(buffer);
        
        // Send file size to client
        send(client_sock, buffer, strlen(buffer), 0);
        
        // Wait for client to be ready
        memset(buffer, 0, BUFFER_SIZE);
        recv(client_sock, buffer, BUFFER_SIZE, 0);
        
        if (strcmp(buffer, "READY") != 0) {
            close(server_sock);
            return;
        }
        
        // Tell server we're ready
        strcpy(buffer, "READY");
        send(server_sock, buffer, strlen(buffer), 0);
        
        // Forward file content from server to client
        long total_received = 0;
        
        while (total_received < filesize) {
            memset(buffer, 0, BUFFER_SIZE);
            bytes_read = recv(server_sock, buffer, BUFFER_SIZE, 0);
            
            if (bytes_read <= 0)
                break;
            
            bytes_sent = send(client_sock, buffer, bytes_read, 0);
            if (bytes_sent < 0)
                break;
            
            total_received += bytes_read;
        }
        
        close(server_sock);
    }
}

// Function to remove file from appropriate server based on path
void remove_file(int client_sock, char* filename) {
    char buffer[BUFFER_SIZE];
    char response[BUFFER_SIZE];
    char cmd[BUFFER_SIZE];
    const char* ext;
    int server_sock = -1;
    
    // Extract filename and extension
    char* base_filename = basename(filename);
    ext = get_file_extension(base_filename);
    
    if (strcmp(ext, "c") == 0) {
        // Handle .c files locally
        if (remove(filename) != 0) {
            snprintf(response, BUFFER_SIZE, "ERROR: Failed to remove file %s", filename);
        } else {
            snprintf(response, BUFFER_SIZE, "File %s removed successfully", filename);
        }
        
        send(client_sock, response, strlen(response), 0);
    } else {
        // Determine which server to connect to
        if (strcmp(ext, "pdf") == 0) {
            server_sock = connect_to_server(S2_PORT);
        } else if (strcmp(ext, "txt") == 0) {
            server_sock = connect_to_server(S3_PORT);
        } else if (strcmp(ext, "zip") == 0) {
            server_sock = connect_to_server(S4_PORT);
        } else {
            snprintf(response, BUFFER_SIZE, "ERROR: Unsupported file extension: %s", ext);
            send(client_sock, response, strlen(response), 0);
            return;
        }
        
        if (server_sock < 0) {
            snprintf(response, BUFFER_SIZE, "ERROR: Failed to connect to server for extension %s", ext);
            send(client_sock, response, strlen(response), 0);
            return;
        }
        
        // Replace S1 with S2, S3, or S4 in the path
        char modified_path[MAX_PATH];
        strcpy(modified_path, filename);
        
        if (strncmp(modified_path, "~/S1", 4) == 0) {
            if (strcmp(ext, "pdf") == 0) {
                modified_path[3] = '2';  // Replace S1 with S2
            } else if (strcmp(ext, "txt") == 0) {
                modified_path[3] = '3';  // Replace S1 with S3
            } else if (strcmp(ext, "zip") == 0) {
                modified_path[3] = '4';  // Replace S1 with S4
            }
        }
        
        // Send request to server
        snprintf(cmd, BUFFER_SIZE, "REMOVE_FILE %s", modified_path);
        send(server_sock, cmd, strlen(cmd), 0);
        
        // Get response from server
        memset(buffer, 0, BUFFER_SIZE);
        recv(server_sock, buffer, BUFFER_SIZE, 0);
        
        close(server_sock);
        
        // Forward response to client
        send(client_sock, buffer, strlen(buffer), 0);
    }
}

// Function to download tar file of specified file type
void download_tar(int client_sock, char* filetype) {
    char buffer[BUFFER_SIZE];
    char response[BUFFER_SIZE];
    char cmd[BUFFER_SIZE];
    char tar_path[MAX_PATH];
    FILE* file;
    long filesize;
    int bytes_read, bytes_sent;
    int server_sock = -1;
    
    if (strcmp(filetype, "c") == 0) {
        // Create tar of .c files locally
        sprintf(tar_path, "/tmp/cfiles.tar");
        sprintf(cmd, "find ~/S1 -name \"*.c\" -type f | tar -cf %s -T -", tar_path);
        
        if (system(cmd) != 0) {
            snprintf(response, BUFFER_SIZE, "ERROR: Failed to create tar of .c files");
            send(client_sock, response, strlen(response), 0);
            return;
        }
        
        // Get file size
        struct stat st;
        if (stat(tar_path, &st) == -1) {
            snprintf(response, BUFFER_SIZE, "ERROR: Failed to get tar file size");
            send(client_sock, response, strlen(response), 0);
            return;
        }
        
        filesize = st.st_size;
        
        // Send file size to client
        sprintf(buffer, "%ld", filesize);
        send(client_sock, buffer, strlen(buffer), 0);
        
        // Wait for client to be ready
        memset(buffer, 0, BUFFER_SIZE);
        recv(client_sock, buffer, BUFFER_SIZE, 0);
        
        if (strcmp(buffer, "READY") != 0) {
            return;
        }
        
        // Send file content
        file = fopen(tar_path, "rb");
        if (!file) {
            snprintf(response, BUFFER_SIZE, "ERROR: Cannot open tar file");
            send(client_sock, response, strlen(response), 0);
            return;
        }
        
        while ((bytes_read = fread(buffer, 1, BUFFER_SIZE, file)) > 0) {
            bytes_sent = send(client_sock, buffer, bytes_read, 0);
            if (bytes_sent < 0) {
                break;
            }
            memset(buffer, 0, BUFFER_SIZE);
        }
        
        fclose(file);
        remove(tar_path);  // Clean up
    } else {
        // Determine which server to connect to
        if (strcmp(filetype, "pdf") == 0) {
            server_sock = connect_to_server(S2_PORT);
        } else if (strcmp(filetype, "txt") == 0) {
            server_sock = connect_to_server(S3_PORT);
        } else {
            snprintf(response, BUFFER_SIZE, "ERROR: Unsupported file type: %s", filetype);
            send(client_sock, response, strlen(response), 0);
            return;
        }
        
        if (server_sock < 0) {
            snprintf(response, BUFFER_SIZE, "ERROR: Failed to connect to server for file type %s", filetype);
            send(client_sock, response, strlen(response), 0);
            return;
        }
        
        // Send request to server
        snprintf(cmd, BUFFER_SIZE, "SEND_TAR %s", filetype);
        send(server_sock, cmd, strlen(cmd), 0);
        
        // Get file size from server
        memset(buffer, 0, BUFFER_SIZE);
        recv(server_sock, buffer, BUFFER_SIZE, 0);
        
        if (strncmp(buffer, "ERROR", 5) == 0) {
            send(client_sock, buffer, strlen(buffer), 0);
            close(server_sock);
            return;
        }
        
        filesize = atol(buffer);
        
        // Send file size to client
        send(client_sock, buffer, strlen(buffer), 0);
        
        // Wait for client to be ready
        memset(buffer, 0, BUFFER_SIZE);
        recv(client_sock, buffer, BUFFER_SIZE, 0);
        
        if (strcmp(buffer, "READY") != 0) {
            close(server_sock);
            return;
        }
        
        // Tell server we're ready
        strcpy(buffer, "READY");
        send(server_sock, buffer, strlen(buffer), 0);
        
        // Forward file content from server to client
        long total_received = 0;
        
        while (total_received < filesize) {
            memset(buffer, 0, BUFFER_SIZE);
            bytes_read = recv(server_sock, buffer, BUFFER_SIZE, 0);
            
            if (bytes_read <= 0)
                break;
            
            bytes_sent = send(client_sock, buffer, bytes_read, 0);
            if (bytes_sent < 0)
                break;
            
            total_received += bytes_read;
        }
        
        close(server_sock);
    }
}

// Function to compare two file infos for sorting alphabetically
int compare_file_info(const void* a, const void* b) {
    return strcmp(((FileInfo*)a)->filename, ((FileInfo*)b)->filename);
}

// Function to display file names in specified path
void display_filenames(int client_sock, char* pathname) {
    char buffer[BUFFER_SIZE];
    char response[BUFFER_SIZE * 8]; // Larger buffer for collected filenames
    char cmd[BUFFER_SIZE];
    DIR* dir;
    struct dirent* ent;
    int server_sock;
    
    // Check if directory exists
    dir = opendir(pathname);
    if (!dir) {
        snprintf(response, BUFFER_SIZE, "ERROR: Directory %s not found", pathname);
        send(client_sock, response, strlen(response), 0);
        return;
    }
    
    closedir(dir);
    
    // Get .c files from S1
    FileInfo* files = NULL;
    int file_count = 0;
    int max_files = 100; // Initial capacity
    
    files = (FileInfo*)malloc(max_files * sizeof(FileInfo));
    if (!files) {
        snprintf(response, BUFFER_SIZE, "ERROR: Memory allocation failed");
        send(client_sock, response, strlen(response), 0);
        return;
    }
    
    // Get local .c files
    dir = opendir(pathname);
    while ((ent = readdir(dir)) != NULL) {
        if (ent->d_type == DT_REG) {
            const char* ext = get_file_extension(ent->d_name);
            if (strcmp(ext, "c") == 0) {
                if (file_count >= max_files) {
                    max_files *= 2;
                    files = (FileInfo*)realloc(files, max_files * sizeof(FileInfo));
                    if (!files) {
                        snprintf(response, BUFFER_SIZE, "ERROR: Memory allocation failed");
                        send(client_sock, response, strlen(response), 0);
                        closedir(dir);
                        return;
                    }
                }
                
                strcpy(files[file_count].filename, ent->d_name);
                strcpy(files[file_count].extension, "c");
                file_count++;
            }
        }
    }
    closedir(dir);
    
    // Replace S1 with S2, S3, and S4 in the path to get files from other servers
    char modified_path[MAX_PATH];
    
    // Get .pdf files from S2
    strcpy(modified_path, pathname);
    if (strncmp(modified_path, "~/S1", 4) == 0) {
        modified_path[3] = '2';  // Replace S1 with S2
    }
    
    server_sock = connect_to_server(S2_PORT);
    if (server_sock >= 0) {
        snprintf(cmd, BUFFER_SIZE, "LIST_FILES %s pdf", modified_path);
        send(server_sock, cmd, strlen(cmd), 0);
        
        memset(buffer, 0, BUFFER_SIZE);
        recv(server_sock, buffer, BUFFER_SIZE, 0);
        
        if (strncmp(buffer, "ERROR", 5) != 0) {
            // Parse the response
            char* token = strtok(buffer, ",");
            while (token != NULL) {
                if (file_count >= max_files) {
                    max_files *= 2;
                    files = (FileInfo*)realloc(files, max_files * sizeof(FileInfo));
                    if (!files) {
                        snprintf(response, BUFFER_SIZE, "ERROR: Memory allocation failed");
                        send(client_sock, response, strlen(response), 0);
                        close(server_sock);
                        return;
                    }
                }
                
                strcpy(files[file_count].filename, token);
                strcpy(files[file_count].extension, "pdf");
                file_count++;
                
                token = strtok(NULL, ",");
            }
        }
        
        close(server_sock);
    }
    
    // Get .txt files from S3
    strcpy(modified_path, pathname);
    if (strncmp(modified_path, "~/S1", 4) == 0) {
        modified_path[3] = '3';  // Replace S1 with S3
    }
    
    server_sock = connect_to_server(S3_PORT);
    if (server_sock >= 0) {
        snprintf(cmd, BUFFER_SIZE, "LIST_FILES %s txt", modified_path);
        send(server_sock, cmd, strlen(cmd), 0);
        
        memset(buffer, 0, BUFFER_SIZE);
        recv(server_sock, buffer, BUFFER_SIZE, 0);
        
        if (strncmp(buffer, "ERROR", 5) != 0) {
            // Parse the response
            char* token = strtok(buffer, ",");
            while (token != NULL) {
                if (file_count >= max_files) {
                    max_files *= 2;
                    files = (FileInfo*)realloc(files, max_files * sizeof(FileInfo));
                    if (!files) {
                        snprintf(response, BUFFER_SIZE, "ERROR: Memory allocation failed");
                        send(client_sock, response, strlen(response), 0);
                        close(server_sock);
                        return;
                    }
                }
                
                strcpy(files[file_count].filename, token);
                strcpy(files[file_count].extension, "txt");
                file_count++;
                
                token = strtok(NULL, ",");
            }
        }
        
        close(server_sock);
    }
    
    // Get .zip files from S4
    strcpy(modified_path, pathname);
    if (strncmp(modified_path, "~/S1", 4) == 0) {
        modified_path[3] = '4';  // Replace S1 with S4
    }
    
    server_sock = connect_to_server(S4_PORT);
    if (server_sock >= 0) {
        snprintf(cmd, BUFFER_SIZE, "LIST_FILES %s zip", modified_path);
        send(server_sock, cmd, strlen(cmd), 0);
        
        memset(buffer, 0, BUFFER_SIZE);
        recv(server_sock, buffer, BUFFER_SIZE, 0);
        
        if (strncmp(buffer, "ERROR", 5) != 0) {
            // Parse the response
            char* token = strtok(buffer, ",");
            while (token != NULL) {
                if (file_count >= max_files) {
                    max_files *= 2;
                    files = (FileInfo*)realloc(files, max_files * sizeof(FileInfo));
                    if (!files) {
                        snprintf(response, BUFFER_SIZE, "ERROR: Memory allocation failed");
                        send(client_sock, response, strlen(response), 0);
                        close(server_sock);
                        return;
                    }
                }
                
                strcpy(files[file_count].filename, token);
                strcpy(files[file_count].extension, "zip");
                file_count++;
                
                token = strtok(NULL, ",");
            }
        }
        
        close(server_sock);
    }
    
    // Sort files by extension group and then alphabetically
    qsort(files, file_count, sizeof(FileInfo), compare_file_info);
    
    // Build response
    memset(response, 0, BUFFER_SIZE * 8);
    
    if (file_count == 0) {
        strcpy(response, "No files found in the specified directory");
    } else {
        // Add .c files first
        // Add .c files first
        strcat(response, "Files in directory:\n");
        
        // Group files by extension and list them in order: .c, .pdf, .txt, .zip
        char current_ext[10];
        
        // Process .c files
        strcpy(current_ext, "c");
        for (int i = 0; i < file_count; i++) {
            if (strcmp(files[i].extension, current_ext) == 0) {
                strcat(response, files[i].filename);
                strcat(response, "\n");
            }
        }
        
        // Process .pdf files
        strcpy(current_ext, "pdf");
        for (int i = 0; i < file_count; i++) {
            if (strcmp(files[i].extension, current_ext) == 0) {
                strcat(response, files[i].filename);
                strcat(response, "\n");
            }
        }
        
        // Process .txt files
        strcpy(current_ext, "txt");
        for (int i = 0; i < file_count; i++) {
            if (strcmp(files[i].extension, current_ext) == 0) {
                strcat(response, files[i].filename);
                strcat(response, "\n");
            }
        }
        
        // Process .zip files
        strcpy(current_ext, "zip");
        for (int i = 0; i < file_count; i++) {
            if (strcmp(files[i].extension, current_ext) == 0) {
                strcat(response, files[i].filename);
                strcat(response, "\n");
            }
        }
    }
    
    free(files);
    
    // Send response to client
    send(client_sock, response, strlen(response), 0);
}

// Function to handle client
void prcclient(int client_sock) {
    char buffer[BUFFER_SIZE];
    char response[BUFFER_SIZE];
    char cmd[32], arg1[MAX_PATH], arg2[MAX_PATH];
    int read_size;
    
    while (1) {
        // Clear buffers
        memset(buffer, 0, BUFFER_SIZE);
        memset(response, 0, BUFFER_SIZE);
        memset(cmd, 0, sizeof(cmd));
        memset(arg1, 0, sizeof(arg1));
        memset(arg2, 0, sizeof(arg2));
        
        // Receive command from client
        read_size = recv(client_sock, buffer, BUFFER_SIZE, 0);
        
        if (read_size <= 0) {
            // Client disconnected or error
            break;
        }
        
        printf("Received command: %s\n", buffer);
        
        // Parse command
        int args = sscanf(buffer, "%s %s %s", cmd, arg1, arg2);
        
        if (strcmp(cmd, "uploadf") == 0) {
            // Upload file
            if (args < 3) {
                strcpy(response, "ERROR: Invalid command syntax. Usage: uploadf filename destination_path");
                send(client_sock, response, strlen(response), 0);
            } else {
                upload_file(client_sock, arg1, arg2);
            }
        } else if (strcmp(cmd, "downlf") == 0) {
            // Download file
            if (args < 2) {
                strcpy(response, "ERROR: Invalid command syntax. Usage: downlf filename");
                send(client_sock, response, strlen(response), 0);
            } else {
                download_file(client_sock, arg1);
            }
        } else if (strcmp(cmd, "removef") == 0) {
            // Remove file
            if (args < 2) {
                strcpy(response, "ERROR: Invalid command syntax. Usage: removef filename");
                send(client_sock, response, strlen(response), 0);
            } else {
                remove_file(client_sock, arg1);
            }
        } else if (strcmp(cmd, "downltar") == 0) {
            // Download tar
            if (args < 2) {
                strcpy(response, "ERROR: Invalid command syntax. Usage: downltar filetype");
                send(client_sock, response, strlen(response), 0);
            } else {
                download_tar(client_sock, arg1);
            }
        } else if (strcmp(cmd, "dispfnames") == 0) {
            // Display filenames
            if (args < 2) {
                strcpy(response, "ERROR: Invalid command syntax. Usage: dispfnames pathname");
                send(client_sock, response, strlen(response), 0);
            } else {
                display_filenames(client_sock, arg1);
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
    pid_t pid;
    
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
    
    printf("Server S1 started. Listening on port %d...\n", PORT);
    
    // Create ~/S1 directory if it doesn't exist
    char s1_dir[MAX_PATH];
    snprintf(s1_dir, sizeof(s1_dir), "%s/S1", getenv("HOME"));
    mkdir(s1_dir, 0755);
    
    // Accept connections and fork for each client
    while (1) {
        if ((client_sock = accept(server_fd, (struct sockaddr*)&address, (socklen_t*)&addrlen)) < 0) {
            perror("accept failed");
            continue;
        }
        
        printf("New client connected\n");
        
        // Fork to handle client
        pid = fork();
        
        if (pid < 0) {
            perror("fork failed");
            close(client_sock);
        } else if (pid == 0) {
            // Child process
            close(server_fd);
            prcclient(client_sock);
            exit(0);
        } else {
            // Parent process
            close(client_sock);
        }
    }
    
    return 0;
}