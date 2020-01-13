/*****************************************************************************
* File: file.h
*
* (c) 2016 Sentons Inc. - All Rights Reserved.
*
* All information contained herein is and remains the property of Sentons
* Incorporated and its suppliers if any. The intellectual and technical
* concepts contained herein are proprietary to Sentons Incorporated and its
* suppliers and may be covered by U.S. and Foreign Patents, patents in
* process, and are protected by trade secret or copyright law. Dissemination
* of this information or reproduction of this material is strictly forbidden
* unless prior written permission is obtained from Sentons Incorporated.
*
* SENTONS PROVIDES THIS SOURCE CODE STRICTLY ON AN "AS IS" BASIS,
* WITHOUT ANY WARRANTY WHATSOEVER, AND EXPRESSLY DISCLAIMS ALL
* WARRANTIES, EXPRESS, IMPLIED OR STATUTORY WITH REGARD THERETO, INCLUDING
* THE IMPLIED WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR
* PURPOSE, TITLE OR NON-INFRINGEMENT OF THIRD PARTY RIGHTS. SENTONS SHALL
* NOT BE LIABLE FOR ANY DAMAGES SUFFERED BY YOU AS A RESULT OF USING,
* MODIFYING OR DISTRIBUTING THIS SOFTWARE OR ITS DERIVATIVES.
*****************************************************************************/

#include "config.h"
#include "file.h"
#include "debug.h"

/*==========================================================================*/
/* FILE UTILITIES                                                           */
/*==========================================================================*/
int file_open(const char* path, int flags, int rights, struct file **f) {
    struct file* filp = NULL;
    mm_segment_t oldfs;

    oldfs = get_fs();
    set_fs(get_ds());
    filp = filp_open(path, flags, rights);
    set_fs(oldfs);
    if(IS_ERR(filp)) {
        return PTR_ERR(filp);
    }
    *f = filp;
    return 0;
}

void file_close(struct file* file) {
    filp_close(file, NULL);
}

int file_read(struct file* file, unsigned long long offset, unsigned char* data, unsigned int size) {
    mm_segment_t oldfs;
    int ret;

    oldfs = get_fs();
    set_fs(get_ds());

    ret = vfs_read(file, data, size, &offset);

    set_fs(oldfs);
    return ret;
}

int file_write(struct file* file, unsigned long long offset, unsigned char* data, unsigned int size) {
    mm_segment_t oldfs;
    int ret;

    oldfs = get_fs();
    set_fs(get_ds());

    ret = vfs_write(file, data, size, &offset);

    set_fs(oldfs);
    return ret;
}

int file_size(struct file *file, int *p_size) {
    struct kstat stat;
    int error = -EBADF;

    if (file) {
        if (p_size) {
            //error = vfs_getattr(file->f_vfsmnt, file->f_dentry, &stat);
	    error = vfs_getattr(&file->f_path, &stat);
            *p_size = stat.size;
            error = 0;
        } else {
            error = -EINVAL;
        }
    }
    return error;
}
