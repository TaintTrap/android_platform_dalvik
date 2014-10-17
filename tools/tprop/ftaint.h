#include "attr/xattr.h"

#define TAINT_XATTR_NAME "user.taint"

#define USAGE "Usage: %s [g|s|a] <file> [<hex>]\n"
#define TAINT_CLEAR 0x0

typedef unsigned int u4;

u4 getTaintXattr(const char *path)
{
    int ret;
    u4 buf;
    u4 tag = TAINT_CLEAR;

    ret = getxattr(path, TAINT_XATTR_NAME, &buf, sizeof(buf)); 
    if (ret > 0) {
        tag = buf;
    } else {
        if (errno == ENOATTR) {
            fprintf(stdout, "getxattr(%s): no taint tag\n", path);
        } else if (errno == ERANGE) {
            fprintf(stderr, "Error: getxattr(%s) contents to large\n", path);
        } else if (errno == ENOTSUP) {
            fprintf(stderr, "Error: getxattr(%s) not supported\n", path);
        } else {
            fprintf(stderr, "Errro: getxattr(%s): unknown error code %d\n", path, errno);
        }
    }

    return tag;
}

void setTaintXattr(const char *path, u4 tag)
{
    int ret;

    ret = setxattr(path, TAINT_XATTR_NAME, &tag, sizeof(tag), 0);

    if (ret < 0) {
        if (errno == ENOSPC || errno == EDQUOT) {
            fprintf(stderr, "Error: setxattr(%s): not enough room to set xattr\n", path);
        } else if (errno == ENOTSUP) {
            fprintf(stderr, "Error: setxattr(%s) not supported\n", path);
        } else {
            fprintf(stderr, "Errro: setxattr(%s): unknown error code %d\n", path, errno);
        }
    }
}

