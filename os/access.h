#ifndef _XSERVER_OS_ACCESS_H
#define _XSERVER_OS_ACCESS_H

#include "dix.h"

#define XSERV_t
#define TRANS_SERVER
#define TRANS_REOPEN
#include <X11/Xtrans/Xtransint.h>

typedef struct sockaddr *sockaddrPtr;

int AddHost(ClientPtr client, int family, unsigned length, const void *pAddr);
Bool ForEachHostInFamily(int family,
                         Bool (*func)(unsigned char *addr, short len, void *closure),
                         void *closure);
int RemoveHost(ClientPtr client, int family, unsigned length, void *pAddr);
int GetHosts(void **data, int *pnHosts, int *pLen, BOOL *pEnabled);
int InvalidHost(sockaddrPtr saddr, int len, ClientPtr client);
void AddLocalHosts(void);
void ResetHosts(const char *display);

/* register local hosts entries for outself, based on listening fd */
void DefineSelf(XtransConnInfo ci);

/* check whether given addr belongs to ourself */
void AugmentSelf(void *from, int len);

int ChangeAccessControl(ClientPtr client, int fEnabled);

void AccessUsingXdmcp(void);

extern Bool defeatAccessControl;

#endif /* _XSERVER_OS_ACCESS_H */
