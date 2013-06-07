// Static file handler module
//
// Copyright (c) 2013 The Wheatserver Author. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file. See the AUTHORS file for names of contributors.

#include <fcntl.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/uio.h>
#include <sys/types.h>
#include <libgen.h>

#include "../application.h"
#include "../../protocol/http/proto_http.h"

int staticFileCall(struct conn *, void *);
int initStaticFile(struct protocol *);
void deallocStaticFile();
void *initStaticFileData(struct conn *);
void freeStaticFileData(void *app_data);

// Static File Configuration
static struct configuration StaticConf[] = {
    {"static-file-dir",   2, stringValidator,      {.ptr=NULL},
        NULL,                   STRING_FORMAT},
    {"file-maxsize",      2, unsignedIntValidator, {.val=WHEAT_MAX_FILE_LIMIT},
        NULL,                   INT_FORMAT},
    {"allowed-extension", 2, stringValidator,      {.ptr=WHEAT_ASTERISK},
        (void *)WHEAT_NOTFREE,  STRING_FORMAT},
    {"directory-index",   2, stringValidator,      {.ptr=NULL},
        NULL,                   STRING_FORMAT},
};

static struct app AppStatic = {
    "Http", NULL, staticFileCall, initStaticFile,
    deallocStaticFile, initStaticFileData, freeStaticFileData, 0
};

static struct moduleAttr AppStaticAttr = {
    "static-file", APP, {.app=&AppStatic}, NULL, 0,
    StaticConf, sizeof(StaticConf)/sizeof(struct configuration),
    NULL, 0
};

static unsigned int MaxFileSize = WHEAT_MAX_BUFFER_SIZE;
static struct dict *AllowExtensions = NULL;
static wstr IfModifiedSince = NULL;
static struct list *DirectoryIndex = NULL;

struct contenttype {
    const char *extension;
    const char *mime_type;
};

struct staticFileData {
    wstr filename;
    wstr extension;
    int fd;
};

static struct contenttype ContentTypes[] = {
    {"ai", "application/postscript"},
    {"aif", "audio/x-aiff"},
    {"aifc", "audio/x-aiff"},
    {"aiff", "audio/x-aiff"},
    {"asc", "text/plain"},
    {"au", "audio/basic"},
    {"avi", "video/x-msvideo"},
    {"bcpio", "application/x-bcpio"},
    {"bmp", "image/bmp"},
    {"cdf", "application/x-netcdf"},
    {"cpio", "application/x-cpio"},
    {"cpt", "application/mac-compactpro"},
    {"csh", "application/x-csh"},
    {"css", "text/css"},
    {"dcr", "application/x-director"},
    {"dir", "application/x-director"},
    {"djv", "image/vnd.djvu"},
    {"djvu", "image/vnd.djvu"},
    {"doc", "application/msword"},
    {"dvi", "application/x-dvi"},
    {"dxr", "application/x-director"},
    {"eps", "application/postscript"},
    {"etx", "text/x-setext"},
    {"ez", "application/andrew-inset"},
    {"gif", "image/gif"},
    {"gtar", "application/x-gtar"},
    {"hdf", "application/x-hdf"},
    {"hqx", "application/mac-binhex40"},
    {"htm", "text/html"},
    {"html", "text/html"},
    {"ice", "x-conference/x-cooltalk"},
    {"ief", "image/ief"},
    {"iges", "model/iges"},
    {"igs", "model/iges"},
    {"jpe", "image/jpeg"},
    {"jpeg", "image/jpeg"},
    {"jpg", "image/jpeg"},
    {"js", "application/x-javascript"},
    {"kar", "audio/midi"},
    {"latex", "application/x-latex"},
    {"m3u", "audio/x-mpegurl"},
    {"man", "application/x-troff-man"},
    {"me", "application/x-troff-me"},
    {"mesh", "model/mesh"},
    {"mid", "audio/midi"},
    {"midi", "audio/midi"},
    {"mif", "application/vnd.mif"},
    {"mov", "video/quicktime"},
    {"movie", "video/x-sgi-movie"},
    {"mp2", "audio/mpeg"},
    {"mp3", "audio/mpeg"},
    {"mpe", "video/mpeg"},
    {"mpeg", "video/mpeg"},
    {"mpg", "video/mpeg"},
    {"mpga", "audio/mpeg"},
    {"ms", "application/x-troff-ms"},
    {"msh", "model/mesh"},
    {"mxu", "video/vnd.mpegurl"},
    {"nc", "application/x-netcdf"},
    {"oda", "application/oda"},
    {"pbm", "image/x-portable-bitmap"},
    {"pdb", "chemical/x-pdb"},
    {"pdf", "application/pdf"},
    {"pgm", "image/x-portable-graymap"},
    {"pgn", "application/x-chess-pgn"},
    {"png", "image/png"},
    {"pnm", "image/x-portable-anymap"},
    {"ppm", "image/x-portable-pixmap"},
    {"ppt", "application/vnd.ms-powerpoint"},
    {"ps", "application/postscript"},
    {"qt", "video/quicktime"},
    {"ra", "audio/x-realaudio"},
    {"ram", "audio/x-pn-realaudio"},
    {"ras", "image/x-cmu-raster"},
    {"rgb", "image/x-rgb"},
    {"rm", "audio/x-pn-realaudio"},
    {"roff", "application/x-troff"},
    {"rpm", "audio/x-pn-realaudio-plugin"},
    {"rtf", "text/rtf"},
    {"rtx", "text/richtext"},
    {"sgm", "text/sgml"},
    {"sgml", "text/sgml"},
    {"sh", "application/x-sh"},
    {"shar", "application/x-shar"},
    {"silo", "model/mesh"},
    {"sit", "application/x-stuffit"},
    {"skd", "application/x-koan"},
    {"skm", "application/x-koan"},
    {"skp", "application/x-koan"},
    {"skt", "application/x-koan"},
    {"smi", "application/smil"},
    {"smil", "application/smil"},
    {"snd", "audio/basic"},
    {"spl", "application/x-futuresplash"},
    {"src", "application/x-wais-source"},
    {"sv4cpio", "application/x-sv4cpio"},
    {"sv4crc", "application/x-sv4crc"},
    {"swf", "application/x-shockwave-flash"},
    {"t", "application/x-troff"},
    {"tar", "application/x-tar"},
    {"tcl", "application/x-tcl"},
    {"tex", "application/x-tex"},
    {"texi", "application/x-texinfo"},
    {"texinfo", "application/x-texinfo"},
    {"tif", "image/tiff"},
    {"tiff", "image/tiff"},
    {"tr", "application/x-troff"},
    {"tsv", "text/tab-separated-values"},
    {"txt", "text/plain"},
    {"ustar", "application/x-ustar"},
    {"vcd", "application/x-cdlink"},
    {"vrml", "model/vrml"},
    {"wav", "audio/x-wav"},
    {"wbmp", "image/vnd.wap.wbmp"},
    {"wbxml", "application/vnd.wap.wbxml"},
    {"wml", "text/vnd.wap.wml"},
    {"wmlc", "application/vnd.wap.wmlc"},
    {"wmls", "text/vnd.wap.wmlscript"},
    {"wmlsc", "application/vnd.wap.wmlscriptc"},
    {"wrl", "model/vrml"},
    {"xbm", "image/x-xbitmap"},
    {"xht", "application/xhtml+xml"},
    {"xhtml", "application/xhtml+xml"},
    {"xls", "application/vnd.ms-excel"},
    {"xml", "text/xml"},
    {"xpm", "image/x-xpixmap"},
    {"xsl", "text/xml"},
    {"xwd", "image/x-xwindowdump"},
    {"xyz", "chemical/x-xyz"},
    {"zip", "application/zip"},
};

static int fillResHeaders(struct conn *c, off_t rep_len, time_t m_time)
{
    int ret = 0;
    struct staticFileData *static_data = c->app_private_data;
    int i;
    char buf[50];

    if (rep_len != 0) {
        ret = snprintf(buf, sizeof(buf), "%lld", rep_len);
        if (ret < 0 || ret > sizeof(buf))
            return -1;
        appendToResHeaders(c, CONTENT_LENGTH, buf);
    }

    if (m_time != 0) {
        ret = convertHttpDate(m_time, buf, sizeof(buf));
        if (ret < 0)
            return -1;
        appendToResHeaders(c, LAST_MODIFIED, buf);
    }

    if (static_data->extension && static_data->filename) {
        int size = sizeof(ContentTypes)/sizeof(struct contenttype);
        for (i = 0; i < size; ++i) {
            if (!strcmp(static_data->extension, ContentTypes[i].extension)) {
                break;
            }
        }
        if (i != size) {
            appendToResHeaders(c, CONTENT_TYPE, ContentTypes[i].mime_type);
        } else {
            appendToResHeaders(c, CONTENT_TYPE, "application/octet-stream");
        }
    }
    return ret;
}

int staticFileCall(struct conn *c, void *arg)
{
    wstr path = arg;
    struct staticFileData *static_data;
    off_t len;
    time_t m_time;
    int ret;
    struct stat stat;

    static_data = c->app_private_data;
    static_data->fd = open(path, O_RDONLY);

    if (static_data->fd  == -1) {
        wheatLog(WHEAT_VERBOSE, "open file failed: %s", strerror(errno));
        goto failed404;
    }

    ret = lstat(path, &stat);
    if (ret == -1)
        goto failed404;

    if (!S_ISREG(stat.st_mode)) {
        close(static_data->fd);
        static_data->fd = -1;
        if (S_ISDIR(stat.st_mode) && DirectoryIndex) {
            struct listNode *node;
            struct listIterator *iter;
            wstr last;
            char append_path[255];
            iter = listGetIterator(DirectoryIndex, START_HEAD);
            while ((node = listNext(iter)) != NULL) {
                last = listNodeValue(node);
                snprintf(append_path, 255, "%s/%s", path, last);
                static_data->fd = open(append_path, O_RDONLY);

                if (static_data->fd  != -1) {
                    break;
                }
            }
        }
        if (static_data->fd == -1) {
            wheatLog(WHEAT_VERBOSE, "open file failed: %s", strerror(errno));
            goto failed404;
        }
    }
    if (AllowExtensions && static_data->extension &&
            !dictFetchValue(AllowExtensions, static_data->extension)) {
        goto failed404;
    }
    ret = getFileSizeAndMtime(static_data->fd , &len, &m_time);
    if (ret == WHEAT_WRONG) {
        wheatLog(WHEAT_VERBOSE, "open file failed: %s", strerror(errno));
        goto failed404;
    }
    if (len > MaxFileSize) {
        wheatLog(WHEAT_NOTICE, "file exceed max limit %d", len);
        wheatLog(WHEAT_VERBOSE, "open file failed: %s", strerror(errno));
        goto failed404;
    }

    wstr modified = dictFetchValue(httpGetReqHeaders(c), IfModifiedSince);
    if (modified != NULL) {
        char buf[wstrlen(modified)];
        memcpy(buf, modified, wstrlen(modified));
        time_t client_m_time = fromHttpDate(buf);
        if (m_time <= client_m_time) {
            fillResInfo(c, 304, "Not Modified");
            ret = fillResHeaders(c, 0, 0);
            if (ret == -1)
                goto failed;
            ret = httpSendHeaders(c);
            if (ret == -1)
                goto failed;
            return WHEAT_OK;
        }
    }
    fillResInfo(c, 200, "OK");
    ret = fillResHeaders(c, len, m_time);
    if (ret == -1) {
        wheatLog(WHEAT_WARNING, "fill Res Headers failes: %s", strerror(errno));
        goto failed;
    }
    ret = httpSendHeaders(c);
    if (ret == -1) {
        wheatLog(WHEAT_WARNING, "static file send headers failed: %s", strerror(errno));
        goto failed;
    }
    ret = sendClientFile(c, static_data->fd , len);
    if (ret == WHEAT_WRONG) {
        wheatLog(WHEAT_WARNING, "send static file failed: %s", strerror(errno));
        goto failed;
    }
    return WHEAT_OK;


failed404:
    sendResponse404(c);
    return WHEAT_OK;

failed:
    setClientClose(c);
    return WHEAT_OK;
}

int initStaticFile(struct protocol *p)
{
    int args, i, ret;
    wstr extensions, indexes;
    struct configuration *conf = getConfiguration("file-maxsize");
    MaxFileSize = conf->target.val;

    conf = getConfiguration("allowed-extension");
    extensions = wstrNew(conf->target.ptr);
    if (wstrIndex(extensions, '*') != -1) {
        AllowExtensions = NULL;
    } else {
        AllowExtensions = dictCreate(&wstrDictType);

        wstr *argvs = wstrNewSplit(extensions, ",", 1, &args);
        if (!argvs) {
            wheatLog(WHEAT_WARNING, "init Static File failed");
            return WHEAT_WRONG;
        }

        for (i = 0; i < args; ++i) {
            ret = dictAdd(AllowExtensions, wstrNew(argvs[i]), wstrNew("allowed"));
            if (ret == DICT_WRONG)
                break;
        }

        wstrFreeSplit(argvs, args);
    }
    wstrFree(extensions);

    conf = getConfiguration("directory-index");
    if (conf->target.ptr) {
        indexes = wstrNew(conf->target.ptr);
        DirectoryIndex = createList();
        listSetFree(DirectoryIndex, (void (*)(void*))wstrFree);
        wstr *argvs = wstrNewSplit(indexes, ",", 1, &args);
        if (!argvs) {
            wheatLog(WHEAT_WARNING, "split failed");
            return WHEAT_WRONG;
        }

        for (i = 0; i < args; ++i) {
            appendToListTail(DirectoryIndex, wstrNew(argvs[i]));
            if (ret == DICT_WRONG)
                break;
        }

        wstrFreeSplit(argvs, args);
    } else {
        DirectoryIndex = NULL;
    }

    IfModifiedSince = wstrNew(IF_MODIFIED_SINCE);
    return WHEAT_OK;
}

void deallocStaticFile()
{
    if (AllowExtensions)
        dictRelease(AllowExtensions);
    if (DirectoryIndex)
        freeList(DirectoryIndex);
    MaxFileSize = 0;
    wstrFree(IfModifiedSince);
}

void *initStaticFileData(struct conn *c)
{
    struct staticFileData *data = wmalloc(sizeof(*data));
    if (!data)
        return NULL;
    memset(data, 0, sizeof(*data));
    wstr path = httpGetPath(c);
    if (path) {
        char *base_name = basename(path);
        char *point = strrchr(base_name, '.');
        if (point && base_name && base_name < point) {
            data->extension = wstrNew(point+1);
            data->filename = wstrNewLen(base_name, (int)(point-base_name));
        }
    }
    data->fd = 0;
    return data;
}

void freeStaticFileData(void *app_data)
{
    struct staticFileData *data = app_data;
    wstrFree(data->extension);
    wstrFree(data->filename);
    if (data->fd > 0)
        close(data->fd);
    wfree(data);
}
