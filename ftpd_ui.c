#include <gtk/gtk.h>
#include <limits.h>
#include "ftp_common.h"

extern int start_ftp_server(const char *bind_ip, int port);
extern int accept_ftp_client(int server_fd, const char *server_ip);

static GtkWidget *ip_entry;
static GtkWidget *port_entry;
static GtkWidget *start_button;
static GtkWidget *stop_button;
static GtkWidget *status_text;
static GtkWidget *status_scrolled;
static GtkTextBuffer *status_buffer;
static GtkWidget *server_state_label;
static GtkWidget *root_dir_entry;
static GtkWidget *root_dir_button;
static int server_fd = -1;
static gboolean server_running = FALSE;
static GThread *server_thread = NULL;

static void append_status(const char *message) {
    GtkTextIter iter;
    gtk_text_buffer_get_end_iter(status_buffer, &iter);
    gtk_text_buffer_insert(status_buffer, &iter, message, -1);
    gtk_text_buffer_insert(status_buffer, &iter, "\n", -1);
    
    // Auto-scroll to bottom
    if (status_scrolled) {
        GtkAdjustment *adj = gtk_scrolled_window_get_vadjustment(
            GTK_SCROLLED_WINDOW(status_scrolled));
        if (adj) {
            gtk_adjustment_set_value(adj, gtk_adjustment_get_upper(adj) - gtk_adjustment_get_page_size(adj));
        }
    }
}

static void on_browse_root_clicked(GtkWidget *widget, gpointer data) {
    (void)data;
    GtkWidget *dialog = gtk_file_chooser_dialog_new(
        "Select Server Root",
        GTK_WINDOW(gtk_widget_get_toplevel(widget)),
        GTK_FILE_CHOOSER_ACTION_SELECT_FOLDER,
        "_Cancel", GTK_RESPONSE_CANCEL,
        "_Open", GTK_RESPONSE_ACCEPT,
        NULL);
    if (gtk_dialog_run(GTK_DIALOG(dialog)) == GTK_RESPONSE_ACCEPT) {
        char *folder = gtk_file_chooser_get_filename(GTK_FILE_CHOOSER(dialog));
        if (folder) {
            gtk_entry_set_text(GTK_ENTRY(root_dir_entry), folder);
            g_free(folder);
        }
    }
    gtk_widget_destroy(dialog);
}

static gboolean append_status_idle(gpointer data) {
    gchar *msg = (gchar *)data;
    append_status(msg);
    g_free(msg);
    return FALSE;
}

static gpointer server_thread_func(gpointer data) {
    (void)data; // Suppress unused parameter warning
    while (server_running && server_fd >= 0) {
        int client_fd = accept_ftp_client(server_fd, 
            gtk_entry_get_text(GTK_ENTRY(ip_entry)));
        if (client_fd >= 0) {
            gchar *msg = g_strdup_printf("Client connected: %d", client_fd);
            g_idle_add(append_status_idle, msg);
        } else if (server_running) {
            g_usleep(100000); // 100ms
        }
    }
    return NULL;
}

static void on_start_clicked(GtkWidget *widget, gpointer data) {
    (void)widget; (void)data; // Suppress unused parameter warnings
    if (server_running) return;
    
    const char *root_dir = gtk_entry_get_text(GTK_ENTRY(root_dir_entry));
    if (root_dir && strlen(root_dir) > 0) {
        if (chdir(root_dir) != 0) {
            append_status("Cannot change to selected directory");
            return;
        }
    }

    const char *ip = gtk_entry_get_text(GTK_ENTRY(ip_entry));
    const char *port_str = gtk_entry_get_text(GTK_ENTRY(port_entry));
    int port = atoi(port_str);
    
    if (port <= 0 || port > 65535) {
        append_status("Invalid port number");
        return;
    }
    
    server_fd = start_ftp_server(ip, port);
    if (server_fd < 0) {
        append_status("Failed to start server");
        return;
    }
    
    server_running = TRUE;
    gtk_widget_set_sensitive(start_button, FALSE);
    gtk_widget_set_sensitive(stop_button, TRUE);
    gtk_widget_set_sensitive(ip_entry, FALSE);
    gtk_widget_set_sensitive(port_entry, FALSE);
    gtk_widget_set_sensitive(root_dir_entry, FALSE);
    gtk_widget_set_sensitive(root_dir_button, FALSE);
    
    char msg[256];
    snprintf(msg, sizeof(msg), "Server started on %s:%d", ip, port);
    append_status(msg);
    gtk_label_set_text(GTK_LABEL(server_state_label), "Running");
    
    server_thread = g_thread_new("server", server_thread_func, NULL);
}

static void on_stop_clicked(GtkWidget *widget, gpointer data) {
    (void)widget; (void)data; // Suppress unused parameter warnings
    if (!server_running) return;
    
    server_running = FALSE;
    if (server_fd >= 0) {
        close(server_fd);
        server_fd = -1;
    }
    
    gtk_widget_set_sensitive(start_button, TRUE);
    gtk_widget_set_sensitive(stop_button, FALSE);
    gtk_widget_set_sensitive(ip_entry, TRUE);
    gtk_widget_set_sensitive(port_entry, TRUE);
    gtk_widget_set_sensitive(root_dir_entry, TRUE);
    gtk_widget_set_sensitive(root_dir_button, TRUE);
    
    append_status("Server stopped");
    gtk_label_set_text(GTK_LABEL(server_state_label), "Stopped");
}

static void on_destroy(GtkWidget *widget, gpointer data) {
    (void)widget; (void)data; // Suppress unused parameter warnings
    server_running = FALSE;
    if (server_fd >= 0) {
        close(server_fd);
    }
    gtk_main_quit();
}

int main(int argc, char *argv[]) {
    gtk_init(&argc, &argv);
    
    GtkWidget *window = gtk_window_new(GTK_WINDOW_TOPLEVEL);
    gtk_window_set_title(GTK_WINDOW(window), "FTP Server");
    gtk_window_set_default_size(GTK_WINDOW(window), 600, 500);
    g_signal_connect(window, "destroy", G_CALLBACK(on_destroy), NULL);
    
    GtkWidget *vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 5);
    gtk_container_add(GTK_CONTAINER(window), vbox);
    gtk_container_set_border_width(GTK_CONTAINER(vbox), 10);
    
    // IP input
    GtkWidget *ip_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 5);
    GtkWidget *ip_label = gtk_label_new("Server IP:");
    gtk_widget_set_size_request(ip_label, 100, -1);
    ip_entry = gtk_entry_new();
    gtk_entry_set_text(GTK_ENTRY(ip_entry), "0.0.0.0");
    gtk_entry_set_placeholder_text(GTK_ENTRY(ip_entry), "Bind IP (0.0.0.0)");
    gtk_box_pack_start(GTK_BOX(ip_box), ip_label, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(ip_box), ip_entry, TRUE, TRUE, 0);
    gtk_box_pack_start(GTK_BOX(vbox), ip_box, FALSE, FALSE, 0);
    
    // Port input
    GtkWidget *port_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 5);
    GtkWidget *port_label = gtk_label_new("Port:");
    gtk_widget_set_size_request(port_label, 100, -1);
    port_entry = gtk_entry_new();
    gtk_entry_set_text(GTK_ENTRY(port_entry), "9999");
    gtk_entry_set_placeholder_text(GTK_ENTRY(port_entry), "Port");
    gtk_box_pack_start(GTK_BOX(port_box), port_label, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(port_box), port_entry, TRUE, TRUE, 0);
    gtk_box_pack_start(GTK_BOX(vbox), port_box, FALSE, FALSE, 0);

    // Root directory
    GtkWidget *root_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 5);
    GtkWidget *root_label = gtk_label_new("Root Dir:");
    gtk_widget_set_size_request(root_label, 100, -1);
    root_dir_entry = gtk_entry_new();
    char cwd_buffer[PATH_MAX];
    if (getcwd(cwd_buffer, sizeof(cwd_buffer))) {
        gtk_entry_set_text(GTK_ENTRY(root_dir_entry), cwd_buffer);
    }
    gtk_entry_set_placeholder_text(GTK_ENTRY(root_dir_entry), "Server root directory");
    root_dir_button = gtk_button_new_with_label("Browse");
    g_signal_connect(root_dir_button, "clicked", G_CALLBACK(on_browse_root_clicked), NULL);
    gtk_box_pack_start(GTK_BOX(root_box), root_label, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(root_box), root_dir_entry, TRUE, TRUE, 0);
    gtk_box_pack_start(GTK_BOX(root_box), root_dir_button, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(vbox), root_box, FALSE, FALSE, 0);
    
    // Buttons
    GtkWidget *button_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 5);
    start_button = gtk_button_new_with_label("Start Server");
    stop_button = gtk_button_new_with_label("Stop Server");
    gtk_widget_set_sensitive(stop_button, FALSE);
    g_signal_connect(start_button, "clicked", G_CALLBACK(on_start_clicked), NULL);
    g_signal_connect(stop_button, "clicked", G_CALLBACK(on_stop_clicked), NULL);
    gtk_box_pack_start(GTK_BOX(button_box), start_button, TRUE, TRUE, 0);
    gtk_box_pack_start(GTK_BOX(button_box), stop_button, TRUE, TRUE, 0);
    gtk_box_pack_start(GTK_BOX(vbox), button_box, FALSE, FALSE, 0);
    
    // Status text
    GtkWidget *status_label = gtk_label_new("Status:");
    gtk_box_pack_start(GTK_BOX(vbox), status_label, FALSE, FALSE, 0);
    server_state_label = gtk_label_new("Stopped");
    gtk_box_pack_start(GTK_BOX(vbox), server_state_label, FALSE, FALSE, 0);
    
    status_scrolled = gtk_scrolled_window_new(NULL, NULL);
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(status_scrolled),
                                   GTK_POLICY_AUTOMATIC, GTK_POLICY_AUTOMATIC);
    status_text = gtk_text_view_new();
    status_buffer = gtk_text_view_get_buffer(GTK_TEXT_VIEW(status_text));
    gtk_text_view_set_editable(GTK_TEXT_VIEW(status_text), FALSE);
    gtk_container_add(GTK_CONTAINER(status_scrolled), status_text);
    gtk_box_pack_start(GTK_BOX(vbox), status_scrolled, TRUE, TRUE, 0);
    
    append_status("FTP Server - Ready to start");
    
    gtk_widget_show_all(window);
    gtk_main();
    
    return 0;
}