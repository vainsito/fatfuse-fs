# FAT-FUSE File System Project

## Overview

This repository houses the development of the FAT-FUSE File System project, designed to enhance the functionality of a FAT file system and introduce transparent user activity monitoring. Leveraging the FUSE (Filesystem in Userspace) module, the project provides flexibility in implementation and debugging compared to traditional kernel-level development.

## Implemented Features

### 1. File System Surveillance

- **Creation of Log File:**
  - A hidden log file (/fs.log) is created to record user file read and write operations.
  - Careful consideration ensures the proper creation of the log file after mounting the volume, preventing duplication.
  
- **Selective Logging:**
  - Read and write operations are selectively logged, offering a detailed record of user activity.
  
- **Hidden File Handling:**
  - The log file is hidden from commands like `ls` but remains accessible for reading and writing using commands like `cat` or redirections.

### 2. Cross-FAT Compatibility

- **Transparency in Surveillance:**
  - Surveillance functionality extends to other FAT implementations, ensuring compatibility with native Linux FAT systems.
  
- **Strategic Log File Attributes:**
  - The log file's first byte is marked as 0xe5, designating it as "pending for deletion" without affecting its integrity.
  - The entry's attribute field is set to System, Device, or Reserved, preventing modification by standard file system tools.

### 3. Unlink and rmdir Functionality

- **Deletion Operations:**
  - Addition of unlink and rmdir functionality to delete files and directories.
  - Proper handling of tasks such as marking clusters as free, updating directory entries, and maintaining the in-memory directory tree.
  - Low-level functions ensure efficient marking of entries for deletion, involving writing 0xE5 in the first byte of the directory entry.

### 4. Dynamic Cluster Allocation for Write Operations

- **Enhanced Write Operations:**
  - Modification to allow writing beyond pre-allocated clusters for a file.
  - Improved fat_file_pwrite function to dynamically allocate new clusters and perform complete write operations.
  - Inclusion of error logging functions (e.g., fat_table_print, fat_file_print_dentry) for debugging purposes.

## Project Structure

- **fat_fuse_ops.c:** Contains implementations for surveillance, log creation, and hiding log files.
- **fat_file.c:** Implements unlink, rmdir, and dynamic cluster allocation for write operations.
- **fat_table.c:** Provides functions for handling FAT table operations.
- **Additional Recommendations:** Encourages efficient debugging and focused problem-solving.

## How to Use

- **Requirements for Approval:**
  - Utilize Bitbucket for version control with small and meaningful commits.
  - Maintain consistent code styling using clang-format.
  - Pass the provided tests by the instructor.
  - Deliver a basic implementation in the main branch, with optional star-point features in a separate branch.

## Contributions and Collaboration

- **Collaborative Development:**
  - Continuous collaboration from all group members:
    - Ignacio Ramirez
    - Alexis Ortiz
    - Ezequiel Marin Reyna
    - Manu Saharrea
  
- **Version Control:**
  - Utilize Bitbucket for version control, emphasizing small and meaningful commits.
  
- **Coding Style:**
  - Maintain a consistent code style using clang-format.
