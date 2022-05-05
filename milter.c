#include "libmilter/mfapi.h"
#include "srs2.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sysexits.h>
#include <syslog.h>
#include <unistd.h>

struct mlfiPriv
{
    char *mlfi_mailfrom;
};

#define MLFIPRIV ((struct mlfiPriv *)smfi_getpriv(ctx))

/* srs rewriting parameters */
srs_t *srs = NULL;
char *domain = NULL;
const char **excludes = NULL;

sfsistat mlfi_connect(SMFICTX *ctx, char *hostname, _SOCK_ADDR *hostaddr)
{
    struct mlfiPriv *priv;

    /* allocate some private memory */
    priv = malloc(sizeof *priv);
    if (priv == NULL)
    {
        /* can't accept this message right now */
        return SMFIS_TEMPFAIL;
    }
    memset(priv, '\0', sizeof *priv);

    /* save the private data */
    smfi_setpriv(ctx, priv);

    /* continue processing */
    return SMFIS_CONTINUE;
}

sfsistat mlfi_envfrom(SMFICTX *ctx, char **argv)
{
    struct mlfiPriv *priv = MLFIPRIV;

    /* capture the envelope sender */
    char *mailaddr = smfi_getsymval(ctx, "{mail_addr}");
    if (mailaddr == NULL)
        mailaddr = "";
    if ((priv->mlfi_mailfrom = strdup(mailaddr)) == NULL)
    {
        return SMFIS_TEMPFAIL;
    }

    /* continue processing */
    return SMFIS_CONTINUE;
}

/**
 * Arguments:
 *    srs:       SRS configuration object
 *    address:   address to be rewritten
 *    value:     pre-allocated buffer for rewritten address
 *    valuelen:  length of pre-allocated buffer
 *    domain:    local domain name for SRS-rewritten addresses
 *    excludes:  list of domains not subject to SRS rewriting
 *
 * Returns: 0 if address is not rewritten,
 *          1 if address is rewritten
 */
int milter_forward(srs_t *srs, const char *address, char *value, int valuelen,
                   const char *domain, const char **excludes)
{
    int result;
    size_t addrlen;

    addrlen = strlen(address);
    for (; *excludes; excludes++)
    {
        size_t len;
        len = strlen(*excludes);
        if (len >= addrlen)
            continue;
        if (strcasecmp(*excludes, &address[addrlen - len]) == 0
            && (**excludes == '.' || address[addrlen - len - 1] == '@'))
        {
            syslog(LOG_MAIL | LOG_INFO,
                   "srs_forward: <%s> not rewritten: Domain excluded by policy",
                   address);
            return 0;
        }
    }
    if (srs_reverse(srs, value, valuelen, address) == SRS_SUCCESS)
    {
        syslog(LOG_MAIL | LOG_NOTICE,
               "srs_forward: <%s> not rewritten: Valid SRS address for <%s>",
               address, value);
        return 0;
    }
    result = srs_forward(srs, value, valuelen, address, domain);
    if (result == SRS_SUCCESS)
    {
        if (strcmp(address, value) != 0)
            syslog(LOG_MAIL | LOG_INFO, "srs_forward: <%s> rewritten as <%s>",
                   address, value);
        return 1;
    }
    else
    {
        if (result != SRS_ENOTREWRITTEN)
            syslog(LOG_MAIL | LOG_INFO, "srs_forward: <%s> not rewritten: %s",
                   address, srs_strerror(result));
    }
    return 0;
}

sfsistat mlfi_eom(SMFICTX *ctx)
{
    struct mlfiPriv *priv = MLFIPRIV;
    char newaddr[1024];

    if (priv->mlfi_mailfrom == NULL)
        return SMFIS_CONTINUE;

    /* Look up forward substitution */
    if (milter_forward(srs, priv->mlfi_mailfrom, newaddr, sizeof(newaddr),
                       domain, excludes))
    {
        /* Modify envelope sender for SRS forward processing */
        /* TODO: Do we need to copy forward ESMTP arguments or are they
         * preserved? */
        if (smfi_chgfrom(ctx, newaddr, NULL) != MI_SUCCESS)
        {
            syslog(LOG_MAIL | LOG_INFO,
                   "srs_forward: Failed to change envelope from <%s>",
                   priv->mlfi_mailfrom);
            return SMFIS_TEMPFAIL;
        }
    }
    return SMFIS_CONTINUE;
}

sfsistat mlfi_close(SMFICTX *ctx)
{
    struct mlfiPriv *priv = MLFIPRIV;

    if (priv == NULL)
        return SMFIS_CONTINUE;
    if (priv->mlfi_mailfrom != NULL)
        free(priv->mlfi_mailfrom);
    free(priv);
    smfi_setpriv(ctx, NULL);
    return SMFIS_CONTINUE;
}

struct smfiDesc smfilter = {
    "PostSRS",    /* filter name */
    SMFI_VERSION, /* version code -- do not change */
    SMFIF_CHGFROM,
    /* flags */
    mlfi_connect, /* connection info filter */
    NULL,         /* SMTP HELO command filter */
    mlfi_envfrom, /* envelope sender filter */
    NULL,         /* envelope recipient filter */
    NULL,         /* header filter */
    NULL,         /* end of header */
    NULL,         /* body block filter */
    mlfi_eom,     /* end of message */
    NULL,         /* message aborted */
    mlfi_close,   /* connection cleanup */
    NULL,         /* unknown SMTP commands */
    NULL,         /* DATA command */
    NULL          /* Once, at the start of each SMTP connection */
};

int milter_main(char *oconn, srs_t *srs_in, char *domain_in,
                const char **excludes_in)
{
    /* Copy in pointers to SRS data; will not change during operation */
    srs = srs_in;
    domain = domain_in;
    excludes = excludes_in;

    if (smfi_setconn(oconn) == MI_FAILURE)
    {
        (void)fprintf(stderr, "smfi_setconn failed\n");
        exit(EX_SOFTWARE);
    }

    /*
    **  If we're using a local socket, make sure it
    **  doesn't already exist.  Don't ever run this
    **  code as root!!
    */
    if (strncasecmp(oconn, "unix:", 5) == 0)
        unlink(oconn + 5);
    else if (strncasecmp(oconn, "local:", 6) == 0)
        unlink(oconn + 6);

    /* Register milter */
    if (smfi_register(smfilter) == MI_FAILURE)
    {
        fprintf(stderr, "smfi_register failed\n");
        exit(EX_UNAVAILABLE);
    }
    return smfi_main();
}
