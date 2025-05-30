#ifndef ITOOL_H
#define ITOOL_H

enum mode_t { GENERATE, COMPARE };

#define	D_NONE  0
#define	D_HASH  (1<<0)
#define	D_SIZE  (1<<1)
#define	D_LINK  (1<<2)
#define	D_UID   (1<<3)
#define	D_GID   (1<<4)
#define	D_CTIME (1<<5)
#define	D_MTIME (1<<6)
#define	D_ATIME (1<<7)
#define	D_MODE  (1<<8)
#define	D_XATTR (1<<9)
#define	D_ACL   (1<<10)
#define	D_DEV   (1<<11)

#define MODE(X)	(X&07777)

typedef unsigned int uint_t;
typedef unsigned long ulong_t;
typedef long long int64;
typedef unsigned long long uint64;

void	scan_directory(const char *dir);
char 	*pathname(char *dir,char *filepath);
void 	get_xattrs(const char *path, char *out_buf, size_t out_size);

#endif /*ITOOL_H*/
