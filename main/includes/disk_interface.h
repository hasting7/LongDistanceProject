#ifndef _DISK_INTERFACE_H_
#define _DISK_INTERFACE_H_

#include <stddef.h>

extern const char* CONFIG_TYPE;
extern const char* USER_TYPE;
extern const char* SCREEN_TYPE;

void store_struct(const char *type, const char *recall_name, void *data_in, size_t struct_size);
bool get_struct(const char *type, const char *recall_name, void *data_out, size_t *struct_size);
void delete_struct(const char *type, const char *recall_name);
void clear_segment(const char *type);
void disk_init();


#endif