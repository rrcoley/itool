#include "registry.h"
#include <stdio.h>

#ifdef _WIN32
#include <libregf.h>

void 
walk_keys(libregf_key_t *key, const char *prefix) 
{
        int value_count = 0;
        libregf_key_get_number_of_values(key, &value_count, NULL);

        for (int i = 0; i < value_count; i++) 
	{
		libregf_value_t *value = NULL;
		libregf_key_get_value(key, i, &value, NULL);

		const char *value_name = NULL;
		libregf_value_get_name(value, &value_name, NULL);
		if (!value_name) value_name = "(default)";

		const uint8_t *data = NULL;
		size_t data_size = 0;
		libregf_value_get_data(value, &data, &data_size, NULL);

		char hex_data[2048] = {0};
		for (size_t j = 0; j < data_size && j < 1024; j++) 
		{
			sprintf(&hex_data[j * 2], "%02x", data[j]);
		}

		int value_type = 0;
		libregf_value_get_type(value, &value_type, NULL);
		const char *type_str = libregf_value_type_string(value_type);

		printf("REG %s\%s : %s [%s] = %s\n", hive_name, prefix, value_name, type_str, hex_data);
		libregf_value_free(&value, NULL);
        }

	int subkey_count = 0;
	libregf_key_get_number_of_sub_keys(key, &subkey_count, NULL);
	for (int i = 0; i < subkey_count; i++) 
	{
		libregf_key_t *subkey = NULL;
		libregf_key_get_sub_key(key, i, &subkey, NULL);
		const char *subkey_name = NULL;
		libregf_key_get_name(subkey, &subkey_name, NULL);

		char new_prefix[1024];
		snprintf(new_prefix, sizeof(new_prefix), "%s\%s", prefix, subkey_name);
		walk_keys(subkey, new_prefix);
		libregf_key_free(&subkey, NULL);
	}
}

void 
process_registry_hive(const char *filepath, const char *hive_name) 
{
	libregf_handle_t *regf = NULL;
	if (libregf_handle_initialize(&regf, NULL) != 1) return;
	if (libregf_handle_open(regf, filepath, LIBREGF_OPEN_READ, NULL) != 1) 
	{
		libregf_handle_free(&regf, NULL);
		return;
	}

	libregf_key_t *root_key = NULL;
	if (libregf_handle_get_root_key(regf, &root_key, NULL) != 1) 
	{
		libregf_handle_free(&regf, NULL);
		return;
	}


	walk_keys(root_key, "");
	libregf_key_free(&root_key, NULL);
	libregf_handle_free(&regf, NULL);
}
#endif
