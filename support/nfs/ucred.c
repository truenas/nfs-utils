#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <errno.h>
#include <stdlib.h>
#include <unistd.h>
#include <rpc/rpc.h>

#include "exportfs.h"
#include "nfs_ucred.h"

#ifdef HAVE_TIRPC_GSS_GETCRED
#include <rpc/rpcsec_gss.h>
#endif /* HAVE_TIRPC_GSS_GETCRED */
#ifdef HAVE_TIRPC_AUTHDES_GETUCRED
#include <rpc/auth_des.h>
#endif /* HAVE_TIRPC_AUTHDES_GETUCRED */

static int nfs_ucred_copy_cred(struct nfs_ucred *cred, uid_t uid, gid_t gid,
			       const gid_t *groups, int ngroups)
{
	if (ngroups > 0) {
		size_t sz = ngroups * sizeof(groups[0]);
		cred->groups = malloc(sz);
		if (cred->groups == NULL)
			return ENOMEM;
		cred->ngroups = ngroups;
		memcpy(cred->groups, groups, sz);
	} else
		nfs_ucred_init_groups(cred, NULL, 0);
	cred->uid = uid;
	cred->gid = gid;
	return 0;
}

static int nfs_ucred_init_cred_squashed(struct nfs_ucred *cred,
					const struct exportent *ep)
{
	cred->uid = ep->e_anonuid;
	cred->gid = ep->e_anongid;
	nfs_ucred_init_groups(cred, NULL, 0);
	return 0;
}

static int nfs_ucred_init_cred(struct nfs_ucred *cred, uid_t uid, gid_t gid,
			       const gid_t *groups, int ngroups,
			       const struct exportent *ep)
{
	if (ep->e_flags & NFSEXP_ALLSQUASH) {
		nfs_ucred_init_cred_squashed(cred, ep);
	} else if (ep->e_flags & NFSEXP_ROOTSQUASH && uid == 0) {
		nfs_ucred_init_cred_squashed(cred, ep);
		if (gid != 0)
			cred->gid = gid;
	} else {
		int ret = nfs_ucred_copy_cred(cred, uid, gid, groups, ngroups);
		if (ret != 0)
			return ret;
		nfs_ucred_squash_groups(cred, ep);
	}
	return 0;
}

static int nfs_ucred_init_null(struct nfs_ucred *cred,
			       const struct exportent *ep)
{
	return nfs_ucred_init_cred_squashed(cred, ep);
}

static int nfs_ucred_init_unix(struct nfs_ucred *cred, struct svc_req *rqst,
			       const struct exportent *ep)
{
	struct authunix_parms *aup;

	aup = (struct authunix_parms *)rqst->rq_clntcred;
	return nfs_ucred_init_cred(cred, aup->aup_uid, aup->aup_gid,
				   aup->aup_gids, aup->aup_len, ep);
}

#ifdef HAVE_TIRPC_GSS_GETCRED
static int nfs_ucred_init_gss(struct nfs_ucred *cred, struct svc_req *rqst,
			      const struct exportent *ep)
{
	rpc_gss_ucred_t *gss_ucred = NULL;

	if (!rpc_gss_getcred(rqst, NULL, &gss_ucred, NULL) || gss_ucred == NULL)
		return EINVAL;
	return nfs_ucred_init_cred(cred, gss_ucred->uid, gss_ucred->gid,
				   gss_ucred->gidlist, gss_ucred->gidlen, ep);
}
#endif /* HAVE_TIRPC_GSS_GETCRED */

#ifdef HAVE_TIRPC_AUTHDES_GETUCRED
int authdes_getucred(struct authdes_cred *adc, uid_t *uid, gid_t *gid,
		     int *grouplen, gid_t *groups);

static int nfs_ucred_init_des(struct nfs_ucred *cred, struct svc_req *rqst,
			      const struct exportent *ep)
{
	struct authdes_cred *des_cred;
	uid_t uid;
	gid_t gid;
	int grouplen;
	gid_t groups[NGROUPS];

	des_cred = (struct authdes_cred *)rqst->rq_clntcred;
	if (!authdes_getucred(des_cred, &uid, &gid, &grouplen, &groups[0]))
		return EINVAL;
	return nfs_ucred_init_cred(cred, uid, gid, groups, grouplen, ep);
}
#endif /* HAVE_TIRPC_AUTHDES_GETUCRED */

int nfs_ucred_get(struct nfs_ucred **credp, struct svc_req *rqst,
		  const struct exportent *ep)
{
	struct nfs_ucred *cred = malloc(sizeof(*cred));
	int ret;

	*credp = NULL;
	if (cred == NULL)
		return ENOMEM;
	switch (rqst->rq_cred.oa_flavor) {
	case AUTH_UNIX:
		ret = nfs_ucred_init_unix(cred, rqst, ep);
		break;
#ifdef HAVE_TIRPC_GSS_GETCRED
	case RPCSEC_GSS:
		ret = nfs_ucred_init_gss(cred, rqst, ep);
		break;
#endif /* HAVE_TIRPC_GSS_GETCRED */
#ifdef HAVE_TIRPC_AUTHDES_GETUCRED
	case AUTH_DES:
		ret = nfs_ucred_init_des(cred, rqst, ep);
		break;
#endif /* HAVE_TIRPC_AUTHDES_GETUCRED */
	default:
		ret = nfs_ucred_init_null(cred, ep);
		break;
	}
	if (ret == 0) {
		*credp = cred;
		return 0;
	}
	free(cred);
	return ret;
}
