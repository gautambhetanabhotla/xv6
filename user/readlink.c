#include "kernel/fcntl.h"
#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"

int readlink(const char *path, char *buf, int bufsize)
{
  struct stat st;
  int fd = open(path, O_RDONLY | O_NOFOLLOW);
  if (fd < 0) {
    return -1; // Error opening the symbolic link
  }

  if (fstat(fd, &st) < 0) {
    close(fd);
    return -1; // Not a symlink (or stat failed)
  }

  if (st.type != T_SYMLINK) {
    close(fd);
    return -2; // Not a symbolic link
  }

  int bytes_read = read(fd, buf, bufsize - 1); // Leave space for null terminator
  if (bytes_read < 0) {
    close(fd);
    return -1; // Error reading the symbolic link
  }

  buf[bytes_read] = '\0'; // Null-terminate the buffer
  close(fd);
  return bytes_read; // Return the number of bytes read
}

int main(int argc, char *argv[])
{
  if (argc != 2) {
    printf("Usage: readlink <symbolic_link_path>\n");
    return 1;
  }
  char buf[256];
  int bytes_read = readlink(argv[1], buf, sizeof(buf));
  if (bytes_read == -1) {
    printf("Error reading symbolic link\n");
    return 1;
  } else if (bytes_read == -2) {
    printf("Not a symbolic link\n");
    return 1;
  }
  printf("%s\n", buf);
  return 0;
}