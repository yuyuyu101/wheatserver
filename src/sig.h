#ifndef WHEATSEVER_SIGNAL_H
#define WHEATSEVER_SIGNAL_H

void initMasterSignals();
void initWorkerSignals();
void signalProc(int signal);
void signalGenericHandle(int sig);

/* Master Handle Signal Function */
void handleChld();
void handleHup();
void handleQuit();
void handleInt();
void handleTerm();
void handleTtin();
void handleTtou();
void handleUsr1();
void handleUsr2();
void handleWinch();
void handleSegv();

/* Worker Handle Signal Function */
void handleWorkerUsr1(int);
void handleWorkerAbort(int);
void handleWorkerQuit(int);
void handleWorkerAlrm(int);

#endif
