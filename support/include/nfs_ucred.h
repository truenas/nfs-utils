#ifndef _NFS_UCRED_H
#define _NFS_UCRED_H

#include <sys/types.h>

struct nfs_ucred {
	uid_t uid;
	gid_t gid;
	int ngroups;
	gid_t *groups;
};

struct svc_req;
struct exportent;

int nfs_ucred_get(struct nfs_ucred **credp, struct svc_req *rqst,
		  const struct exportent *ep);

void nfs_ucred_squash_groups(struct nfs_ucred *cred,
			     const struct exportent *ep);
int nfs_ucred_reload_groups(struct nfs_ucred *cred, const struct exportent *ep);
int nfs_ucred_swap_effective(const struct nfs_ucred *cred,
			     struct nfs_ucred **savedp);

static inline void nfs_ucred_free(struct nfs_ucred *cred)
{
	free(cred->groups);
	free(cred);
}

static inline void nfs_ucred_init_groups(struct nfs_ucred *cred, gid_t *groups,
					 int ngroups)
{
	cred->groups = groups;
	cred->ngroups = ngroups;
}

static inline void nfs_ucred_free_groups(struct nfs_ucred *cred)
{
	free(cred->groups);
	nfs_ucred_init_groups(cred, NULL, 0);
}

#endif /* _NFS_UCRED_H */
