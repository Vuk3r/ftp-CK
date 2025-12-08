#ifndef FTP_CLIENT_H
#define FTP_CLIENT_H

#include "ftp_common.h" // Cần file header từ bước trước

typedef struct {
    int control_fd;
    char server_ip[16];
    int server_port;
    int connected;
} ftp_client_t;

int ftp_connect(ftp_client_t *client, const char *ip, int port);
int ftp_login(ftp_client_t *client, const char *username, const char *password);
int ftp_list(ftp_client_t *client, char *buffer, size_t size);
int ftp_retr(ftp_client_t *client, const char *remote_file, const char *local_file);
int ftp_stor(ftp_client_t *client, const char *local_file, const char *remote_file);
int ftp_cwd(ftp_client_t *client, const char *path);
int ftp_pwd(ftp_client_t *client, char *path, size_t size);
int ftp_dele(ftp_client_t *client, const char *remote_file);
int ftp_rename(ftp_client_t *client, const char *from_path, const char *to_path);
int ftp_disconnect(ftp_client_t *client);

#endif // FTP_CLIENT_H