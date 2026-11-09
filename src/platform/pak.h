#ifndef TB_PAK_H
#define TB_PAK_H
#include <stdbool.h>
bool PakOpen(void);
void PakClose(void);
bool PakLoad(const char *name, unsigned char **out, unsigned int *outSize);
int PakCount(void);
const char *PakNameAt(int i);
const char *PakGetPath(void);
#endif
