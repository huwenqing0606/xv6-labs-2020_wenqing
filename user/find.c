#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"
#include "kernel/fs.h"

#define IN  0
#define OUT 1
#define ERR 2

#define READEND  0
#define WRITEEND 1

#define O_RDONLY 0

// 实现一个find函数的功能
// given a path, return the formatted file name, size is DIRSIZ
char*
fmtname(char *path)
{
  static char buf[DIRSIZ+1];
  char *p;

  // Find first character after last slash.
  for(p=path+strlen(path); p >= path && *p != '/'; p--)
    ;
  p++;

  // Return blank-padded name.
  if(strlen(p) >= DIRSIZ)
    return p;
  memmove(buf, p, strlen(p));
  // the returned file name is formatted with ' ' appended to it to match DIRSIZ size
  memset(buf+strlen(p), ' ', DIRSIZ-strlen(p));
  return buf;
}

// find the file with given name inside a given path
void
find(char *path, char *name)
{
  char buf[512], name_fmt[DIRSIZ+1], *p;
  int fd;
  struct dirent de;
  struct stat st;

  // formatted the name for search with ' ' appended to it to match DIRSIZ size
  memmove(name_fmt, name, strlen(name));
  memset(name_fmt+strlen(name), ' ', DIRSIZ-strlen(name));

  if((fd = open(path, O_RDONLY)) < 0){
    fprintf(ERR, "find: cannot open %s\n", path);
    return;
  }

  if(fstat(fd, &st) < 0){
    fprintf(ERR, "find: cannot stat %s\n", path);
    close(fd);
    return;
  }

  switch(st.type){
  case T_FILE: // if file check whether the name matches
    if (strcmp(fmtname(path), name_fmt) == 0) 
      fprintf(OUT, "%s\n", path);
    break;

  case T_DIR: // if directory recursively call find 
    if(strlen(path) + 1 + DIRSIZ + 1 > sizeof buf){
      printf("find: path too long\n");
      break;
    }
    strcpy(buf, path);
    p = buf+strlen(buf);
    *p++ = '/';
    while(read(fd, &de, sizeof(de)) == sizeof(de)){
      // inum == 0 means invalid directory entry
      if(de.inum == 0)
        continue;
      // add de.name to path
      memmove(p, de.name, DIRSIZ);
      // don't find . and ..
      if (!strcmp(de.name, ".") || !strcmp(de.name, "..")) continue;
      // recursive call find
      find(buf, name);
    }
    break;
  }
  close(fd);
}

int 
main(int argc, char *argv[])
{
    if (argc != 3) 
    {
        fprintf(ERR, "usage: find <path> <name>\n");
        exit(1);
    }
    find(argv[1], argv[2]);
    exit(0);
}
