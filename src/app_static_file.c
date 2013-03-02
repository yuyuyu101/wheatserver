#include <fcntl.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/uio.h>
#include <sys/types.h>

#include "wheatserver.h"
#include "proto_http.h"

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
    {"bin", "application/octet-stream"},
    {"bmp", "image/bmp"},
    {"cdf", "application/x-netcdf"},
    {"class", "application/octet-stream"},
    {"cpio", "application/x-cpio"},
    {"cpt", "application/mac-compactpro"},
    {"csh", "application/x-csh"},
    {"css", "text/css"},
    {"dcr", "application/x-director"},
    {"dir", "application/x-director"},
    {"djv", "image/vnd.djvu"},
    {"djvu", "image/vnd.djvu"},
    {"dll", "application/octet-stream"},
    {"dms", "application/octet-stream"},
    {"doc", "application/msword"},
    {"dvi", "application/x-dvi"},
    {"dxr", "application/x-director"},
    {"eps", "application/postscript"},
    {"etx", "text/x-setext"},
    {"exe", "application/octet-stream"},
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
    {"lha", "application/octet-stream"},
    {"lzh", "application/octet-stream"},
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
    {"so", "application/octet-stream"},
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

static int fillResHeaders(struct client *c)
{
    char buf[256];
    int ret;
    struct httpData *http_data = c->protocol_data;
    struct staticFileData *static_data = c->app_private_data;
    int i;
    ret = snprintf(buf, 255, "%s: %d\r\n", "Content-Length",
            http_data->response_length);
    if (ret == -1)
        return ret;
    appendToListTail(http_data->res_headers, wstrNew(buf));
    if (static_data->extension && static_data->filename) {
        int size = sizeof(ContentTypes)/sizeof(struct contenttype);
        for (i = 0; i < size; ++i) {
            if (!strcmp(static_data->extension, ContentTypes[i].extension)) {
                break;
            }
        }
        if (i != size) {
            ret = snprintf(buf, 255, "%s: %s\r\n", "Content-Type",
                    ContentTypes[i].mime_type);
            if (ret == -1)
                return ret;
            appendToListTail(http_data->res_headers, wstrNew(buf));
        }
    }
    return ret;
}

int staticFileCall(struct client *c, void *arg)
{
    wstr path = arg;
    int file_d = open(path, O_RDONLY), ret;
    off_t len, send = 0;
    struct httpData *http_data = c->protocol_data;
    struct staticFileData *static_data = c->app_private_data;
    if (file_d == -1) {
        goto failed404;
    }
    if (AllowExtensions &&
            !dictFetchValue(AllowExtensions, static_data->extension)) {
        goto failed404;
    }
    ret = getFileSize(file_d, &len);
    if (ret == WHEAT_WRONG) {
        goto failed404;
    }
    if (len > MaxFileSize) {
        wheatLog(WHEAT_NOTICE, "file exceed max limit %d", len);
        goto failed404;
    }

    fillResInfo(http_data, len, 200, "OK");
    ret = fillResHeaders(c);
    if (ret == -1)
        goto failed404;
    ret = httpSendHeaders(c);
    if (ret == -1)
        goto failed;
    size_t unit_read = Server.max_buffer_size < WHEAT_MAX_BUFFER_SIZE ?
        Server.max_buffer_size/20 : WHEAT_MAX_BUFFER_SIZE/20;
    while (send < len) {
        int nread;
        lseek(file_d, send, SEEK_SET);
        if (ret == -1) {
            goto failed;
        }
        nread = readBulkFrom(file_d, &c->res_buf, unit_read);
        if (nread <= 0) {
            goto failed;
        }
        send += nread;
        ret = WorkerProcess->worker->sendData(c);
        if (ret == -1)
            goto failed;
    }
    if (wstrlen(c->res_buf))
        ret = WorkerProcess->worker->sendData(c);
    if (ret == -1)
        goto failed;

    if (file_d > 0) close(file_d);
    return WHEAT_OK;

failed404:
    sendResponse404(c);

failed:
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
    struct httpData *http_data = c->protocol_data;
    struct staticFileData *data = malloc(sizeof(*data));
    if (!data)
        return NULL;
    memset(data, 0, sizeof(*data));
    if (http_data->path) {
        char *point = strrchr(http_data->path, '.');
        char *slash = strrchr(http_data->path, '/');
        if (point && slash && slash < point) {
            data->extension = wstrNew(point+1);
            data->filename = wstrNewLen(slash+1, point-slash-1);
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
