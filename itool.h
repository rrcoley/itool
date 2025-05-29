#ifndef ITOOL_H
#define ITOOL_H

#define MODE(X)	(X&07777)

typedef unsigned int uint_t;
typedef unsigned long ulong_t;
typedef long long int64;
typedef unsigned long long uint64;

void	scan_directory(const char *dir);
char 	*pathname(char *dir,char *filepath);
void 	get_xattrs(const char *path, char *out_buf, size_t out_size);

#endif /*ITOOL_H*/
