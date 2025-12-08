#include <gtk/gtk.h>
#include <limits.h>
#include "ftp_client.h"

static GtkWidget *server_ip_entry;
static GtkWidget *server_port_entry;
static GtkWidget *username_entry;
static GtkWidget *password_entry;
static GtkWidget *connect_button;
static GtkWidget *disconnect_button;
static GtkWidget *file_list_box;
static GtkWidget *remote_file_entry;
static GtkWidget *local_file_entry;
static GtkWidget *upload_button;
static GtkWidget *download_button;
static GtkWidget *refresh_button;
static GtkWidget *local_file_browse_button;
static GtkWidget *save_path_button;
static GtkWidget *select_remote_button;
static GtkWidget *working_dir_entry;
static GtkWidget *working_dir_button;
static GtkWidget *remote_dir_entry;
static GtkWidget *change_dir_button;
static GtkWidget *delete_button;
static GtkWidget *move_target_entry;
static GtkWidget *move_button;
static GtkWidget *status_label;

static ftp_client_t client;
static gboolean connected = FALSE;

// Forward declarations
static void on_refresh_clicked(GtkWidget *widget, gpointer data);

static void clear_file_list(void) {
    if (!file_list_box) return;
    GList *children = gtk_container_get_children(GTK_CONTAINER(file_list_box));
    for (GList *iter = children; iter != NULL; iter = iter->next) {
        gtk_widget_destroy(GTK_WIDGET(iter->data));
    }
    g_list_free(children);
}

static char *extract_filename(const char *line) {
    if (!line) return NULL;
    const char *last_space = strrchr(line, ' ');
    const char *candidate = last_space ? last_space + 1 : line;
    if (*candidate == '\0') return NULL;
    return g_strdup(candidate);
}

static void add_remote_row(const char *line) {
    if (!line || !file_list_box) return;
    GtkWidget *row = gtk_list_box_row_new();
    GtkWidget *label = gtk_label_new(line);
    gtk_label_set_xalign(GTK_LABEL(label), 0.0f);
    gtk_container_add(GTK_CONTAINER(row), label);
    char *name = extract_filename(line);
    if (name) {
        g_object_set_data_full(G_OBJECT(row), "remote_name", name, g_free);
    }
    gboolean is_directory = (line[0] == 'd');
    g_object_set_data(G_OBJECT(row), "is_directory", GINT_TO_POINTER(is_directory ? 1 : 0));
    gtk_container_add(GTK_CONTAINER(file_list_box), row);
    gtk_widget_show_all(row);
}

static void set_connection_state(gboolean is_connected) {
    connected = is_connected;
    gtk_widget_set_sensitive(connect_button, !is_connected);
    gtk_widget_set_sensitive(disconnect_button, is_connected);
    gtk_widget_set_sensitive(server_ip_entry, !is_connected);
    gtk_widget_set_sensitive(server_port_entry, !is_connected);
    gtk_widget_set_sensitive(username_entry, !is_connected);
    gtk_widget_set_sensitive(password_entry, !is_connected);
    gtk_widget_set_sensitive(refresh_button, is_connected);
    gtk_widget_set_sensitive(upload_button, is_connected);
    gtk_widget_set_sensitive(download_button, is_connected);
    gtk_widget_set_sensitive(local_file_browse_button, is_connected);
    gtk_widget_set_sensitive(select_remote_button, is_connected);
    gtk_widget_set_sensitive(save_path_button, is_connected);
    gtk_widget_set_sensitive(change_dir_button, is_connected);
    gtk_widget_set_sensitive(delete_button, is_connected);
    gtk_widget_set_sensitive(move_button, is_connected);
    gtk_widget_set_sensitive(move_target_entry, is_connected);
    gtk_widget_set_sensitive(remote_dir_entry, is_connected);
    if (!is_connected) {
        clear_file_list();
    }
}

static void ensure_local_path_default(const char *remote_file) {
    const char *local_text = gtk_entry_get_text(GTK_ENTRY(local_file_entry));
    if (local_text && strlen(local_text) > 0) return;
    if (!remote_file || strlen(remote_file) == 0) return;
    const char *base_dir = gtk_entry_get_text(GTK_ENTRY(working_dir_entry));
    char cwd_buffer[PATH_MAX];
    if (!base_dir || strlen(base_dir) == 0) {
        if (!getcwd(cwd_buffer, sizeof(cwd_buffer))) return;
        base_dir = cwd_buffer;
    }
    char path_buffer[PATH_MAX];
    snprintf(path_buffer, sizeof(path_buffer), "%s/%s", base_dir, remote_file);
    gtk_entry_set_text(GTK_ENTRY(local_file_entry), path_buffer);
}

static void fill_local_from_workdir(const char *remote_file) {
    if (!remote_file || strlen(remote_file) == 0) return;
    const char *base_dir = gtk_entry_get_text(GTK_ENTRY(working_dir_entry));
    char cwd_buffer[PATH_MAX];
    if (!base_dir || strlen(base_dir) == 0) {
        if (!getcwd(cwd_buffer, sizeof(cwd_buffer))) return;
        base_dir = cwd_buffer;
    }
    char path_buffer[PATH_MAX];
    snprintf(path_buffer, sizeof(path_buffer), "%s/%s", base_dir, remote_file);
    gtk_entry_set_text(GTK_ENTRY(local_file_entry), path_buffer);
}

static void update_status(const char *message) {
    gtk_label_set_text(GTK_LABEL(status_label), message);
}

static void on_connect_clicked(GtkWidget *widget, gpointer data) {
    (void)widget; (void)data; // Suppress unused parameter warnings
    if (connected) return;
    
    const char *ip = gtk_entry_get_text(GTK_ENTRY(server_ip_entry));
    const char *port_str = gtk_entry_get_text(GTK_ENTRY(server_port_entry));
    const char *username = gtk_entry_get_text(GTK_ENTRY(username_entry));
    const char *password = gtk_entry_get_text(GTK_ENTRY(password_entry));
    
    int port = atoi(port_str);
    if (port <= 0 || port > 65535) {
        update_status("Invalid port number");
        return;
    }
    
    if (strlen(ip) == 0) {
        update_status("Please enter server IP");
        return;
    }

    if (ftp_connect(&client, ip, port) < 0) {
        update_status("Connection failed");
        return;
    }
    
    if (ftp_login(&client, username, password) < 0) {
        update_status("Login failed");
        ftp_disconnect(&client);
        return;
    }
    
    set_connection_state(TRUE);

    char path[FTP_BUFFER_SIZE];
    if (ftp_pwd(&client, path, sizeof(path)) == 0) {
        char status_msg[FTP_BUFFER_SIZE];
        snprintf(status_msg, sizeof(status_msg), "Connected to %s:%d • %s", ip, port, path);
        update_status(status_msg);
    } else {
        update_status("Connected");
    }

    on_refresh_clicked(NULL, NULL);
}

static void on_disconnect_clicked(GtkWidget *widget, gpointer data) {
    (void)widget; (void)data; // Suppress unused parameter warnings
    if (!connected) return;
    
    ftp_disconnect(&client);
    set_connection_state(FALSE);
    update_status("Disconnected");
}

static void handle_connection_error(void) {
    if (connected) {
        ftp_disconnect(&client);
        set_connection_state(FALSE);
        update_status("Connection lost - Disconnected");
    }
}

static void on_refresh_clicked(GtkWidget *widget, gpointer data) {
    (void)widget; (void)data;
    if (!connected) return;
    
    char buffer[FTP_BUFFER_SIZE];
    if (ftp_list(&client, buffer, sizeof(buffer)) == 0) {
        clear_file_list();
        char *cursor = buffer;
        while (cursor && *cursor) {
            char *line_end = strchr(cursor, '\n');
            if (line_end) {
                *line_end = '\0';
            }
            g_strstrip(cursor);
            if (strlen(cursor) > 0) {
                add_remote_row(cursor);
            }
            if (!line_end) break;
            cursor = line_end + 1;
        }
        update_status("File list refreshed");
    } else {
        if (!client.connected) {
            handle_connection_error();
        } else {
            update_status("Failed to refresh file list");
        }
    }
}

static void on_upload_clicked(GtkWidget *widget, gpointer data) {
    (void)widget; (void)data;
    if (!connected) return;
    
    const char *local_file = gtk_entry_get_text(GTK_ENTRY(local_file_entry));
    const char *remote_file = gtk_entry_get_text(GTK_ENTRY(remote_file_entry));
    
    if (strlen(local_file) == 0 || strlen(remote_file) == 0) {
        update_status("Please specify both local and remote file names");
        return;
    }
    
    if (ftp_stor(&client, local_file, remote_file) == 0) {
        update_status("File uploaded successfully");
        on_refresh_clicked(NULL, NULL);
    } else {
        if (!client.connected) {
            handle_connection_error();
        } else {
            update_status("Upload failed");
        }
    }
}

static void on_download_clicked(GtkWidget *widget, gpointer data) {
    (void)widget; (void)data;
    if (!connected) return;
    
    const char *remote_file = gtk_entry_get_text(GTK_ENTRY(remote_file_entry));
    const char *local_file = gtk_entry_get_text(GTK_ENTRY(local_file_entry));

    ensure_local_path_default(remote_file);
    local_file = gtk_entry_get_text(GTK_ENTRY(local_file_entry));
    
    if (strlen(remote_file) == 0 || strlen(local_file) == 0) {
        update_status("Please specify both remote and local file names");
        return;
    }
    
    if (ftp_retr(&client, remote_file, local_file) == 0) {
        update_status("File downloaded successfully");
    } else {
        if (!client.connected) {
            handle_connection_error();
        } else {
            update_status("Download failed");
        }
    }
}

static void on_select_local_file_clicked(GtkWidget *widget, gpointer data) {
    (void)data;
    GtkWidget *dialog = gtk_file_chooser_dialog_new(
        "Select Local File",
        GTK_WINDOW(gtk_widget_get_toplevel(widget)),
        GTK_FILE_CHOOSER_ACTION_OPEN,
        "_Cancel", GTK_RESPONSE_CANCEL,
        "_Open", GTK_RESPONSE_ACCEPT,
        NULL);

    if (gtk_dialog_run(GTK_DIALOG(dialog)) == GTK_RESPONSE_ACCEPT) {
        char *filename = gtk_file_chooser_get_filename(GTK_FILE_CHOOSER(dialog));
        if (filename) {
            gtk_entry_set_text(GTK_ENTRY(local_file_entry), filename);
            g_free(filename);
        }
    }
    gtk_widget_destroy(dialog);
}

static void on_select_save_path_clicked(GtkWidget *widget, gpointer data) {
    (void)data;
    GtkWidget *dialog = gtk_file_chooser_dialog_new(
        "Select Download Path",
        GTK_WINDOW(gtk_widget_get_toplevel(widget)),
        GTK_FILE_CHOOSER_ACTION_SAVE,
        "_Cancel", GTK_RESPONSE_CANCEL,
        "_Save", GTK_RESPONSE_ACCEPT,
        NULL);

    if (gtk_dialog_run(GTK_DIALOG(dialog)) == GTK_RESPONSE_ACCEPT) {
        char *filename = gtk_file_chooser_get_filename(GTK_FILE_CHOOSER(dialog));
        if (filename) {
            gtk_entry_set_text(GTK_ENTRY(local_file_entry), filename);
            g_free(filename);
        }
    }
    gtk_widget_destroy(dialog);
}

static void on_select_working_dir_clicked(GtkWidget *widget, gpointer data) {
    (void)data;
    GtkWidget *dialog = gtk_file_chooser_dialog_new(
        "Select Local Working Folder",
        GTK_WINDOW(gtk_widget_get_toplevel(widget)),
        GTK_FILE_CHOOSER_ACTION_SELECT_FOLDER,
        "_Cancel", GTK_RESPONSE_CANCEL,
        "_Open", GTK_RESPONSE_ACCEPT,
        NULL);

    if (gtk_dialog_run(GTK_DIALOG(dialog)) == GTK_RESPONSE_ACCEPT) {
        char *folder = gtk_file_chooser_get_filename(GTK_FILE_CHOOSER(dialog));
        if (folder) {
            gtk_entry_set_text(GTK_ENTRY(working_dir_entry), folder);
            g_free(folder);
        }
    }
    gtk_widget_destroy(dialog);
}

static const char *get_selected_remote(void) {
    GtkListBoxRow *row = gtk_list_box_get_selected_row(GTK_LIST_BOX(file_list_box));
    if (!row) return NULL;
    return g_object_get_data(G_OBJECT(row), "remote_name");
}

static void on_use_remote_selection_clicked(GtkWidget *widget, gpointer data) {
    (void)widget; (void)data;
    GtkListBoxRow *row = gtk_list_box_get_selected_row(GTK_LIST_BOX(file_list_box));
    if (!row) return;
    gboolean is_directory = GPOINTER_TO_INT(g_object_get_data(G_OBJECT(row), "is_directory")) != 0;
    if (is_directory) {
        update_status("This is a directory. Please select a file.");
        return;
    }
    const char *name = get_selected_remote();
    if (name && strlen(name) > 0) {
        gtk_entry_set_text(GTK_ENTRY(remote_file_entry), name);
        fill_local_from_workdir(name);
    }
}

static void on_remote_row_activated(GtkListBox *box, GtkListBoxRow *row, gpointer data) {
    (void)box; (void)data;
    gboolean is_directory = GPOINTER_TO_INT(g_object_get_data(G_OBJECT(row), "is_directory")) != 0;
    if (is_directory) {
        update_status("This is a directory. Please select a file.");
        return;
    }
    const char *name = g_object_get_data(G_OBJECT(row), "remote_name");
    if (name && strlen(name) > 0) {
        gtk_entry_set_text(GTK_ENTRY(remote_file_entry), name);
        fill_local_from_workdir(name);
    }
}

static void on_change_dir_clicked(GtkWidget *widget, gpointer data) {
    (void)widget; (void)data;
    if (!connected) return;
    const char *path = gtk_entry_get_text(GTK_ENTRY(remote_dir_entry));
    if (!path || strlen(path) == 0) return;
    if (ftp_cwd(&client, path) == 0) {
        update_status("Changed remote directory");
        on_refresh_clicked(NULL, NULL);
    } else {
        if (!client.connected) {
            handle_connection_error();
        } else {
            update_status("Change directory failed");
        }
    }
}

static void on_delete_clicked(GtkWidget *widget, gpointer data) {
    (void)widget; (void)data;
    if (!connected) return;
    const char *name = get_selected_remote();
    if (!name || strlen(name) == 0) return;
    if (ftp_dele(&client, name) == 0) {
        update_status("File deleted");
        on_refresh_clicked(NULL, NULL);
    } else {
        if (!client.connected) {
            handle_connection_error();
        } else {
            update_status("Delete failed");
        }
    }
}

static void on_move_clicked(GtkWidget *widget, gpointer data) {
    (void)widget; (void)data;
    if (!connected) return;
    const char *name = get_selected_remote();
    const char *target = gtk_entry_get_text(GTK_ENTRY(move_target_entry));
    if (!name || !target || strlen(target) == 0) return;

    char dest[FTP_MAX_PATH];
    size_t len = strlen(target);
    if (target[len - 1] == '/' || target[len - 1] == '\\') {
        snprintf(dest, sizeof(dest), "%s%s", target, name);
    } else {
        snprintf(dest, sizeof(dest), "%s", target);
    }

    if (ftp_rename(&client, name, dest) == 0) {
        update_status("Moved");
        on_refresh_clicked(NULL, NULL);
    } else {
        if (!client.connected) {
            handle_connection_error();
        } else {
            update_status("Move failed");
        }
    }
}

static void on_destroy(GtkWidget *widget, gpointer data) {
    (void)widget; (void)data; // Suppress unused parameter warnings
    if (connected) {
        ftp_disconnect(&client);
    }
    gtk_main_quit();
}

int main(int argc, char *argv[]) {
    gtk_init(&argc, &argv);
    
    memset(&client, 0, sizeof(client));
    
    GtkWidget *window = gtk_window_new(GTK_WINDOW_TOPLEVEL);
    gtk_window_set_title(GTK_WINDOW(window), "FTP Client");
    gtk_window_set_default_size(GTK_WINDOW(window), 700, 600);
    g_signal_connect(window, "destroy", G_CALLBACK(on_destroy), NULL);
    
    GtkWidget *vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 5);
    gtk_container_add(GTK_CONTAINER(window), vbox);
    gtk_container_set_border_width(GTK_CONTAINER(vbox), 10);
    
    // Connection settings
    GtkWidget *conn_frame = gtk_frame_new("Connection Settings");
    GtkWidget *conn_vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 5);
    gtk_container_add(GTK_CONTAINER(conn_frame), conn_vbox);
    gtk_container_set_border_width(GTK_CONTAINER(conn_vbox), 5);
    
    GtkWidget *ip_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 5);
    GtkWidget *ip_label = gtk_label_new("Server IP:");
    gtk_widget_set_size_request(ip_label, 100, -1);
    server_ip_entry = gtk_entry_new();
    gtk_entry_set_text(GTK_ENTRY(server_ip_entry), "127.0.0.1");
    gtk_entry_set_placeholder_text(GTK_ENTRY(server_ip_entry), "Server IP");
    gtk_box_pack_start(GTK_BOX(ip_box), ip_label, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(ip_box), server_ip_entry, TRUE, TRUE, 0);
    gtk_box_pack_start(GTK_BOX(conn_vbox), ip_box, FALSE, FALSE, 0);
    
    GtkWidget *port_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 5);
    GtkWidget *port_label = gtk_label_new("Port:");
    gtk_widget_set_size_request(port_label, 100, -1);
    server_port_entry = gtk_entry_new();
    gtk_entry_set_text(GTK_ENTRY(server_port_entry), "9999");
    gtk_entry_set_placeholder_text(GTK_ENTRY(server_port_entry), "Port");
    gtk_box_pack_start(GTK_BOX(port_box), port_label, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(port_box), server_port_entry, TRUE, TRUE, 0);
    gtk_box_pack_start(GTK_BOX(conn_vbox), port_box, FALSE, FALSE, 0);
    
    GtkWidget *user_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 5);
    GtkWidget *user_label = gtk_label_new("Username:");
    gtk_widget_set_size_request(user_label, 100, -1);
    username_entry = gtk_entry_new();
    gtk_entry_set_text(GTK_ENTRY(username_entry), "user");
    gtk_entry_set_placeholder_text(GTK_ENTRY(username_entry), "Username");
    gtk_box_pack_start(GTK_BOX(user_box), user_label, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(user_box), username_entry, TRUE, TRUE, 0);
    gtk_box_pack_start(GTK_BOX(conn_vbox), user_box, FALSE, FALSE, 0);
    
    GtkWidget *pass_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 5);
    GtkWidget *pass_label = gtk_label_new("Password:");
    gtk_widget_set_size_request(pass_label, 100, -1);
    password_entry = gtk_entry_new();
    gtk_entry_set_visibility(GTK_ENTRY(password_entry), FALSE);
    gtk_entry_set_text(GTK_ENTRY(password_entry), "pass");
    gtk_entry_set_placeholder_text(GTK_ENTRY(password_entry), "Password");
    gtk_box_pack_start(GTK_BOX(pass_box), pass_label, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(pass_box), password_entry, TRUE, TRUE, 0);
    gtk_box_pack_start(GTK_BOX(conn_vbox), pass_box, FALSE, FALSE, 0);
    
    GtkWidget *conn_button_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 5);
    connect_button = gtk_button_new_with_label("Connect");
    disconnect_button = gtk_button_new_with_label("Disconnect");
    gtk_widget_set_sensitive(disconnect_button, FALSE);
    g_signal_connect(connect_button, "clicked", G_CALLBACK(on_connect_clicked), NULL);
    g_signal_connect(disconnect_button, "clicked", G_CALLBACK(on_disconnect_clicked), NULL);
    gtk_box_pack_start(GTK_BOX(conn_button_box), connect_button, TRUE, TRUE, 0);
    gtk_box_pack_start(GTK_BOX(conn_button_box), disconnect_button, TRUE, TRUE, 0);
    gtk_box_pack_start(GTK_BOX(conn_vbox), conn_button_box, FALSE, FALSE, 0);
    
    gtk_box_pack_start(GTK_BOX(vbox), conn_frame, FALSE, FALSE, 0);
    
    // Local working dir
    GtkWidget *work_frame = gtk_frame_new("Local Working Folder");
    GtkWidget *work_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 5);
    gtk_container_add(GTK_CONTAINER(work_frame), work_box);
    gtk_container_set_border_width(GTK_CONTAINER(work_box), 5);
    GtkWidget *work_label = gtk_label_new("Folder:");
    gtk_widget_set_size_request(work_label, 100, -1);
    working_dir_entry = gtk_entry_new();
    char work_cwd[PATH_MAX];
    if (getcwd(work_cwd, sizeof(work_cwd))) {
        gtk_entry_set_text(GTK_ENTRY(working_dir_entry), work_cwd);
    }
    gtk_entry_set_placeholder_text(GTK_ENTRY(working_dir_entry), "Local folder for downloads/uploads");
    working_dir_button = gtk_button_new_with_label("Browse");
    g_signal_connect(working_dir_button, "clicked", G_CALLBACK(on_select_working_dir_clicked), NULL);
    gtk_box_pack_start(GTK_BOX(work_box), work_label, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(work_box), working_dir_entry, TRUE, TRUE, 0);
    gtk_box_pack_start(GTK_BOX(work_box), working_dir_button, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(vbox), work_frame, FALSE, FALSE, 0);

    // File list
    GtkWidget *list_frame = gtk_frame_new("Remote Files");
    GtkWidget *list_vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 5);
    gtk_container_add(GTK_CONTAINER(list_frame), list_vbox);
    gtk_container_set_border_width(GTK_CONTAINER(list_vbox), 5);
    
    GtkWidget *list_button_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 5);
    refresh_button = gtk_button_new_with_label("Refresh");
    gtk_widget_set_sensitive(refresh_button, FALSE);
    g_signal_connect(refresh_button, "clicked", G_CALLBACK(on_refresh_clicked), NULL);
    select_remote_button = gtk_button_new_with_label("Use Selection");
    gtk_widget_set_sensitive(select_remote_button, FALSE);
    g_signal_connect(select_remote_button, "clicked", G_CALLBACK(on_use_remote_selection_clicked), NULL);
    delete_button = gtk_button_new_with_label("Delete Selected");
    gtk_widget_set_sensitive(delete_button, FALSE);
    g_signal_connect(delete_button, "clicked", G_CALLBACK(on_delete_clicked), NULL);
    gtk_box_pack_start(GTK_BOX(list_button_box), refresh_button, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(list_button_box), select_remote_button, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(list_button_box), delete_button, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(list_vbox), list_button_box, FALSE, FALSE, 0);
    
    GtkWidget *scrolled = gtk_scrolled_window_new(NULL, NULL);
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scrolled),
                                   GTK_POLICY_AUTOMATIC, GTK_POLICY_AUTOMATIC);
    file_list_box = gtk_list_box_new();
    gtk_list_box_set_selection_mode(GTK_LIST_BOX(file_list_box), GTK_SELECTION_SINGLE);
    g_signal_connect(file_list_box, "row-activated", G_CALLBACK(on_remote_row_activated), NULL);
    gtk_container_add(GTK_CONTAINER(scrolled), file_list_box);
    gtk_box_pack_start(GTK_BOX(list_vbox), scrolled, TRUE, TRUE, 0);
    
    gtk_box_pack_start(GTK_BOX(vbox), list_frame, TRUE, TRUE, 0);

    // Remote directory actions
    GtkWidget *remote_dir_frame = gtk_frame_new("Remote Directory");
    GtkWidget *remote_dir_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 5);
    gtk_container_add(GTK_CONTAINER(remote_dir_frame), remote_dir_box);
    gtk_container_set_border_width(GTK_CONTAINER(remote_dir_box), 5);
    GtkWidget *remote_dir_label = gtk_label_new("Path:");
    gtk_widget_set_size_request(remote_dir_label, 100, -1);
    remote_dir_entry = gtk_entry_new();
    gtk_entry_set_placeholder_text(GTK_ENTRY(remote_dir_entry), "Remote folder path");
    change_dir_button = gtk_button_new_with_label("Change Dir");
    gtk_widget_set_sensitive(change_dir_button, FALSE);
    g_signal_connect(change_dir_button, "clicked", G_CALLBACK(on_change_dir_clicked), NULL);
    gtk_box_pack_start(GTK_BOX(remote_dir_box), remote_dir_label, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(remote_dir_box), remote_dir_entry, TRUE, TRUE, 0);
    gtk_box_pack_start(GTK_BOX(remote_dir_box), change_dir_button, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(vbox), remote_dir_frame, FALSE, FALSE, 0);

    // Move / rename
    GtkWidget *move_frame = gtk_frame_new("Move / Rename");
    GtkWidget *move_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 5);
    gtk_container_add(GTK_CONTAINER(move_frame), move_box);
    gtk_container_set_border_width(GTK_CONTAINER(move_box), 5);
    GtkWidget *move_label = gtk_label_new("Target:");
    gtk_widget_set_size_request(move_label, 100, -1);
    move_target_entry = gtk_entry_new();
    gtk_entry_set_placeholder_text(GTK_ENTRY(move_target_entry), "folder/ or new name");
    move_button = gtk_button_new_with_label("Move");
    gtk_widget_set_sensitive(move_button, FALSE);
    gtk_widget_set_sensitive(move_target_entry, FALSE);
    g_signal_connect(move_button, "clicked", G_CALLBACK(on_move_clicked), NULL);
    gtk_box_pack_start(GTK_BOX(move_box), move_label, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(move_box), move_target_entry, TRUE, TRUE, 0);
    gtk_box_pack_start(GTK_BOX(move_box), move_button, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(vbox), move_frame, FALSE, FALSE, 0);
    
    // File transfer
    GtkWidget *transfer_frame = gtk_frame_new("File Transfer");
    GtkWidget *transfer_vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 5);
    gtk_container_add(GTK_CONTAINER(transfer_frame), transfer_vbox);
    gtk_container_set_border_width(GTK_CONTAINER(transfer_vbox), 5);
    
    GtkWidget *remote_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 5);
    GtkWidget *remote_label = gtk_label_new("Remote File:");
    gtk_widget_set_size_request(remote_label, 100, -1);
    remote_file_entry = gtk_entry_new();
    gtk_entry_set_placeholder_text(GTK_ENTRY(remote_file_entry), "remote.txt");
    gtk_box_pack_start(GTK_BOX(remote_box), remote_label, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(remote_box), remote_file_entry, TRUE, TRUE, 0);
    gtk_box_pack_start(GTK_BOX(transfer_vbox), remote_box, FALSE, FALSE, 0);
    
    GtkWidget *local_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 5);
    GtkWidget *local_label = gtk_label_new("Local File:");
    gtk_widget_set_size_request(local_label, 100, -1);
    local_file_entry = gtk_entry_new();
    gtk_entry_set_placeholder_text(GTK_ENTRY(local_file_entry), "local path");
    gtk_box_pack_start(GTK_BOX(local_box), local_label, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(local_box), local_file_entry, TRUE, TRUE, 0);
    gtk_box_pack_start(GTK_BOX(transfer_vbox), local_box, FALSE, FALSE, 0);
    
    GtkWidget *transfer_button_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 5);
    upload_button = gtk_button_new_with_label("Upload");
    download_button = gtk_button_new_with_label("Download");
    local_file_browse_button = gtk_button_new_with_label("Browse");
    save_path_button = gtk_button_new_with_label("Save");
    gtk_widget_set_sensitive(upload_button, FALSE);
    gtk_widget_set_sensitive(download_button, FALSE);
    gtk_widget_set_sensitive(local_file_browse_button, FALSE);
    gtk_widget_set_sensitive(save_path_button, FALSE);
    g_signal_connect(upload_button, "clicked", G_CALLBACK(on_upload_clicked), NULL);
    g_signal_connect(download_button, "clicked", G_CALLBACK(on_download_clicked), NULL);
    g_signal_connect(local_file_browse_button, "clicked", G_CALLBACK(on_select_local_file_clicked), NULL);
    g_signal_connect(save_path_button, "clicked", G_CALLBACK(on_select_save_path_clicked), NULL);
    gtk_box_pack_start(GTK_BOX(transfer_button_box), upload_button, TRUE, TRUE, 0);
    gtk_box_pack_start(GTK_BOX(transfer_button_box), download_button, TRUE, TRUE, 0);
    gtk_box_pack_start(GTK_BOX(transfer_button_box), local_file_browse_button, TRUE, TRUE, 0);
    gtk_box_pack_start(GTK_BOX(transfer_button_box), save_path_button, TRUE, TRUE, 0);
    gtk_box_pack_start(GTK_BOX(transfer_vbox), transfer_button_box, FALSE, FALSE, 0);
    
    gtk_box_pack_start(GTK_BOX(vbox), transfer_frame, FALSE, FALSE, 0);
    
    // Status
    status_label = gtk_label_new("Not connected");
    gtk_box_pack_start(GTK_BOX(vbox), status_label, FALSE, FALSE, 0);
    
    gtk_widget_show_all(window);
    gtk_main();
    
    return 0;
}