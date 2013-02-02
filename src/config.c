#include "wheatserver.h"

static struct enumIdName Verbose[] = {
    {WHEAT_DEBUG, "DEBUG"}, {WHEAT_VERBOSE, "VERBOSE"},
    {WHEAT_NOTICE, "NOTICE"}, {WHEAT_WARNING, "WARNING"},
    {-1, NULL}
};

struct configuration configTable[] = {
    //  name   |    args| validator     |      default       |         helper |  flags

    // Master Configuration
    {"bind-addr",     2, stringValidator,      {.ptr=NULL},     NULL,     STRING_FORMAT},
    {"port",          2, unsignedIntValidator, {.val=WHEAT_SERVERPORT},    NULL,     INT_FORMAT},
    {"worker-number", 2, unsignedIntValidator, {.val=2},    NULL,     INT_FORMAT},
    {"logfile",       2, stringValidator,      {.ptr=NULL},  NULL,     STRING_FORMAT},
    {"logfile-level", 2, enumValidator,        {.enum_ptr=&Verbose[0]},  &Verbose[0], ENUM_FORMAT},

    // WSGI Configuration
    {"app-module-path",      2, stringValidator,      {.ptr=NULL},  NULL,     STRING_FORMAT},
    {"app-module-name",      2, stringValidator,      {.ptr=NULL},  NULL,     STRING_FORMAT},
    {"app-name",      2, stringValidator,      {.ptr=NULL},  NULL,     STRING_FORMAT},
};

void fillServerConfig()
{
    struct configuration *conf = &configTable[0];

    Server.bind_addr = conf->target.ptr;
    conf++;
    Server.port = conf->target.val;
    conf++;
    Server.worker_number = conf->target.val;
    conf++;
    Server.logfile = conf->target.ptr;
    conf++;
    Server.verbose = conf->target.enum_ptr->id;
}

struct configuration *getConfiguration(const char *name)
{
    int len = sizeof(configTable) / sizeof(struct configuration);
    int i;
    for (i = 0; i < len; i++) {
        if (!strncmp(name, configTable[i].name, strlen(name))) {
            return &configTable[i];
        }
    }
    return NULL;
}

void applyConfig(wstr config)
{
    int count, args;
    wstr *lines = wstrNewSplit(config, "\n", 1, &count);
    char *err;
    struct configuration *conf;

    int i;
    for (i = 0; i < count; i++) {
        wstr line = wstrStrip(lines[i], "\t\n\r ");
        if (line[0] == '#' || line[0] == '\0')
            continue;

        wstr *argvs = wstrNewSplit(line, " ", 1, &args);

        if ((conf = getConfiguration(argvs[0])) == NULL) {
            err = "Unknown configuration name";
            goto loaderr;
        }
        if (args != conf->args) {
            err = "Incorrect args";
            goto loaderr;
        }
        if (conf->validator(conf, argvs[0], argvs[1]) != VALIDATE_OK) {
            err = "Validate Failed";
            goto loaderr;
        }

        wstrFreeSplit(argvs, args);
    }
    wstrFreeSplit(lines, count);
    fillServerConfig();
    return ;

loaderr:
    fprintf(stderr, "\n*** FATAL CONFIG FILE ERROR ***\n");
    fprintf(stderr, "Reading the configuration file, at line %d\n", i+1);
    fprintf(stderr, ">>> '%s'\n", lines[i]);
    fprintf(stderr, "%s\n", err);
    halt(1);
}

void loadConfigFile(const char *filename, char *options)
{
    wstr config = wstrEmpty();
    char line[WHEATSERVER_CONFIGLINE_MAX+1];
    if (filename[0] != '\0') {
        FILE *fp;
        if ((fp = fopen(filename, "r")) == NULL) {
            wheatLog(WHEAT_WARNING,
                "Fatal error, can't open config file '%s'", filename);
            halt(1);
        }
        while(fgets(line, WHEATSERVER_CONFIGLINE_MAX+1, fp) != NULL)
            config = wstrCat(config, line);
        if (fp != stdin)
            fclose(fp);
    }
    if (options) {
        config = wstrCat(config,"\n");
        config = wstrCat(config, options);
    }

    applyConfig(config);
    wstrFree(config);
}

void statConfigByName(const char *n, char *result, int len)
{
    wstr name = wstrNew(n);

    if (!wstrCmpChars(name, "port", 4)) {
        snprintf(result, len, "%d", Server.port);
    } else if (!wstrCmpChars(name, "bind-addr", 9)) {
        snprintf(result, len, "%s", Server.bind_addr);
    } else if (!wstrCmpChars(name, "logfile", 7)) {
        snprintf(result, len, "%s", Server.logfile);
    } else if (!wstrCmpChars(name, "logfile-level", 13)) {
        if (Server.verbose == 0)
            snprintf(result, len, "DEBUG");
        else if (Server.verbose == 1)
            snprintf(result, len, "VERBOSE");
        else if (Server.verbose == 2)
            snprintf(result, len, "NOTICE");
        else if (Server.verbose == 3)
            snprintf(result, len, "WARNING");
        else
            snprintf(result, len, "UNKNOWN");
    } else
        snprintf(result, len, "UNKNOWN");
    wstrFree(name);
}

void printServerConfig()
{
    wheatLog(WHEAT_DEBUG, "---- Now Configuration are ----");
    int size = sizeof(configTable) / sizeof(struct configuration);
    int i;
    struct configuration *conf = &configTable[0];
    for (i = 0; i < size; i++) {
        if (conf->format == STRING_FORMAT)
            wheatLog(WHEAT_DEBUG, "%s: %s", conf->name, conf->target.ptr);
        else if (conf->format == INT_FORMAT)
            wheatLog(WHEAT_DEBUG, "%s: %d", conf->name, conf->target.val);
        else if (conf->format == ENUM_FORMAT) {
            wheatLog(WHEAT_DEBUG, "%s: %s", conf->name, conf->target.enum_ptr->name);
        }
        conf++;
    }
}
