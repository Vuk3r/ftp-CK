#include "ftp_common.h"
#include <errno.h>
#include <stdarg.h>
#include <strings.h>
#include <sys/select.h>

typedef struct {
    int control_fd;
    char current_dir[FTP_MAX_PATH];
    char server_ip[16];
    int server_port;
    char client_ip[16];
    int client_port;
} client_session_t;

static void server_log(const char *level, const char *fmt, va_list args) {
    fprintf(stderr, "[SERVER %s] ", level);
    vfprintf(stderr, fmt, args);
    fprintf(stderr, "\n");
}

static void server_log_info(const char *fmt, ...) {
    va_list args;
    va_start(args, fmt);
    server_log("INFO", fmt, args);
    va_end(args);
}

static void server_log_error(const char *fmt, ...) {
    va_list args;
    va_start(args, fmt);
    server_log("ERROR", fmt, args);
    va_end(args);
}

void *handle_client(void *arg) {
    client_session_t *session = (client_session_t *)arg;
    int control_fd = session->control_fd;
    char buffer[FTP_MAX_LINE];
    char command[FTP_MAX_LINE];
    char cmd_arg[FTP_MAX_LINE];
    int authenticated = 0;
    int data_fd = -1;
    int pasv_listen_fd = -1;
    int pasv_port = 0;
    
    getcwd(session->current_dir, sizeof(session->current_dir));
    
    server_log_info("Session started with %s:%d", session->client_ip, session->client_port);
    send_ftp_response(control_fd, FTP_READY, "FTP Server Ready");
    
    while (1) {
        if (read_ftp_command(control_fd, buffer, sizeof(buffer)) <= 0) {
            server_log_info("Connection closed or read error for %s:%d", session->client_ip, session->client_port);
            break;
        }
        
        // Parse command
        sscanf(buffer, "%s %[^\r\n]", command, cmd_arg);
        
        if (strcasecmp(command, "USER") == 0) {
            send_ftp_response(control_fd, FTP_NEED_PASSWORD, "Password required");
        }
        else if (strcasecmp(command, "PASS") == 0) {
            authenticated = 1;
            send_ftp_response(control_fd, FTP_LOGIN_SUCCESS, "Login successful");
        }
        else if (strcasecmp(command, "PWD") == 0) {
            char response[FTP_MAX_LINE];
            
            // *** ĐÃ SỬA (Warning) ***
            // Truncate an toàn, trừ 5 byte cho ("" và \0)
            snprintf(response, sizeof(response), "\"%.*s\"", (int)sizeof(response) - 5, session->current_dir);
            
            send_ftp_response(control_fd, FTP_PATHNAME_CREATED, response);
        }
        else if (strcasecmp(command, "CWD") == 0) {
            if (chdir(cmd_arg) == 0) {
                getcwd(session->current_dir, sizeof(session->current_dir));
                send_ftp_response(control_fd, FTP_FILE_ACTION_OK, "Directory changed");
            } else {
                server_log_error("Failed to change directory to '%s' for %s:%d", cmd_arg, session->client_ip, session->client_port);
                send_ftp_response(control_fd, FTP_FILE_NOT_FOUND, "Directory not found");
            }
        }
        else if (strcasecmp(command, "PASV") == 0) {
            // ... (Không thay đổi)
            if (pasv_listen_fd >= 0) {
                close(pasv_listen_fd);
            }
            pasv_listen_fd = socket(AF_INET, SOCK_STREAM, 0);
            struct sockaddr_in addr;
            memset(&addr, 0, sizeof(addr));
            addr.sin_family = AF_INET;
            addr.sin_addr.s_addr = INADDR_ANY;
            addr.sin_port = 0;
            if (bind(pasv_listen_fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
                server_log_error("Failed to enter passive mode: %s", strerror(errno));
                send_ftp_response(control_fd, FTP_ACTION_FAILED, "Cannot enter passive mode");
                continue;
            }
            listen(pasv_listen_fd, 1);
            struct sockaddr_in local_addr;
            socklen_t len = sizeof(local_addr);
            getsockname(pasv_listen_fd, (struct sockaddr *)&local_addr, &len);
            pasv_port = ntohs(local_addr.sin_port);

            struct sockaddr_in ctrl_local;
            socklen_t ctrl_len = sizeof(ctrl_local);
            if (getsockname(control_fd, (struct sockaddr *)&ctrl_local, &ctrl_len) == 0) {
                if (!inet_ntop(AF_INET, &ctrl_local.sin_addr, session->server_ip, sizeof(session->server_ip))) {
                    strncpy(session->server_ip, "127.0.0.1", sizeof(session->server_ip) - 1);
                    session->server_ip[sizeof(session->server_ip) - 1] = '\0';
                }
            }

            struct in_addr ip_addr;
            if (inet_pton(AF_INET, session->server_ip, &ip_addr) <= 0) {
                inet_pton(AF_INET, "127.0.0.1", &ip_addr);
            }
            unsigned char *ip_bytes = (unsigned char *)&ip_addr.s_addr;
            char pasv_response[FTP_MAX_LINE];
            snprintf(pasv_response, sizeof(pasv_response),
                    "227 Entering Passive Mode (%d,%d,%d,%d,%d,%d)",
                    ip_bytes[0], ip_bytes[1], ip_bytes[2], ip_bytes[3],
                    pasv_port / 256, pasv_port % 256);
            send(control_fd, pasv_response, strlen(pasv_response), 0);
            send(control_fd, "\r\n", 2, 0);
            server_log_info("PASV announced %d.%d.%d.%d:%d to %s:%d",
                            ip_bytes[0], ip_bytes[1], ip_bytes[2], ip_bytes[3],
                            pasv_port, session->client_ip, session->client_port);
        }
        else if (strcasecmp(command, "LIST") == 0) {
            // ... (Không thay đổi)
            if (!authenticated) {
                server_log_error("LIST denied for unauthenticated client %s:%d", session->client_ip, session->client_port);
                send_ftp_response(control_fd, FTP_LOGIN_FAILED, "Not logged in");
                continue;
            }
            if (pasv_listen_fd >= 0) {
                fd_set read_fds;
                struct timeval timeout;
                FD_ZERO(&read_fds);
                FD_SET(pasv_listen_fd, &read_fds);
                timeout.tv_sec = 5;
                timeout.tv_usec = 0;
                if (select(pasv_listen_fd + 1, &read_fds, NULL, NULL, &timeout) > 0) {
                    data_fd = accept(pasv_listen_fd, NULL, NULL);
                } else {
                    data_fd = -1;
                }
                close(pasv_listen_fd);
                pasv_listen_fd = -1;
            }
            if (data_fd < 0) {
                server_log_error("LIST data connection failed for %s:%d", session->client_ip, session->client_port);
                send_ftp_response(control_fd, FTP_ACTION_FAILED, "Data connection failed");
                continue;
            }
            send_ftp_response(control_fd, FTP_DATA_CONN_OPEN, "Opening ASCII mode data connection");
            DIR *dir = opendir(".");
            if (dir) {
                struct dirent *entry;
                char list_buffer[FTP_MAX_LINE];
                while ((entry = readdir(dir)) != NULL) {
                    struct stat st;
                    if (stat(entry->d_name, &st) == 0) {
                        if (S_ISDIR(st.st_mode)) {
                            // *** ĐÃ SỬA (Warning) ***
                            // Trừ 50 byte cho phần text cứng, truncate tên file an toàn
                            snprintf(list_buffer, sizeof(list_buffer), "drwxr-xr-x 1 user user %ld %.*s\r\n",
                                    st.st_size, (int)sizeof(list_buffer) - 50, entry->d_name);
                        } else {
                            // *** ĐÃ SỬA (Warning) ***
                            snprintf(list_buffer, sizeof(list_buffer), "-rw-r--r-- 1 user user %ld %.*s\r\n",
                                    st.st_size, (int)sizeof(list_buffer) - 50, entry->d_name);
                        }
                        send(data_fd, list_buffer, strlen(list_buffer), 0);
                    }
                }
                closedir(dir);
            }
            close(data_fd);
            data_fd = -1;
            send_ftp_response(control_fd, FTP_SUCCESS, "Transfer complete");
        }
        else if (strcasecmp(command, "RETR") == 0) {
            if (!authenticated) {
                server_log_error("RETR denied for unauthenticated client %s:%d", session->client_ip, session->client_port);
                send_ftp_response(control_fd, FTP_LOGIN_FAILED, "Not logged in");
                continue;
            }
            
            // Accept data connection
            if (pasv_listen_fd >= 0) {
                // ... (logic accept data_fd không đổi)
                fd_set read_fds;
                struct timeval timeout;
                FD_ZERO(&read_fds);
                FD_SET(pasv_listen_fd, &read_fds);
                timeout.tv_sec = 5;
                timeout.tv_usec = 0;
                if (select(pasv_listen_fd + 1, &read_fds, NULL, NULL, &timeout) > 0) {
                    data_fd = accept(pasv_listen_fd, NULL, NULL);
                } else {
                    data_fd = -1;
                }
                close(pasv_listen_fd);
                pasv_listen_fd = -1;
            }
            
            if (data_fd < 0) {
                server_log_error("RETR data connection failed for %s:%d", session->client_ip, session->client_port);
                send_ftp_response(control_fd, FTP_ACTION_FAILED, "Data connection failed");
                continue;
            }
            
            FILE *file = fopen(cmd_arg, "rb");
            if (file) {
                send_ftp_response(control_fd, FTP_DATA_CONN_OPEN, "Opening BINARY mode data connection");
                
                // Lời gọi này giờ đã khớp với header (không còn warning)
                if (send_file_over_socket(data_fd, file) < 0) {
                    server_log_error("Error sending file '%s' to %s:%d", cmd_arg, session->client_ip, session->client_port);
                    send_ftp_response(control_fd, FTP_ACTION_FAILED, "Error reading file or sending data");
                } else {
                    send_ftp_response(control_fd, FTP_SUCCESS, "Transfer complete");
                }
                
                fclose(file); 
                close(data_fd);
                data_fd = -1;
            } else {
                close(data_fd);
                data_fd = -1;
                server_log_error("File not found for RETR '%s' requested by %s:%d", cmd_arg, session->client_ip, session->client_port);
                send_ftp_response(control_fd, FTP_FILE_NOT_FOUND, "File not found");
            }
        }
        else if (strcasecmp(command, "STOR") == 0) {
            if (!authenticated) {
                server_log_error("STOR denied for unauthenticated client %s:%d", session->client_ip, session->client_port);
                send_ftp_response(control_fd, FTP_LOGIN_FAILED, "Not logged in");
                continue;
            }
            
            // Accept data connection
            if (pasv_listen_fd >= 0) {
                // ... (logic accept data_fd không đổi)
                fd_set read_fds;
                struct timeval timeout;
                FD_ZERO(&read_fds);
                FD_SET(pasv_listen_fd, &read_fds);
                timeout.tv_sec = 5;
                timeout.tv_usec = 0;
                if (select(pasv_listen_fd + 1, &read_fds, NULL, NULL, &timeout) > 0) {
                    data_fd = accept(pasv_listen_fd, NULL, NULL);
                } else {
                    data_fd = -1;
                }
                close(pasv_listen_fd);
                pasv_listen_fd = -1;
            }
            
            if (data_fd < 0) {
                server_log_error("STOR data connection failed for %s:%d", session->client_ip, session->client_port);
                send_ftp_response(control_fd, FTP_ACTION_FAILED, "Data connection failed");
                continue;
            }
            
            FILE *file = fopen(cmd_arg, "wb");
            if (file) {
                send_ftp_response(control_fd, FTP_DATA_CONN_OPEN, "Opening BINARY mode data connection");
                
                // Lời gọi này giờ đã khớp với header (không còn warning)
                if (receive_file_over_socket(data_fd, file) < 0) {
                    fclose(file); 
                    close(data_fd);
                    data_fd = -1;
                    unlink(cmd_arg); 
                    server_log_error("Error receiving file '%s' from %s:%d", cmd_arg, session->client_ip, session->client_port);
                    send_ftp_response(control_fd, FTP_ACTION_FAILED, "Error receiving file or writing data");
                    continue;
                }
                
                fclose(file); 
                close(data_fd);
                data_fd = -1;
                send_ftp_response(control_fd, FTP_SUCCESS, "Transfer complete");
            } else {
                close(data_fd);
                data_fd = -1;
                
                // *** ĐÃ SỬA (Error) ***
                // Đã sửa FTP_FILE_ACTION_FAILED thành FTP_ACTION_FAILED
                server_log_error("Cannot create file '%s' for STOR from %s:%d: %s", cmd_arg, session->client_ip, session->client_port, strerror(errno));
                send_ftp_response(control_fd, FTP_ACTION_FAILED, "Cannot create file");
            }
        }
        else if (strcasecmp(command, "QUIT") == 0) {
            send_ftp_response(control_fd, FTP_GOODBYE, "Goodbye");
            break;
        }
        else {
            // Mặc định là '502 Command not implemented' thay vì '200'
            server_log_error("Unsupported command '%s' from %s:%d", command, session->client_ip, session->client_port);
            send_ftp_response(control_fd, 502, "Command not implemented");
        }
    }
    
    if (pasv_listen_fd >= 0) close(pasv_listen_fd);
    if (data_fd >= 0) close(data_fd);
    close(control_fd);
    server_log_info("Session ended with %s:%d", session->client_ip, session->client_port);
    free(session);
    return NULL;
}

// ... (Hàm start_ftp_server và accept_ftp_client không thay đổi)
int start_ftp_server(const char *bind_ip, int port) {
    int server_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (server_fd < 0) {
        server_log_error("Failed to create server socket: %s", strerror(errno));
        return -1;
    }
    
    int opt = 1;
    setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
    
    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    
    if (bind_ip && strlen(bind_ip) > 0) {
        if (inet_pton(AF_INET, bind_ip, &addr.sin_addr) <= 0) {
            addr.sin_addr.s_addr = INADDR_ANY;
        }
    } else {
        addr.sin_addr.s_addr = INADDR_ANY;
    }
    
    if (bind(server_fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        server_log_error("Failed to bind server socket: %s", strerror(errno));
        close(server_fd);
        return -1;
    }
    
    if (listen(server_fd, 10) < 0) {
        server_log_error("Failed to listen on server socket: %s", strerror(errno));
        close(server_fd);
        return -1;
    }
    
    return server_fd;
}

int accept_ftp_client(int server_fd, const char *server_ip) {
    struct sockaddr_in client_addr;
    socklen_t len = sizeof(client_addr);
    int client_fd = accept(server_fd, (struct sockaddr *)&client_addr, &len);
    
    if (client_fd >= 0) {
        client_session_t *session = malloc(sizeof(client_session_t));
        session->control_fd = client_fd;
        session->server_ip[0] = '\0';
        if (server_ip && strlen(server_ip) > 0 && strcmp(server_ip, "0.0.0.0") != 0) {
            strncpy(session->server_ip, server_ip, sizeof(session->server_ip) - 1);
            session->server_ip[sizeof(session->server_ip) - 1] = '\0';
        } else {
            struct sockaddr_in local_addr;
            socklen_t local_len = sizeof(local_addr);
            if (getsockname(client_fd, (struct sockaddr *)&local_addr, &local_len) == 0) {
                if (!inet_ntop(AF_INET, &local_addr.sin_addr, session->server_ip, sizeof(session->server_ip))) {
                    strncpy(session->server_ip, "127.0.0.1", sizeof(session->server_ip) - 1);
                    session->server_ip[sizeof(session->server_ip) - 1] = '\0';
                }
            } else {
                strncpy(session->server_ip, "127.0.0.1", sizeof(session->server_ip) - 1);
                session->server_ip[sizeof(session->server_ip) - 1] = '\0';
            }
        }
        if (!inet_ntop(AF_INET, &client_addr.sin_addr, session->client_ip, sizeof(session->client_ip))) {
            strncpy(session->client_ip, "unknown", sizeof(session->client_ip) - 1);
            session->client_ip[sizeof(session->client_ip) - 1] = '\0';
        }
        session->client_port = ntohs(client_addr.sin_port);
        server_log_info("Accepted connection from %s:%d", session->client_ip, session->client_port);
        
        pthread_t thread;
        pthread_create(&thread, NULL, handle_client, session);
        pthread_detach(thread);
    } else if (errno != EINTR) {
        server_log_error("Failed to accept client connection: %s", strerror(errno));
    }
    
    return client_fd;
}