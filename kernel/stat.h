#define T_DIR    1  // Directory
#define T_FILE   2  // File
#define T_DEVICE 3  // Device
#define T_SYMLINK 4 // Symbolic link

struct stat {
  int dev;     // File system's disk device
  uint ino;    // Inode number
  short type;  // Type of file
  short nlink; // Number of links to file
  uint64 size; // Size of file in bytes
  short permissions; // Permissions
  short owner;
};

#define P_S_BIT 0x40   // Sticky bit if dir, setuid bit if file
#define P_ROWN 0x20    // Read permission, owner
#define P_WOWN 0x10    // Write permission, owner
#define P_XOWN 0x8     // Execute/search permission, owner
#define P_ROTH 0x4     // Read permission, non-owner
#define P_WOTH 0x2     // Write permission, non-owner
#define P_XOTH 0x1     // Execute/search permission, non-owner
