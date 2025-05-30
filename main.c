/* 
 * main.c
 */
#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <string.h>
#include <time.h>
#include <sqlite3.h>
#include <sys/stat.h>
#include <dirent.h>
#include <unistd.h>
#include <pwd.h>
#include <grp.h>
#include <sys/types.h>
#if defined(__APPLE__)
#include <stdbool.h>
#include <sys/acl.h>
#include <sys/xattr.h>
#else
#include <acl/libacl.h>
#endif
#include <zlib.h>

#include "itool.h"
#include "hash.h"
#include "registry.h"

sqlite3 *db;
sqlite3_stmt *stmt;
sqlite3_stmt *check_stmt;
sqlite3_stmt *mark_stmt;

const char *CreateFiles_sql = \
	"CREATE TABLE IF NOT EXISTS files ( \
		\"path\" TEXT PRIMARY KEY, \
		\"size\" INTEGER, \
		\"sha256\" TEXT,\
		\"mode\" INTEGER, \
		\"acl\" TEXT, \
		\"created\" INTEGER, \
		\"modified\" INTEGER, \
		\"accessed\" INTEGER, \
		\"xattrs\" TEXT, \
		\"uid\" INTEGER, \
		\"gid\" INTEGER, \
		\"device\" INTEGER, \
		\"symlink\" TEXT  \
	);";

const char *InsertFiles_sql = \
	"INSERT OR REPLACE INTO files ( \
		\"path\", \
		\"size\", \
		\"sha256\", \
		\"mode\", \
		\"acl\", \
		\"created\", \
		\"modified\", \
		\"accessed\", \
		\"xattrs\", \
		\"uid\", \
		\"gid\", \
		\"device\", \
		\"symlink\" \
	) VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?);";

const char *CreateMark_sql = "CREATE TEMP TABLE IF NOT EXISTS mark (path TEXT PRIMARY KEY);";
const char *InsertMark_sql = "INSERT OR REPLACE INTO mark (path) VALUES (?);";
const char *CheckFiles_sql = "SELECT * FROM files WHERE path = ?;";
const char *MissingFiles_sql = "SELECT path FROM files WHERE path NOT IN (SELECT path FROM mark);";

#define Mode	0
#define Opts	1

struct
{
	int	var;
	char 	*name;
	int 	flag;
} options[] = 
{
	{ Mode, "compare", COMPARE },
	{ Opts, "hash",    D_HASH },
	{ Opts, "size",    D_SIZE },
	{ Opts, "link",    D_LINK },
	{ Opts, "uid",     D_UID },
	{ Opts, "gid",     D_GID },
	{ Opts, "ctime",   D_CTIME },
	{ Opts, "mtime",   D_MTIME },
	{ Opts, "atime",   D_ATIME },
	{ Opts, "mode",    D_MODE },
	{ Opts, "xattr",   D_XATTR },
	{ Opts, "acl",     D_ACL },
	{ Opts, "dev",     D_DEV },
	{ 0,    0,         D_NONE }
};

mode_t	mode = GENERATE;
ulong_t opts = (D_HASH|D_SIZE|D_LINK|D_UID|D_GID|D_CTIME|D_MTIME|D_MODE|D_XATTR|D_ACL|D_DEV);

char	*dbpath;
char	*startdir;

char *
pathname(char *dir,char *filepath)
{
	char *pathname;

	if (filepath[0]=='/')
	{
		pathname=strdup(filepath);
		return filepath;
	}

	if ((pathname=malloc(strlen(dir)+strlen(filepath)+2)) == (char *)0)
		return 0;

	sprintf(pathname,"%s/%s",dir,filepath);

	for(ulong_t i=0;i<strlen(pathname);i++)
	{
		while (pathname[i] == '/' && pathname[i+1]=='/')
		{
			for(ulong_t j=i; j<strlen(pathname); j++)
				pathname[j]=pathname[j+1];
		}
	}
	return pathname;
}

void 
get_xattrs(const char *path, char *out_buf, size_t out_size) 
{
#if defined(__APPLE__)
	char xattr_buf[4096];

	ssize_t list_len = listxattr(path, xattr_buf, sizeof(xattr_buf), XATTR_NOFOLLOW);
	if (list_len <= 0) 
	{
		snprintf(out_buf, out_size, "none");
		return;
	}
	out_buf[0] = '\0';
	for (ssize_t i = 0; i < list_len;) 
	{
		const char *attr = &xattr_buf[i];
		size_t len = strlen(attr);
		strncat(out_buf, attr, out_size - strlen(out_buf) - 1);
		strncat(out_buf, ",", out_size - strlen(out_buf) - 1);
		i += len + 1;
	}
	size_t l = strlen(out_buf);
	if (l > 0 && out_buf[l - 1] == ',') out_buf[l - 1] = '\0';
#else
	snprintf(out_buf, out_size, "n/a");
#endif
}

int
join(char **str,const char *fmt, ...)
{
static	char	buf[1024];
	va_list ap;

	va_start(ap,fmt);
	vsnprintf(buf,sizeof(buf)-1,fmt,ap);
	va_end(ap);

	if (*str == 0)
		*str=strdup(buf);
	else
	{
		*str=realloc(*str,strlen(*str)+strlen(buf)+1);
		strcat(*str,",");
		strcat(*str,buf);
	}
	return 0;
}

int 
main(int argc, char **argv) 
{
	int	flags;

	if (argc < 3) 
	{
		fprintf(stderr, "Usage: %s <directory> <output.db> [--compare]\n", argv[0]);
		return 1;
	}

	startdir = argv[1];
	dbpath = pathname(argv[1],argv[2]);

	flags = SQLITE_OPEN_READWRITE;

	for(int i=3;i<argc;i++)
	{ 
		char *name=argv[i];

		int set=0;
		if (strncmp(name,"--",2) == 0) name+=2;
		if (strncmp(name,"no",2) == 0)
		{
			name+=2;
			set=1;
		}
		int found=0;
		for(int j=0; options[j].name; j++)
		{
			if (strcmp(name, options[j].name) == 0)
			{
				found=1;
				switch(options[j].var)
				{
				case Mode:
					mode = options[j].flag;
					break;
				case Opts:
					if (set == 0) 
						opts |= options[j].flag;
					else 
						opts &= ~options[j].flag;
					break;
				}
			}
		}
		if (found==0)
		{
			printf("Unknown option [%s]\n",argv[i]);
			exit(0);
		}
	}

	if (mode == GENERATE)
		flags |= SQLITE_OPEN_CREATE;

	if (sqlite3_open_v2(dbpath,&db,flags,NULL)) 
	{
		fprintf(stderr, "Can't open database: %s\n", sqlite3_errmsg(db));
		return 1;
	}

	if (mode == GENERATE) 
	{
		printf("Audit %s - Creating %s\n",startdir,dbpath);

		if (SQLITE_OK != sqlite3_exec(db, CreateFiles_sql, 0, 0, 0)) 
		{
			fprintf(stderr, "SQL error: %s\n", sqlite3_errmsg(db));
			return 1;
		}

		if (SQLITE_OK != sqlite3_prepare_v2(db, InsertFiles_sql, -1, &stmt, 0)) 
		{
			fprintf(stderr, "Failed to prepare insert: %s\n", sqlite3_errmsg(db));
			return 1;
		}
	}
	else
	{
		printf("Auditing %s against %s\n",startdir,dbpath);

		if (SQLITE_OK != sqlite3_exec(db, CreateMark_sql, 0, 0, 0))
		{
			fprintf(stderr, "Failed to create mark table: %s\n", sqlite3_errmsg(db));
			return 1;
		}

		if (SQLITE_OK != sqlite3_prepare_v2(db, InsertMark_sql, -1, &mark_stmt, 0)) 
		{
			fprintf(stderr, "Failed to prepare mark statement: %s\n", sqlite3_errmsg(db));
			return 1;
		}

		if (SQLITE_OK != sqlite3_prepare_v2(db, CheckFiles_sql, -1, &check_stmt, 0)) 
		{
			fprintf(stderr, "Failed to prepare check statement: %s\n", sqlite3_errmsg(db));
			return 1;
		}
	} 

	scan_directory(startdir);

	if (mode == COMPARE) 
	{
		char	**results;
		int	rows,cols;

		if (SQLITE_OK == sqlite3_get_table(db, MissingFiles_sql, &results, &rows, &cols, 0))
		{
			for (int i = 1; i <= rows; i++) 
				printf("DELETED: %s\n", results[i]);
		}
		sqlite3_free_table(results);
	}

	sqlite3_finalize(stmt);
	sqlite3_finalize(check_stmt);
	sqlite3_close(db);
}

void 
scan_directory(const char *dirpath) 
{
	DIR 	*dir = opendir(dirpath);
	char 	fullpath[4096];
	struct dirent *entry;

	if ((dir = opendir(dirpath)) == (DIR *)0) return;

	while ((entry = readdir(dir)) != NULL) 
	{
		if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0) continue;

		snprintf(fullpath, sizeof(fullpath), "%s/%s", dirpath, entry->d_name);
		if (strcmp(fullpath, dbpath) == 0) continue;

		struct stat st;
		if (lstat(fullpath, &st) == -1) continue;

		if (S_ISDIR(st.st_mode)) 
		{
			scan_directory(fullpath);
			continue;
		}

		unsigned char hash[32];
		sha256_file(fullpath, hash);
		char hex[65] = {0};
		for (int i = 0; i < 32; i++) 
			sprintf(hex + i * 2, "%02x", hash[i]);


		char symlink_target[1024] = {0};
		if (S_ISLNK(st.st_mode)) 
		{
			ssize_t len = readlink(fullpath, symlink_target, sizeof(symlink_target)-1);
			if (len > 0) 
				symlink_target[len] = '\0';
			else 
				strcpy(symlink_target, "unreadable");
		} 
		else 
			strcpy(symlink_target, "n/a");

		char acl_str[64] = "n/a";
		char xattr_str[1024];
		get_xattrs(fullpath, xattr_str, sizeof(xattr_str));

		if (mode == GENERATE) 
		{
			sqlite3_bind_text(stmt,  1,fullpath, -1, SQLITE_STATIC);
			sqlite3_bind_int64(stmt, 2,st.st_size);
			sqlite3_bind_text(stmt,  3,hex, -1, SQLITE_STATIC);
			sqlite3_bind_int(stmt,   4,st.st_mode);
			sqlite3_bind_text(stmt,  5,acl_str, -1, SQLITE_STATIC);
			sqlite3_bind_int64(stmt, 6,st.st_ctime);
			sqlite3_bind_int64(stmt, 7,st.st_mtime);
			sqlite3_bind_int64(stmt, 8,st.st_atime);
			sqlite3_bind_text(stmt,  9,xattr_str, -1, SQLITE_STATIC);
			sqlite3_bind_int64(stmt,10,st.st_uid);
			sqlite3_bind_int64(stmt,11,st.st_gid);
			sqlite3_bind_int64(stmt,12,st.st_dev);
			sqlite3_bind_text(stmt, 13,symlink_target, -1, SQLITE_STATIC);

			sqlite3_step(stmt);
			sqlite3_reset(stmt);
		}
		else
		{
			sqlite3_bind_text(check_stmt, 1, fullpath, -1, SQLITE_STATIC);
			sqlite3_bind_text(mark_stmt, 1, fullpath, -1, SQLITE_STATIC);

			sqlite3_step(mark_stmt);
			sqlite3_reset(mark_stmt);

			if (sqlite3_step(check_stmt) == SQLITE_ROW) 
			{
				const off_t db_size   = (const int64) sqlite3_column_int64(check_stmt, 2-1);
				const char *db_hash   = (const char *)sqlite3_column_text(check_stmt,  3-1);
				const mode_t db_mode  = (const mode_t)sqlite3_column_int(check_stmt,   4-1);
				const char *db_acl    = (const char *)sqlite3_column_text(check_stmt,  5-1);
				const time_t db_ctime = (const time_t)sqlite3_column_int(check_stmt,   6-1);
				const time_t db_mtime = (const time_t)sqlite3_column_int(check_stmt,   7-1);
				const time_t db_atime = (const time_t)sqlite3_column_int(check_stmt,   8-1);
				const char *db_xattr  = (const char *)sqlite3_column_text(check_stmt,  9-1);
				const uint_t db_uid   = (const uint_t)sqlite3_column_int(check_stmt,  10-1);
				const uint_t db_gid   = (const uint_t)sqlite3_column_int64(check_stmt,11-1);
				const int db_dev      = (const int)   sqlite3_column_int(check_stmt,  12-1);
				const char *db_link   = (const char *)sqlite3_column_text(check_stmt, 13-1);

				char symlink_target[1024] = {0};

				// This isn't right - should be db_mode
				if (S_ISLNK(st.st_mode)) 
				{
					ssize_t len = readlink(fullpath, symlink_target, sizeof(symlink_target)-1);
					if (len > 0) 
						symlink_target[len] = 0;
					else 
						strcpy(symlink_target, "unreadable");
				} 
				else
					strcpy(symlink_target, "n/a");

				char	*cbuf=0;


				if (opts&D_SIZE && db_size != st.st_size) join(&cbuf,"Size: %lld->%lld",db_size,st.st_size); 
				if (opts&D_HASH && strcmp(db_hash,hex)) join(&cbuf,"Hash: %s->%s",db_hash,hex);
				if (opts&D_LINK && strcmp(db_link,symlink_target)) join(&cbuf,"Symlink: %s->%s",db_link,symlink_target); 
				if (opts&D_UID && db_uid != st.st_uid) join(&cbuf,"UID: %d->%d",db_uid,st.st_uid);
				if (opts&D_GID && db_gid != st.st_gid) join(&cbuf,"GID: %d->%d",db_gid,st.st_gid);
				if (opts&D_CTIME && db_ctime != st.st_ctime) join(&cbuf,"Created: %ld->%ld",db_ctime,st.st_ctime);
				if (opts&D_MTIME && db_mtime != st.st_mtime) join(&cbuf,"Modified: %ld->%ld",db_mtime,st.st_mtime);
				if (opts&D_ATIME && db_atime != st.st_atime) join(&cbuf,"Access: %ld->%ld",db_atime,st.st_atime);
				if (opts&D_MODE && MODE(db_mode) != MODE(st.st_mode)) join(&cbuf,"Mode: %ho->%ho",MODE(db_mode),MODE(st.st_mode));
				if (opts&D_XATTR && strcmp(db_xattr, xattr_str)) join(&cbuf,"Xattrs: %s->%s",db_xattr,xattr_str);
				if (opts&D_ACL && strcmp(db_acl, acl_str)) join(&cbuf,"ACL: %s->%s",db_acl,acl_str);
				if (opts&D_DEV && db_dev != st.st_dev) join(&cbuf,"Device: %d->%d",db_dev,st.st_dev);

				if (cbuf != 0) 
					printf("CHANGED: %s (%s)\n",fullpath,cbuf);
                        }
                        else
                                printf("NEW: %s\n", fullpath);

                        sqlite3_reset(check_stmt);
                }
        }
        closedir(dir);
}
