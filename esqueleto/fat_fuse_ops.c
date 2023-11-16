/*
 * fat_fuse_ops.c
 *
 * FAT32 filesystem operations for FUSE (Filesystem in Userspace)
 */

#include "fat_fuse_ops.h"
#include "big_brother.h"
#include "fat_file.h"
#include "fat_filename_util.h"
#include "fat_fs_tree.h"
#include "fat_util.h"
#include "fat_volume.h"
#include <alloca.h>
#include <errno.h>
#include <gmodule.h>
#include <libgen.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <unistd.h>

/* Retrieve the currently mounted FAT volume from the FUSE context. */
static inline fat_volume get_fat_volume() {
    return fuse_get_context()->private_data;
}

#define LOG_MESSAGE_SIZE 100
#define DATE_MESSAGE_SIZE 30

int initial_log = 0;

static void now_to_str(char *buf) {
    time_t now = time(NULL);
    struct tm *timeinfo;
    timeinfo = localtime(&now);

    strftime(buf, DATE_MESSAGE_SIZE, "%d-%m-%Y %H:%M", timeinfo);
}

static int fat_fuse_log_write(char *text){
    errno = 0;

    fat_volume vol = get_fat_volume();
    //Toma el volumen disponible
    fat_tree_node nlog = fat_tree_node_search(vol->file_tree, LOG_FILE);
    //Busca el nodo asociado a la llave LOG_FILE

    if(nlog == NULL){
        errno = ENOENT;
        return -errno;
    }
    //Si no existe el nodo, entonces el log file no existe

    fat_file log_file = fat_tree_get_file(nlog);
    //Consigo el file 
    fat_file parent = fat_tree_get_parent(nlog);
    //Consigo el padre
    fat_file_pwrite(log_file,text,strlen(text),log_file->dentry->file_size,parent);
    //Escribo el texto dentro del file

    return -errno;
}


// TODO: complete this function to log to file
static void fat_fuse_log_activity(char *operation_type, fat_file target_file)
{
    char buf[LOG_MESSAGE_SIZE] = "";
    now_to_str(buf);
    strcat(buf, "\t");
    strcat(buf, getlogin());
    strcat(buf, "\t");
    strcat(buf, target_file->filepath);
    strcat(buf, "\t");
    strcat(buf, operation_type);
    strcat(buf, "\n");

    fat_fuse_log_write(buf);
}

static int fat_fuse_log_init(void){
    errno = 0;
    fat_volume vol = get_fat_volume();

    
    char hid_name[] = LOG_FILE;
    hid_name[1] = FAT_FILENAME_DELETED_CHAR;

    fat_tree_node nlog = fat_tree_node_search(vol->file_tree, hid_name);
    if(nlog == NULL){
        int mknod_ex = fat_fuse_mknod(LOG_FILE, 0, 0);
        if(mknod_ex != 0){
            errno = EEXIST;
            return -errno;
        }
        nlog = fat_tree_node_search(vol->file_tree, LOG_FILE); //Ahora que el nodo existe lo podemos asignar a nuestra variable
        fat_file log_file = fat_tree_get_file(nlog); //Guardamos en nuestra variable log_file el file del log 

        fat_fuse_log_activity("INIT", log_file);
    } else {
        errno = EISDIR;
        return -errno;
    }

    return -errno;
}


/* Get file attributes (file descriptor version) */
int fat_fuse_fgetattr(const char *path, struct stat *stbuf,
                      struct fuse_file_info *fi) {
    fat_file file = (fat_file)fat_tree_get_file((fat_tree_node)fi->fh);
    fat_file_to_stbuf(file, stbuf);
    return 0;
}

/* Get file attributes (path version) */
int fat_fuse_getattr(const char *path, struct stat *stbuf) {
    fat_volume vol;
    fat_file file;

    vol = get_fat_volume();
    file = fat_tree_search(vol->file_tree, path);
    if (file == NULL) {
        errno = ENOENT;
        return -errno;
    }
    fat_file_to_stbuf(file, stbuf);
    return 0;
}

/* Open a file */
int fat_fuse_open(const char *path, struct fuse_file_info *fi) {
    fat_volume vol;
    fat_tree_node file_node;
    fat_file file;

    vol = get_fat_volume();
    file_node = fat_tree_node_search(vol->file_tree, path);
    if (!file_node)
        return -errno;
    file = fat_tree_get_file(file_node);
    if (fat_file_is_directory(file))
        return -EISDIR;
    fat_tree_inc_num_times_opened(file_node);
    fi->fh = (uintptr_t)file_node;
    return 0;
}

/* Open a directory */
int fat_fuse_opendir(const char *path, struct fuse_file_info *fi) {
    fat_volume vol = NULL;
    fat_tree_node file_node = NULL;
    fat_file file = NULL;

    vol = get_fat_volume();
    file_node = fat_tree_node_search(vol->file_tree, path);
    if (file_node == NULL) {
        return -errno;
    }
    file = fat_tree_get_file(file_node);
    if (!fat_file_is_directory(file)) {
        return -ENOTDIR;
    }
    fat_tree_inc_num_times_opened(file_node);
    fi->fh = (uintptr_t)file_node;
    return 0;
}

/* Read directory children. Calls function fat_file_read_children which returns
 * a list of files inside a GList. The children were read from the directory
 * entries in the cluster of the directory.
 * This function iterates over the list of children and adds them to the
 * file tree.
 * This operation should be performed only once per directory, the first time
 * readdir is called.
 */
static void fat_fuse_read_children(fat_tree_node dir_node) {
    fat_volume vol = get_fat_volume();
    fat_file dir = fat_tree_get_file(dir_node);
    GList *children_list = fat_file_read_children(dir);
    // Add child to tree. TODO handle duplicates
    for (GList *l = children_list; l != NULL; l = l->next) {
        vol->file_tree =
            fat_tree_insert(vol->file_tree, dir_node, (fat_file)l->data);
    }
}

static int is_fs_log(fat_file file){
    return (fat_file_cmp_path(file, LOG_FILE) == 0);
}

/* Add entries of a directory in @fi to @buf using @filler function. */
int fat_fuse_readdir(const char *path, void *buf, fuse_fill_dir_t filler,
                     off_t offset, struct fuse_file_info *fi) {
    errno = 0;
    fat_tree_node dir_node = (fat_tree_node)fi->fh;
    fat_file dir = fat_tree_get_file(dir_node);
    fat_file *children = NULL, *child = NULL;
    int error = 0;

    // Insert first two filenames (. and ..)
    if ((*filler)(buf, ".", NULL, 0) || (*filler)(buf, "..", NULL, 0)) {
        return -errno;
    }
    if (!fat_file_is_directory(dir)) {
        errno = ENOTDIR;
        return -errno;
    }
    if (dir->children_read != 1) {
        fat_fuse_read_children(dir_node);
        if (errno < 0) {
            return -errno;
        }
    }

    children = fat_tree_flatten_h_children(dir_node);
    child = children;
    while (*child != NULL) {
        if(!is_fs_log(*child)){
            error = (*filler)(buf, (*child)->name, NULL, 0);
            if (error != 0) {
                return -errno;
            }
        }
        child++;
    }
    if(!initial_log){
        fat_fuse_log_init();
        initial_log = 1;
    }
    

    return 0;
}

/* Read data from a file */
int fat_fuse_read(const char *path, char *buf, size_t size, off_t offset,
                  struct fuse_file_info *fi) {
    errno = 0;
    int bytes_read;
    fat_tree_node file_node = (fat_tree_node)fi->fh;
    fat_file file = fat_tree_get_file(file_node);
    fat_file parent = fat_tree_get_parent(file_node);

    bytes_read = fat_file_pread(file, buf, size, offset, parent);
    if (errno != 0) {
        return -errno;
    }
    fat_fuse_log_activity("read",file);
    return bytes_read;
}

/* Write data from a file */
int fat_fuse_write(const char *path, const char *buf, size_t size, off_t offset,
                   struct fuse_file_info *fi) {
    fat_tree_node file_node = (fat_tree_node)fi->fh;
    fat_file file = fat_tree_get_file(file_node);
    fat_file parent = fat_tree_get_parent(file_node);

    if (size == 0)
        return 0; // Nothing to write
    if (offset > file->dentry->file_size)
        return -EOVERFLOW;
    fat_fuse_log_activity("write", file);
    return fat_file_pwrite(file, buf, size, offset, parent);
}

/* Close a file */
int fat_fuse_release(const char *path, struct fuse_file_info *fi) {
    fat_tree_node file = (fat_tree_node)fi->fh;
    fat_tree_dec_num_times_opened(file);
    return 0;
}

/* Close a directory */
int fat_fuse_releasedir(const char *path, struct fuse_file_info *fi) {
    fat_tree_node file = (fat_tree_node)fi->fh;
    fat_tree_dec_num_times_opened(file);
    return 0;
}

int fat_fuse_mkdir(const char *path, mode_t mode) {
    errno = 0;
    fat_volume vol = NULL;
    fat_file parent = NULL, new_file = NULL;
    fat_tree_node parent_node = NULL;

    // The system has already checked the path does not exist. We get the parent
    vol = get_fat_volume();
    parent_node = fat_tree_node_search(vol->file_tree, dirname(strdup(path)));
    if (parent_node == NULL) {
        errno = ENOENT;
        return -errno;
    }
    parent = fat_tree_get_file(parent_node);
    if (!fat_file_is_directory(parent)) {
        fat_error("Error! Parent is not directory\n");
        errno = ENOTDIR;
        return -errno;
    }

    // init child
    new_file = fat_file_init(vol->table, true, strdup(path));
    if (errno != 0) {
        return -errno;
    }
    // insert to directory tree representation
    vol->file_tree = fat_tree_insert(vol->file_tree, parent_node, new_file);
    // write file in parent's entry (disk)
    fat_file_dentry_add_child(parent, new_file);
    return -errno;
}

/* Creates a new file in @path. @mode and @dev are ignored. */
int fat_fuse_mknod(const char *path, mode_t mode, dev_t dev) {
    errno = 0;
    fat_volume vol;
    fat_file parent, new_file;
    fat_tree_node parent_node;

    // The system has already checked the path does not exist. We get the parent
    vol = get_fat_volume();
    parent_node = fat_tree_node_search(vol->file_tree, dirname(strdup(path)));
    if (parent_node == NULL) {
        errno = ENOENT;
        return -errno;
    }
    parent = fat_tree_get_file(parent_node);
    if (!fat_file_is_directory(parent)) {
        fat_error("Error! Parent is not directory\n");
        errno = ENOTDIR;
        return -errno;
    }
    new_file = fat_file_init(vol->table, false, strdup(path));
    if (errno < 0) {
        return -errno;
    }
    // insert to directory tree representation
    vol->file_tree = fat_tree_insert(vol->file_tree, parent_node, new_file);
    // Write dentry in parent cluster
    fat_file_dentry_add_child(parent, new_file);
    return -errno;
}

int fat_fuse_utime(const char *path, struct utimbuf *buf) {
    errno = 0;
    fat_file parent = NULL;
    fat_volume vol = get_fat_volume();
    fat_tree_node file_node = fat_tree_node_search(vol->file_tree, path);
    if (file_node == NULL || errno != 0) {
        errno = ENOENT;
        return -errno;
    }
    parent = fat_tree_get_parent(file_node);
    if (parent == NULL || errno != 0) {
        DEBUG("WARNING: Setting time for parent ignored");
        return 0; // We do nothing, no utime for parent
    }
    fat_utime(fat_tree_get_file(file_node), parent, buf);
    return -errno;
}

/* Shortens the file at the given offset.*/
int fat_fuse_truncate(const char *path, off_t offset) {
    errno = 0;
    fat_volume vol = get_fat_volume();
    fat_file file = NULL, parent = NULL;
    fat_tree_node file_node = fat_tree_node_search(vol->file_tree, path);
    if (file_node == NULL || errno != 0) {
        errno = ENOENT;
        return -errno;
    }
    file = fat_tree_get_file(file_node);
    if (fat_file_is_directory(file))
        return -EISDIR;

    parent = fat_tree_get_parent(file_node);
    fat_tree_inc_num_times_opened(file_node);
    fat_file_truncate(file, offset, parent);
    return -errno;
}


/* Delete a file */
int fat_fuse_unlink(const char *path){
    errno = 0;
    //Nos ubicamos en el file con la tecnica habitual 
    fat_volume vol = get_fat_volume();
    fat_tree_node file_node = fat_tree_node_search(vol->file_tree, path);

    //En un contexto como este el file deberia de existir 
    if (file_node == NULL){
        errno = ENOENT;
        return -errno;
    }

    fat_file file = fat_tree_get_file(file_node);
    //Nos quedamos con el file que nos interesa borrar

    if(fat_file_is_directory(file)){
        errno = EISDIR;
        return -errno;
    }
    
    fat_file parent = fat_tree_get_parent(file_node);
    fat_file_free_cluster(file, parent);
    //Libera los clusters 
    fat_tree_delete(vol->file_tree,path);
    //Borra el fat_file con ruta = path, y arbol = vol->tree 

    return -errno;

}

/* Delete a directory */
int fat_fuse_rmdir(const char *path){

    errno = 0;

    //Nos ubicamos en el file con la tecnica habitual 
    fat_volume vol = get_fat_volume();
    fat_tree_node dir_node = fat_tree_node_search(vol->file_tree, path);

    //En un contexto como este el file deberia de existir 
    if (dir_node == NULL || errno != 0){
        errno = ENOENT;
        return -errno;
    }

    fat_file directory = fat_tree_get_file(dir_node);
    //Nos quedamos con el file que nos interesa borrar

    if(!fat_file_is_directory(directory)){
        errno = ENOTDIR;
        return -errno;
    }
    
    GList *children = fat_file_read_children(directory);
    bool is_empty = g_list_length(children) == 0;
    g_list_free(children);
    if (!is_empty) {
        errno = ENOTEMPTY;
    }

    fat_file parent = fat_tree_get_parent(dir_node);
    if (parent == NULL) {
        errno = EBUSY;
        return -errno;
    }

    fat_file_free_cluster(directory,parent);
    //Libera los clusters totales ocupados por el directorio
    fat_tree_delete(vol->file_tree,path);
    //Borra el fat_file con ruta = path, y arbol = vol->tree

    return -errno;

}