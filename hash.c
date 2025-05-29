/*
 * hash.c
 */
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

#include "itool.h"

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
