#ifndef REGISTRY_H
#define REGISTRY_H

#ifdef _WIN32
void 	process_registry_hive(const char *filepath, const char *hive_name);
#else
#define process_registry_hive(path, hive) ((void)0)
#endif

#endif
