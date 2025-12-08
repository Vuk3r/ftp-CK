#include "ftp_client.h"
#include <ctype.h>
#include <errno.h>
#include <stdarg.h>
#include <unistd.h>
#include <sys/select.h>

static void client_log_error(const char *fmt, ...) {
    va_list args;
    va_start(args, fmt);
    fprintf(stderr, "[CLIENT ERROR] ");
    vfprintf(stderr, fmt, args);
    fprintf(stderr, "\n");
    va_end(args);
}

static void client_log_info(const char *fmt, ...) {
    va_list args;
    va_start(args, fmt);
    fprintf(stdout, "[CLIENT] ");
    vfprintf(stdout, fmt, args);
    fprintf(stdout, "\n");
    va_end(args);
}

static ssize_t recv_line(int sockfd, char *buffer, size_t size) {
    if (size == 0) {
        errno = EINVAL;
        return -1;
    }

    size_t count = 0;
    while (count < size - 1) {
        char c;
        ssize_t n = recv(sockfd, &c, 1, 0);
        if (n < 0) {
            if (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR) {
                continue;
            }
            return -1;
        }
        if (n == 0) {
            return 0;
        }
        buffer[count++] = c;
        if (c == '\n') {
            break;
        }
    }
    buffer[count] = '\0';
    return (ssize_t)count;
}

static void trim_crlf(char *line) {
    size_t len = strlen(line);
    while (len > 0 && (line[len - 1] == '\n' || line[len - 1] == '\r')) {
        line[len - 1] = '\0';
        len--;
    }
}

static int check_connection_lost(ftp_client_t *client) {
    if (!client || !client->connected || client->control_fd < 0) {
        return 1;
    }
    int error = 0;
    socklen_t len = sizeof(error);
    if (getsockopt(client->control_fd, SOL_SOCKET, SO_ERROR, &error, &len) < 0) {
        return 1;
    }
    if (error != 0) {
        return 1;
    }
    return 0;
}

static int read_response(ftp_client_t *client, int *code, char *message, size_t message_size) {
    if (client->connected && check_connection_lost(client)) {
        client_log_error("Connection lost");
        client->connected = 0;
        return -1;
    }
    
    char line[FTP_MAX_LINE];
    int primary_code = 0;
    int expecting_multi = 0;

    while (1) {
        ssize_t n = recv_line(client->control_fd, line, sizeof(line));
        if (n < 0) {
            if (errno == ECONNRESET || errno == EPIPE || errno == ETIMEDOUT) {
                client_log_error("Connection lost: %s", strerror(errno));
                client->connected = 0;
            } else {
                client_log_error("Failed to receive response from server: %s", strerror(errno));
            }
            return -1;
        }
        if (n == 0) {
            client_log_error("Connection closed by server");
            client->connected = 0;
            return -1;
        }

        trim_crlf(line);
        if (strlen(line) < 3 ||
            !isdigit((unsigned char)line[0]) ||
            !isdigit((unsigned char)line[1]) ||
            !isdigit((unsigned char)line[2])) {
            client_log_error("Malformed response line: %s", line);
            return -1;
        }

        int current_code = (line[0] - '0') * 100 + (line[1] - '0') * 10 + (line[2] - '0');

        if (primary_code == 0) {
            primary_code = current_code;
            expecting_multi = (strlen(line) > 3 && line[3] == '-');
            if (message && message_size > 0) {
                const char *msg_start = line + 3;
                if (*msg_start == ' ' || *msg_start == '-') {
                    msg_start++;
                }
                snprintf(message, message_size, "%s", msg_start);
            }
            if (!expecting_multi) {
                if (code) {
                    *code = primary_code;
                }
                return 0;
            }
            continue;
        }

        if (current_code != primary_code) {
            continue;
        }

        if (strlen(line) > 3 && line[3] == ' ') {
            if (message && message_size > 0) {
                const char *msg_start = line + 4;
                snprintf(message, message_size, "%s", msg_start);
            }
            if (code) {
                *code = primary_code;
            }
            return 0;
        }
        // Otherwise, still inside multi-line response, continue loop.
    }
}

static int send_command(ftp_client_t *client, const char *fmt, ...) {
    char buffer[FTP_MAX_LINE];
    va_list args;
    va_start(args, fmt);
    vsnprintf(buffer, sizeof(buffer), fmt, args);
    va_end(args);

    size_t len = strlen(buffer);
    if (len > sizeof(buffer) - 3) {
        len = sizeof(buffer) - 3;
        buffer[len] = '\0';
    }
    buffer[len++] = '\r';
    buffer[len++] = '\n';
    buffer[len] = '\0';

    if (check_connection_lost(client)) {
        client_log_error("Connection lost before sending command");
        client->connected = 0;
        return -1;
    }
    
    ssize_t sent = send(client->control_fd, buffer, len, 0);
    if (sent < 0) {
        if (errno == ECONNRESET || errno == EPIPE || errno == ETIMEDOUT) {
            client_log_error("Connection lost while sending command");
            client->connected = 0;
        } else {
            client_log_error("Failed to send command '%.*s': %s", 20, buffer, strerror(errno));
        }
        return -1;
    }
    if ((size_t)sent != strlen(buffer)) {
        client_log_error("Partial send for command '%.*s'", 20, buffer);
        return -1;
    }
    return 0;
}

static int ensure_connected(ftp_client_t *client) {
    if (!client || !client->connected) {
        client_log_error("Client is not connected");
        return -1;
    }
    return 0;
}

int ftp_connect(ftp_client_t *client, const char *ip, int port) {
    if (!client || !ip) {
        errno = EINVAL;
        client_log_error("Invalid parameters to ftp_connect");
        return -1;
    }

    memset(client, 0, sizeof(*client));
    client->control_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (client->control_fd < 0) {
        client_log_error("Failed to create socket: %s", strerror(errno));
        return -1;
    }

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    if (inet_pton(AF_INET, ip, &addr.sin_addr) <= 0) {
        client_log_error("Invalid server IP address: %s", ip);
        close(client->control_fd);
        client->control_fd = -1;
        return -1;
    }

    if (connect(client->control_fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        client_log_error("Failed to connect to %s:%d: %s", ip, port, strerror(errno));
        close(client->control_fd);
        client->control_fd = -1;
        return -1;
    }

    struct timeval timeout;
    fd_set read_fds;
    FD_ZERO(&read_fds);
    FD_SET(client->control_fd, &read_fds);
    timeout.tv_sec = 2;
    timeout.tv_usec = 0;
    select(client->control_fd + 1, &read_fds, NULL, NULL, &timeout);
    
    int code = 0;
    if (read_response(client, &code, NULL, 0) < 0) {
        if (code == 0) {
            client_log_error("Server did not respond with READY (connection may be closed)");
        } else {
            client_log_error("Server did not respond with READY (expected %d, got %d)", FTP_READY, code);
        }
        close(client->control_fd);
        client->control_fd = -1;
        return -1;
    }
    if (code != FTP_READY) {
        client_log_error("Server did not respond with READY (expected %d, got %d)", FTP_READY, code);
        close(client->control_fd);
        client->control_fd = -1;
        return -1;
    }

    client->connected = 1;
    strncpy(client->server_ip, ip, sizeof(client->server_ip) - 1);
    client->server_port = port;
    client_log_info("Connected to %s:%d", ip, port);
    return 0;
}

int ftp_login(ftp_client_t *client, const char *username, const char *password) {
    if (ensure_connected(client) < 0) {
        return -1;
    }

    if (send_command(client, "USER %s", username ? username : "") < 0) {
        return -1;
    }

    int code = 0;
    if (read_response(client, &code, NULL, 0) < 0) {
        return -1;
    }

    if (code != FTP_NEED_PASSWORD && code != FTP_LOGIN_SUCCESS) {
        client_log_error("Unexpected response to USER: %d", code);
        return -1;
    }

    if (code == FTP_LOGIN_SUCCESS) {
        return 0;
    }

    if (send_command(client, "PASS %s", password ? password : "") < 0) {
        return -1;
    }

    if (read_response(client, &code, NULL, 0) < 0) {
        return -1;
    }

    if (code != FTP_LOGIN_SUCCESS) {
        client_log_error("Login failed with response code %d", code);
        return -1;
    }

    return 0;
}

static int enter_passive_mode(ftp_client_t *client, char *ip_buffer, size_t ip_size, int *port) {
    if (!ip_buffer || ip_size < 16 || !port) {
        client_log_error("Invalid arguments supplied to enter_passive_mode");
        return -1;
    }
    if (send_command(client, "PASV") < 0) {
        return -1;
    }

    char response[FTP_MAX_LINE];
    int code = 0;
    if (read_response(client, &code, response, sizeof(response)) < 0) {
        return -1;
    }

    int stray_attempts = 0;
    while (code == FTP_SUCCESS && stray_attempts < 4) {
        // Handle queued completion reply from a previous data transfer.
        if (read_response(client, &code, response, sizeof(response)) < 0) {
            return -1;
        }
        stray_attempts++;
    }

    if (code != FTP_PASV_MODE) {
        client_log_error("PASV command failed with response code %d (%s)", code, response);
        return -1;
    }

    if (parse_pasv_response(response, ip_buffer, port) < 0) {
        client_log_error("Failed to parse PASV response: %s", response);
        return -1;
    }
    return 0;
}

int ftp_list(ftp_client_t *client, char *buffer, size_t size) {
    if (ensure_connected(client) < 0) {
        return -1;
    }
    if (!buffer || size == 0) {
        client_log_error("Invalid buffer provided to ftp_list");
        return -1;
    }

    char data_ip[16];
    int data_port = 0;

    if (enter_passive_mode(client, data_ip, sizeof(data_ip), &data_port) < 0) {
        return -1;
    }

    int data_fd = create_data_connection(data_ip, data_port);
    if (data_fd < 0) {
        client_log_error("Failed to establish data connection to %s:%d: %s", data_ip, data_port, strerror(errno));
        return -1;
    }

    if (send_command(client, "LIST") < 0) {
        close(data_fd);
        return -1;
    }

    int code = 0;
    if (read_response(client, &code, NULL, 0) < 0) {
        close(data_fd);
        return -1;
    }
    if (code != FTP_DATA_CONN_OPEN) {
        client_log_error("LIST command rejected with code %d", code);
        close(data_fd);
        return -1;
    }

    size_t total = 0;
    ssize_t n;
    char temp[FTP_BUFFER_SIZE];
    while ((n = recv(data_fd, temp, sizeof(temp), 0)) > 0) {
        size_t copy = (total + n < size - 1) ? (size_t)n : (size - 1 - total);
        if (copy > 0) {
            memcpy(buffer + total, temp, copy);
            total += copy;
        }
        if ((size_t)n > copy) {
            client_log_error("LIST response truncated (buffer too small)");
        }
    }
    close(data_fd);

    if (n < 0) {
        client_log_error("Error receiving LIST data: %s", strerror(errno));
        return -1;
    }

    buffer[total] = '\0';

    if (read_response(client, &code, NULL, 0) < 0) {
        return -1;
    }
    if (code != FTP_SUCCESS) {
        client_log_error("LIST completion failed with code %d", code);
        return -1;
    }

    return 0;
}

int ftp_retr(ftp_client_t *client, const char *remote_file, const char *local_file) {
    if (ensure_connected(client) < 0) {
        return -1;
    }
    if (!remote_file || !local_file) {
        client_log_error("Invalid parameters to ftp_retr");
        return -1;
    }

    char data_ip[16];
    int data_port = 0;
    if (enter_passive_mode(client, data_ip, sizeof(data_ip), &data_port) < 0) {
        return -1;
    }

    int data_fd = create_data_connection(data_ip, data_port);
    if (data_fd < 0) {
        client_log_error("Failed to establish data connection for RETR: %s", strerror(errno));
        return -1;
    }

    if (send_command(client, "RETR %s", remote_file) < 0) {
        close(data_fd);
        return -1;
    }

    int code = 0;
    if (read_response(client, &code, NULL, 0) < 0) {
        close(data_fd);
        return -1;
    }
    if (code != FTP_DATA_CONN_OPEN) {
        client_log_error("RETR command rejected with code %d", code);
        close(data_fd);
        return -1;
    }

    FILE *file = fopen(local_file, "wb");
    if (!file) {
        client_log_error("Failed to open local file '%s' for writing: %s", local_file, strerror(errno));
        close(data_fd);
        return -1;
    }

    int transfer_status = receive_file_over_socket(data_fd, file);

    fclose(file);
    close(data_fd);

    if (read_response(client, &code, NULL, 0) < 0) {
        return -1;
    }

    if (transfer_status < 0) {
        client_log_error("Server reported code %d after RETR failure", code);
        return -1;
    }

    if (code != FTP_SUCCESS) {
        client_log_error("RETR completion failed with code %d", code);
        return -1;
    }

    client_log_info("Downloaded '%s' to '%s'", remote_file, local_file);
    return 0;
}

int ftp_stor(ftp_client_t *client, const char *local_file, const char *remote_file) {
    if (ensure_connected(client) < 0) {
        return -1;
    }
    if (!local_file || !remote_file) {
        client_log_error("Invalid parameters to ftp_stor");
        return -1;
    }

    char data_ip[16];
    int data_port = 0;
    if (enter_passive_mode(client, data_ip, sizeof(data_ip), &data_port) < 0) {
        return -1;
    }

    int data_fd = create_data_connection(data_ip, data_port);
    if (data_fd < 0) {
        client_log_error("Failed to establish data connection for STOR: %s", strerror(errno));
        return -1;
    }

    if (send_command(client, "STOR %s", remote_file) < 0) {
        close(data_fd);
        return -1;
    }

    int code = 0;
    if (read_response(client, &code, NULL, 0) < 0) {
        close(data_fd);
        return -1;
    }
    if (code != FTP_DATA_CONN_OPEN) {
        client_log_error("STOR command rejected with code %d", code);
        close(data_fd);
        return -1;
    }

    FILE *file = fopen(local_file, "rb");
    if (!file) {
        client_log_error("Failed to open local file '%s' for reading: %s", local_file, strerror(errno));
        close(data_fd);
        return -1;
    }

    int transfer_status = send_file_over_socket(data_fd, file);

    fclose(file);
    shutdown(data_fd, SHUT_WR);
    close(data_fd);

    if (read_response(client, &code, NULL, 0) < 0) {
        return -1;
    }

    if (transfer_status < 0) {
        client_log_error("Server reported code %d after STOR failure", code);
        return -1;
    }

    if (code != FTP_SUCCESS) {
        client_log_error("STOR completion failed with code %d", code);
        return -1;
    }

    client_log_info("Uploaded '%s' to '%s'", local_file, remote_file);
    return 0;
}

int ftp_cwd(ftp_client_t *client, const char *path) {
    if (ensure_connected(client) < 0) {
        return -1;
    }
    if (!path) {
        client_log_error("Invalid path parameter to ftp_cwd");
        return -1;
    }

    if (send_command(client, "CWD %s", path) < 0) {
        return -1;
    }

    int code = 0;
    if (read_response(client, &code, NULL, 0) < 0) {
        return -1;
    }
    if (code != FTP_FILE_ACTION_OK) {
        client_log_error("CWD failed with code %d", code);
        return -1;
    }

    return 0;
}

int ftp_pwd(ftp_client_t *client, char *path, size_t size) {
    if (ensure_connected(client) < 0) {
        return -1;
    }
    if (!path || size == 0) {
        client_log_error("Invalid buffer provided to ftp_pwd");
        return -1;
    }

    if (send_command(client, "PWD") < 0) {
        return -1;
    }

    int code = 0;
    char response[FTP_MAX_LINE];
    if (read_response(client, &code, response, sizeof(response)) < 0) {
        return -1;
    }

    if (code != FTP_PATHNAME_CREATED) {
        client_log_error("PWD failed with code %d", code);
        return -1;
    }

    snprintf(path, size, "%s", response);
    return 0;
}

int ftp_dele(ftp_client_t *client, const char *remote_file) {
    if (ensure_connected(client) < 0) {
        return -1;
    }
    if (!remote_file) {
        client_log_error("Invalid parameters to ftp_dele");
        return -1;
    }

    if (send_command(client, "DELE %s", remote_file) < 0) {
        return -1;
    }

    int code = 0;
    if (read_response(client, &code, NULL, 0) < 0) {
        return -1;
    }
    if (code != FTP_FILE_ACTION_OK) {
        client_log_error("DELE failed with code %d", code);
        return -1;
    }
    client_log_info("Deleted '%s'", remote_file);
    return 0;
}

int ftp_rename(ftp_client_t *client, const char *from_path, const char *to_path) {
    if (ensure_connected(client) < 0) {
        return -1;
    }
    if (!from_path || !to_path) {
        client_log_error("Invalid parameters to ftp_rename");
        return -1;
    }

    int code = 0;
    if (send_command(client, "RNFR %s", from_path) < 0) {
        return -1;
    }
    if (read_response(client, &code, NULL, 0) < 0 || code != 350) {
        client_log_error("RNFR failed with code %d", code);
        return -1;
    }
    if (send_command(client, "RNTO %s", to_path) < 0) {
        return -1;
    }
    if (read_response(client, &code, NULL, 0) < 0) {
        return -1;
    }
    if (code != FTP_FILE_ACTION_OK) {
        client_log_error("RNTO failed with code %d", code);
        return -1;
    }
    client_log_info("Renamed '%s' -> '%s'", from_path, to_path);
    return 0;
}

int ftp_disconnect(ftp_client_t *client) {
    if (!client || !client->connected) {
        return 0;
    }

    send_command(client, "QUIT");
    int code = 0;
    if (read_response(client, &code, NULL, 0) == 0 && code != FTP_GOODBYE) {
        client_log_error("QUIT returned code %d", code);
    }

    close(client->control_fd);
    client->control_fd = -1;
    client->connected = 0;
    client_log_info("Disconnected from %s:%d", client->server_ip, client->server_port);
    return 0;
}

