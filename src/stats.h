#ifndef WHEATSERVER_STATS_H_
#define WHEATSERVER_STATS_H_

#define WHEAT_STATCOMMAND_PACKET_FIELD       9

#define ONLY_MASTER                          (1)

#define getStatValByName(name)               (getStatItemByName(name)->val)

enum statType {
    SUM_STAT,
    MAX_STAT,
    ASSIGN_STAT,
};

enum statPrintFormat {
    RAW,
    MICORSECONDS_TIME,
    LOCAL_TIME,
};

struct statItem {
    int id;
    char *name;
    enum statType type;
    enum statPrintFormat format;
    int flags;
    long long val;
};

struct masterClient;
struct workerProcess;

struct statItem *copyStatItems();
struct statItem *getStatItemByName(const char *name);
void sendStatPacket(struct workerProcess *worker_process);
void logStat();
void statinputCommand(struct masterClient *c);
void statCommand(struct masterClient *c);

#endif
