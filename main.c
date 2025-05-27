#include "scan.h"
#include "registry.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <sqlite3.h>
#include <openssl/evp.h>
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

sqlite3 *db;
sqlite3_stmt *stmt;
sqlite3_stmt *check_stmt;
sqlite3_stmt *mark_stmt;

const char *create_sql = "CREATE TABLE IF NOT EXISTS files ("
		"\"path\" TEXT PRIMARY KEY, \"size\" INTEGER, \"sha256\" TEXT,"
		"\"user\" TEXT, \"group\" TEXT, \"mode\" TEXT, \"acl\" TEXT,"
		"\"created\" TEXT, \"modified\" TEXT, \"accessed\" TEXT, \"xattrs\" TEXT);";

const char *insert_sql = "INSERT OR REPLACE INTO files "
		"(\"path\", \"size\", \"sha256\", \"user\", \"group\", \"mode\", \"acl\", \"created\", \"modified\", \"accessed\", \"xattrs\") "
			"VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?);";

#define MODE_GEN	0
#define MODE_COMPARE	1

int	mode = MODE_GEN;
char	*dbpath;
char	*startdir;

int 
main(int argc, char **argv) 
{
	int	rc, flags;
	char	**results;
	int	rows,cols;

	if (argc < 3) 
	{
		fprintf(stderr, "Usage: %s <directory> <output.db> [--compare]\n", argv[0]);
		return 1;
	}

	startdir = argv[1];
	dbpath = argv[2];
	flags = SQLITE_OPEN_READWRITE;

	if (argc > 3 && strcmp(argv[3], "--compare") == 0) 
	{
		mode = MODE_COMPARE;
		flags |= SQLITE_OPEN_CREATE;
	}

	if (sqlite3_open_v2(dbpath,&db,flags,NULL)) 
	{
		fprintf(stderr, "Can't open database: %s\n", sqlite3_errmsg(db));
		return 1;
	}

	printf("Scanning directory %s\n",startdir);
	if (mode == MODE_GEN) 
	{
		printf("Generating %s\n",dbpath);

		if (sqlite3_exec(db, create_sql, 0, 0, 0) != SQLITE_OK) 
		{
			fprintf(stderr, "SQL error: %s\n", sqlite3_errmsg(db));
			return 1;
		}

		if (sqlite3_prepare_v2(db, insert_sql, -1, &stmt, 0) != SQLITE_OK) 
		{
			fprintf(stderr, "Failed to prepare insert: %s\n", sqlite3_errmsg(db));
			return 1;
		}
	}
	else
	{
		printf("Comparing changes to %s\n",dbpath);

		rc = sqlite3_exec(db, "CREATE TEMP TABLE IF NOT EXISTS mark (path TEXT PRIMARY KEY);", 0, 0, 0);
		if (rc != SQLITE_OK) 
		{
			fprintf(stderr, "Failed to create mark table: %s\n", sqlite3_errmsg(db));
			return 1;
		}

		const char *mark_sql = "INSERT OR REPLACE INTO mark (path) VALUES (?);";
		if (sqlite3_prepare_v2(db, mark_sql, -1, &mark_stmt, 0) != SQLITE_OK) 
		{
			fprintf(stderr, "Failed to prepare mark statement: %s\n", sqlite3_errmsg(db));
			return 1;
		}

		const char *check_sql = "SELECT sha256 FROM files WHERE path = ?;";
		if (sqlite3_prepare_v2(db, check_sql, -1, &check_stmt, 0) != SQLITE_OK) 
		{
			fprintf(stderr, "Failed to prepare check statement: %s\n", sqlite3_errmsg(db));
			return 1;
		}
	} 

	scan_directory(startdir);

	if (mode == MODE_COMPARE) 
	{
		rc = sqlite3_get_table(db, "SELECT path FROM files WHERE path NOT IN (SELECT path FROM mark);",
			&results, &rows, &cols, 0);
		if (rc == SQLITE_OK) 
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
sha256_file(const char *filename, unsigned char output[32]) 
{
	FILE *file = fopen(filename, "rb");

	if (!file) return;

	EVP_MD_CTX *ctx = EVP_MD_CTX_new();
	if (!ctx) 
	{
		fclose(file);
		return;
	}

	EVP_DigestInit_ex(ctx, EVP_sha256(), NULL);

	unsigned char buffer[4096];
	size_t bytes;
	while ((bytes = fread(buffer, 1, sizeof(buffer), file)) != 0) 
	{
		EVP_DigestUpdate(ctx, buffer, bytes);
	}

	EVP_DigestFinal_ex(ctx, output, NULL);
	EVP_MD_CTX_free(ctx);
	fclose(file);
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

void 
scan_directory(const char *dirpath) 
{
	DIR 	*dir = opendir(dirpath);
	struct dirent *entry;
	char 	fullpath[4096];

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

		struct passwd *pw = getpwuid(st.st_uid);
		struct group  *gr = getgrgid(st.st_gid);
		char mode_str[16];
		snprintf(mode_str, sizeof(mode_str), "%o", st.st_mode & 07777);

		char acl_str[64] = "n/a";
		char xattr_str[1024];
		get_xattrs(fullpath, xattr_str, sizeof(xattr_str));

		if (mode == MODE_GEN) 
		{
			sqlite3_bind_text(stmt,  1, fullpath, -1, SQLITE_STATIC);
			sqlite3_bind_int64(stmt, 2, st.st_size);
			sqlite3_bind_text(stmt,  3, hex, -1, SQLITE_STATIC);
			sqlite3_bind_text(stmt,  4, pw ? pw->pw_name : "?", -1, SQLITE_STATIC);
			sqlite3_bind_text(stmt,  5, gr ? gr->gr_name : "?", -1, SQLITE_STATIC);
			sqlite3_bind_text(stmt,  6, mode_str, -1, SQLITE_STATIC);
			sqlite3_bind_text(stmt,  7, acl_str, -1, SQLITE_STATIC);
			sqlite3_bind_text(stmt,  8, "?", -1, SQLITE_STATIC); // created
			sqlite3_bind_text(stmt,  9, "?", -1, SQLITE_STATIC); // modified
			sqlite3_bind_text(stmt, 10, "?", -1, SQLITE_STATIC); // accessed
			sqlite3_bind_text(stmt, 11, xattr_str, -1, SQLITE_STATIC);
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
				const char *db_hash = (const char *)sqlite3_column_text(check_stmt, 0);
				if (strcmp(db_hash, hex) != 0) 
				{
					printf("CHANGED: %s\n", fullpath);
				}
			} 
			else 
			{
				printf("NEW: %s\n", fullpath);
			}
            		sqlite3_reset(check_stmt);
		} 
	}
	closedir(dir);
}
