#include "ftp_common.h"

int send_ftp_response(int sockfd, int code, const char *message) {
    char response[FTP_MAX_LINE];
    snprintf(response, sizeof(response), "%d %s\r\n", code, message);
    return send(sockfd, response, strlen(response), 0);
}

int read_ftp_command(int sockfd, char *buffer, size_t size) {
    ssize_t n = recv(sockfd, buffer, size - 1, 0);
    if (n <= 0) return -1;
    
    buffer[n] = '\0';
    
    // Remove trailing \r\n
    if (n >= 2 && buffer[n-2] == '\r' && buffer[n-1] == '\n') {
        buffer[n-2] = '\0';
    } else if (n >= 1 && buffer[n-1] == '\n') {
        buffer[n-1] = '\0';
    }
    
    return n;
}

int parse_pasv_response(const char *response, char *ip, int *port) {
    // Handles both "227 Entering Passive Mode (...)" and "Entering Passive Mode (...)"
    if (!response || !ip || !port) {
        return -1;
    }

    const char *paren = strchr(response, '(');
    if (!paren) {
        return -1;
    }

    int h1, h2, h3, h4, p1, p2;
    if (sscanf(paren, "(%d,%d,%d,%d,%d,%d)", &h1, &h2, &h3, &h4, &p1, &p2) != 6) {
        return -1;
    }

    snprintf(ip, 16, "%d.%d.%d.%d", h1, h2, h3, h4);
    *port = p1 * 256 + p2;
    return 0;
}

int create_data_connection(const char *ip, int port) {
    int sockfd = socket(AF_INET, SOCK_STREAM, 0);
    if (sockfd < 0) return -1;
    
    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    if (inet_pton(AF_INET, ip, &addr.sin_addr) <= 0) {
        close(sockfd);
        return -1;
    }
    
    if (connect(sockfd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        close(sockfd);
        return -1;
    }
    
    return sockfd;
}

// *** ĐÃ THAY ĐỔI ***
// Nhận FILE* thay vì const char*
// Không còn fopen/fclose bên trong
int send_file_over_socket(int sockfd, FILE *file) {
    char buffer[FTP_BUFFER_SIZE];
    size_t n;
    
    while ((n = fread(buffer, 1, sizeof(buffer), file)) > 0) {
        if (send(sockfd, buffer, n, 0) != (ssize_t)n) {
            // Lỗi send, file sẽ được đóng ở hàm gọi
            return -1;
        }
    }
    
    // Đã đọc xong, file sẽ được đóng ở hàm gọi
    return 0;
}

// *** ĐÃ THAY ĐỔI ***
// Nhận FILE* thay vì const char*
// Không còn fopen/fclose bên trong
int receive_file_over_socket(int sockfd, FILE *file) {
    char buffer[FTP_BUFFER_SIZE];
    ssize_t n;
    
    while ((n = recv(sockfd, buffer, sizeof(buffer), 0)) > 0) {
        if (fwrite(buffer, 1, n, file) != (size_t)n) {
            // Lỗi write, file sẽ được đóng ở hàm gọi
            return -1;
        }
    }
    
    // n == 0 (EOF) hoặc n < 0 (lỗi)
    // file sẽ được đóng ở hàm gọi
    return (n == 0) ? 0 : -1;
}

void get_local_ip(char *ip_buffer, size_t size) {
    int sockfd = socket(AF_INET, SOCK_STREAM, 0);
    if (sockfd < 0) {
        strncpy(ip_buffer, "127.0.0.1", size);
        return;
    }
    
    struct sockaddr_in addr;
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = inet_addr("8.8.8.8");
    addr.sin_port = htons(80);
    
    if (connect(sockfd, (struct sockaddr *)&addr, sizeof(addr)) == 0) {
        struct sockaddr_in local_addr;
        socklen_t len = sizeof(local_addr);
        if (getsockname(sockfd, (struct sockaddr *)&local_addr, &len) == 0) {
            inet_ntop(AF_INET, &local_addr.sin_addr, ip_buffer, size);
        } else {
            strncpy(ip_buffer, "127.0.0.1", size);
        }
    } else {
        strncpy(ip_buffer, "127.0.0.1", size);
    }
    
    close(sockfd);
}