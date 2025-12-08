# FTP Client and Server with GTK UI

A complete FTP implementation in C with GTK-based graphical user interfaces for both the client and server.

## Features

### FTP Server
- Multi-threaded server supporting multiple concurrent connections
- Basic FTP protocol commands:
  - USER/PASS (authentication)
  - PWD (print working directory)
  - CWD (change working directory)
  - LIST (list directory contents)
  - RETR (retrieve/download file)
  - STOR (store/upload file)
  - PASV (passive mode)
  - QUIT (disconnect)
- GTK-based GUI for server management
- Configurable port (default: 21)

### FTP Client
- Full-featured FTP client
- Connect to any FTP server
- List remote directory contents
- Upload files to server
- Download files from server
- Change remote directory
- View current remote directory
- GTK-based GUI for easy operation

## Requirements

- GCC compiler
- GTK+ 3.0 development libraries
- pthread library (usually included with glibc)
- pkg-config

### Installing Dependencies

**Ubuntu/Debian:**
```bash
sudo apt-get install build-essential libgtk-3-dev pkg-config
```

**Fedora/RHEL:**
```bash
sudo dnf install gcc gtk3-devel pkg-config
```

**Arch Linux:**
```bash
sudo pacman -S base-devel gtk3 pkg-config
```

## Building

Simply run:
```bash
make
```

This will create two executables:
- `ftpd_ui` - FTP Server with GUI
- `ftp_client_ui` - FTP Client with GUI

To clean build artifacts:
```bash
make clean
```

## Usage

### Starting the FTP Server

1. Run the server:
   ```bash
   ./ftpd_ui
   ```

2. In the GUI:
   - Set the port (default: 21, requires root privileges for ports < 1024)
   - Click "Start Server"
   - The status window will show server activity

**Note:** If you want to use port 21, you may need to run with sudo:
```bash
sudo ./ftpd_ui
```

### Using the FTP Client

1. Run the client:
   ```bash
   ./ftp_client_ui
   ```

2. In the GUI:
   - Enter server address (e.g., `127.0.0.1` for localhost)
   - Enter port (default: 21)
   - Enter username (any username is accepted by the server)
   - Enter password (any password is accepted by the server)
   - Click "Connect"
   - Once connected, you can:
     - View remote files in the file list
     - Click "Refresh" to update the file list
     - Upload files: Enter local file path and remote filename, click "Upload"
     - Download files: Enter remote filename and local file path, click "Download"

## Architecture

### Server Components
- `ftp_common.h/c` - Common FTP protocol utilities
- `ftpd.c` - FTP server implementation
- `ftpd_ui.c` - GTK GUI for FTP server

### Client Components
- `ftp_common.h/c` - Common FTP protocol utilities
- `ftp_client.h/c` - FTP client implementation
- `ftp_client_ui.c` - GTK GUI for FTP client

## Protocol Implementation

The implementation follows the basic FTP protocol (RFC 959):
- Control connection on port 21 (configurable)
- Passive mode for data transfers
- Standard FTP response codes
- Multi-line responses where appropriate

## Security Notes

This is a basic FTP implementation intended for educational purposes:
- Authentication accepts any username/password combination
- No encryption (plain FTP, not FTPS)
- No access control or file permissions checking
- Not recommended for production use without additional security measures

## Troubleshooting

### Port Already in Use
If you get a "bind: Address already in use" error:
- Another FTP server may be running
- Try a different port (e.g., 2121)
- Or stop the existing service

### Permission Denied
If you can't bind to port 21:
- Use a port above 1024, or
- Run with sudo (not recommended for production)

### GTK Not Found
If you get GTK-related errors:
- Ensure GTK+ 3.0 development libraries are installed
- Check that pkg-config can find GTK: `pkg-config --modversion gtk+-3.0`

## License

This project is provided as-is for educational purposes.

