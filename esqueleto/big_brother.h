#ifndef _BIG_BROTHER_H
#define _BIG_BROTHER_H

#define LOG_FILEPATH PATH_SEPARATOR LOG_FILE
#define LOG_FILE LOG_FILE_BASENAME "." LOG_FILE_EXTENSION
#define LOG_FILE_BASENAME "fs"
#define LOG_FILE_EXTENSION "log"

int is_log_file_dentry(unsigned char *base_name, unsigned char *extension);

int is_log_filepath(char *filepath);

#endif