#ifndef FTP_COMMON_H
#define FTP_COMMON_H

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <pthread.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <dirent.h>
#include <sys/stat.h>

#define FTP_MAX_LINE 256
#define FTP_MAX_PATH 512
#define FTP_BUFFER_SIZE 4096

// Mã phản hồi FTP (Thêm các mã còn thiếu)
#define FTP_READY 220
#define FTP_GOODBYE 221
#define FTP_DATA_CONN_OPEN 150
#define FTP_SUCCESS 226
#define FTP_PASV_MODE 227
#define FTP_LOGIN_SUCCESS 230
#define FTP_FILE_ACTION_OK 250
#define FTP_PATHNAME_CREATED 257
#define FTP_NEED_PASSWORD 331
#define FTP_LOGIN_FAILED 530
#define FTP_FILE_NOT_FOUND 550
#define FTP_ACTION_FAILED 550 // Mã lỗi chung
#define FTP_FILE_ACTION_FAILED 553 // Một mã lỗi khác (nhưng ta sẽ dùng 550)


int send_ftp_response(int sockfd, int code, const char *message);
int read_ftp_command(int sockfd, char *buffer, size_t size);
int parse_pasv_response(const char *response, char *ip, int *port);
int create_data_connection(const char *ip, int port);

// *** KHAI BÁO ĐÃ SỬA ***
// Các hàm này giờ nhận FILE* (đã sửa lỗi cảnh báo)
int send_file_over_socket(int sockfd, FILE *file);
int receive_file_over_socket(int sockfd, FILE *file);

void get_local_ip(char *ip_buffer, size_t size);

#endif // FTP_COMMON_H