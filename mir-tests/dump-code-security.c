#include <limits.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#include "../mir.h"

static void fatal (const char *message) {
  fprintf (stderr, "%s\n", message);
  exit (1);
}

static void write_file (const char *path, const char *content) {
  FILE *f = fopen (path, "w");

  if (f == NULL) fatal ("cannot create test file");
  if (fputs (content, f) == EOF) fatal ("cannot write test file");
  fclose (f);
}

static void read_file (const char *path, char *buf, size_t size) {
  FILE *f = fopen (path, "r");

  if (f == NULL) fatal ("cannot read test file");
  if (fgets (buf, (int) size, f) == NULL) fatal ("cannot read file contents");
  fclose (f);
}

int main (void) {
  char dir_template[] = "/tmp/mir-dump-security-XXXXXX";
  char old_cwd[PATH_MAX], path_buf[PATH_MAX * 2];
  char victim_path[PATH_MAX], c_symlink_path[PATH_MAX], bin_symlink_path[PATH_MAX];
  char fake_gcc_path[PATH_MAX], fake_objdump_path[PATH_MAX];
  char gcc_hit_path[PATH_MAX], objdump_hit_path[PATH_MAX];
  char victim_buf[64], gcc_script[PATH_MAX + 64], objdump_script[PATH_MAX + 64];
  uint8_t code[] = {0x90, 0x90, 0x90, 0x90};
  const char *dir = mkdtemp (dir_template), *old_path;
  unsigned long pid = (unsigned long) getpid ();

  if (dir == NULL) fatal ("cannot create temp directory");
  if (getcwd (old_cwd, sizeof (old_cwd)) == NULL) fatal ("cannot get cwd");

  snprintf (victim_path, sizeof (victim_path), "%s/victim.txt", dir);
  snprintf (c_symlink_path, sizeof (c_symlink_path), "%s/_mir_%lu.c", dir, pid);
  snprintf (bin_symlink_path, sizeof (bin_symlink_path), "%s/_mir_%lu.bin", dir, pid);
  snprintf (fake_gcc_path, sizeof (fake_gcc_path), "%s/gcc", dir);
  snprintf (fake_objdump_path, sizeof (fake_objdump_path), "%s/objdump", dir);
  snprintf (gcc_hit_path, sizeof (gcc_hit_path), "%s/gcc-hit", dir);
  snprintf (objdump_hit_path, sizeof (objdump_hit_path), "%s/objdump-hit", dir);

  write_file (victim_path, "SAFE\n");
  unlink (c_symlink_path);
  unlink (bin_symlink_path);
  if (symlink (victim_path, c_symlink_path) != 0) fatal ("cannot create .c symlink");
  if (symlink (victim_path, bin_symlink_path) != 0) fatal ("cannot create .bin symlink");

  snprintf (gcc_script, sizeof (gcc_script), "#!/bin/sh\necho hit > '%s'\nexit 0\n", gcc_hit_path);
  snprintf (objdump_script, sizeof (objdump_script), "#!/bin/sh\necho hit > '%s'\nexit 0\n",
            objdump_hit_path);
  write_file (fake_gcc_path, gcc_script);
  write_file (fake_objdump_path, objdump_script);
  if (chmod (fake_gcc_path, 0700) != 0) fatal ("cannot chmod fake gcc");
  if (chmod (fake_objdump_path, 0700) != 0) fatal ("cannot chmod fake objdump");

  old_path = getenv ("PATH");
  snprintf (path_buf, sizeof (path_buf), "%s:%s", dir, old_path == NULL ? "" : old_path);
  if (setenv ("PATH", path_buf, 1) != 0) fatal ("cannot set PATH");
  if (chdir (dir) != 0) fatal ("cannot chdir");

  _MIR_dump_code ("security-test", code, sizeof (code));

  if (chdir (old_cwd) != 0) fatal ("cannot restore cwd");
  read_file (victim_path, victim_buf, sizeof (victim_buf));
  if (strcmp (victim_buf, "SAFE\n") != 0) fatal ("predictable temp file overwrote target");
  if (access (gcc_hit_path, F_OK) == 0) fatal ("dump code executed gcc from PATH");
  if (access (objdump_hit_path, F_OK) == 0) fatal ("dump code executed objdump from PATH");
  return 0;
}
