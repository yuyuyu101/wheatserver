#include <fcntl.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/uio.h>
#include <sys/types.h>

#include "../application.h"
#include "../../protocol/http/proto_http.h"

static unsigned int MaxFileSize = WHEAT_MAX_BUFFER_SIZE;
static struct dict *AllowExtensions = NULL;

struct contenttype {
    const char *extension;
    const char *mime_type;
};

struct staticFileData {
    wstr filename;
    wstr extension;
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

static int fillResHeaders(struct client *c, int rep_len)
{
    int ret;
    struct staticFileData *static_data = c->app_private_data;
    int i;
    char buf[8];
    ret = snprintf(buf, sizeof(buf), "%d", rep_len);
    if (ret < 0 || ret > sizeof(buf))
        return -1;
    appendToResHeaders(c, CONTENT_LENGTH, buf);

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

int sendFile(struct client *c, int fd, off_t len)
{
    off_t send = 0;
    int ret;
    if (fd < 0) {
        return WHEAT_WRONG;
    }
    if (len == 0) {
        if (getFileSize(fd, &len) == WHEAT_WRONG)
            return WHEAT_WRONG;
    }
    size_t unit_read = Server.max_buffer_size < WHEAT_MAX_BUFFER_SIZE ?
        Server.max_buffer_size/20 : WHEAT_MAX_BUFFER_SIZE/20;
    while (send < len) {
        int nread;
        lseek(fd, send, SEEK_SET);
        wstr ctx = wstrEmpty();
        nread = readBulkFrom(fd, &ctx, unit_read);
        if (nread <= 0)
            return WHEAT_WRONG;
        send += nread;
        ret = WorkerProcess->worker->sendData(c, ctx);
        if (ret == -1)
            return WHEAT_WRONG;
    }
    return WHEAT_OK;
}

int staticFileCall(struct client *c, void *arg)
{
    wstr path = arg;
    int file_d = open(path, O_RDONLY), ret;
    struct staticFileData *static_data = c->app_private_data;
    off_t len;
    if (file_d == -1) {
        wheatLog(WHEAT_VERBOSE, "open file failed: %s", strerror(errno));
        goto failed404;
    }
    if (isRegFile(path) == WHEAT_WRONG) {
        wheatLog(WHEAT_VERBOSE, "open file failed: %s", strerror(errno));
        goto failed404;
    }
    if (AllowExtensions && static_data->extension &&
            !dictFetchValue(AllowExtensions, static_data->extension)) {
        goto failed404;
    }
    ret = getFileSize(file_d, &len);
    if (ret == WHEAT_WRONG) {
        wheatLog(WHEAT_VERBOSE, "open file failed: %s", strerror(errno));
        goto failed404;
    }
    if (len > MaxFileSize) {
        wheatLog(WHEAT_NOTICE, "file exceed max limit %d", len);
        wheatLog(WHEAT_VERBOSE, "open file failed: %s", strerror(errno));
        goto failed404;
    }

    fillResInfo(c, 200, "OK");
    ret = fillResHeaders(c, len);
    if (ret == -1)
        goto failed404;
    ret = httpSendHeaders(c);
    if (ret == -1)
        goto failed;
    ret = sendFile(c, file_d, len);
    if (ret == WHEAT_WRONG)
        goto failed;
    if (file_d > 0) close(file_d);
    return WHEAT_OK;

failed:
    wheatLog(WHEAT_WARNING, "send file failed");

failed404:
    sendResponse404(c);
    if (file_d > 0) close(file_d);

    return WHEAT_WRONG;
}

void initStaticFile()
{
    struct configuration *conf = getConfiguration("file-maxsize");
    MaxFileSize = conf->target.val;

    conf = getConfiguration("allowed-extension");
    wstr extensions = wstrNew(conf->target.ptr);
    if (wstrIndex(extensions, '*') != -1) {
        AllowExtensions = NULL;
    } else {
        AllowExtensions = dictCreate(&wstrDictType);

        int args, i, ret;
        wstr *argvs = wstrNewSplit(extensions, ",", 1, &args);
        if (!argvs) {
            wheatLog(WHEAT_WARNING, "init Static File failed");
            return ;
        }

        for (i = 0; i < args; ++i) {
            ret = dictAdd(AllowExtensions, wstrNew(argvs[i]), wstrNew("allowed"));
            if (ret == DICT_WRONG)
                break;
        }

        wstrFreeSplit(argvs, args);
    }
    wstrFree(extensions);
}

void deallocStaticFile()
{
    if (AllowExtensions)
        dictRelease(AllowExtensions);
    MaxFileSize = 0;
}

void *initStaticFileData(struct client *c)
{
    struct staticFileData *data = malloc(sizeof(*data));
    if (!data)
        return NULL;
    memset(data, 0, sizeof(*data));
    wstr path = httpGetPath(c);
    if (path) {
        char *point = strrchr(path, '.');
        char *slash = strrchr(path, '/');
        if (point && slash && slash < point) {
            data->extension = wstrNew(point+1);
            data->filename = wstrNewLen(slash+1, (int)(point-slash-1));
        }
    }
    return data;
}

void freeStaticFileData(void *app_data)
{
    struct staticFileData *data = app_data;
    wstrFree(data->extension);
    wstrFree(data->filename);
    free(data);
}
