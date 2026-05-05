#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <alloca.h>
#include <errno.h>
#include <pwd.h>
#include <stdlib.h>
#include <unistd.h>
#include <grp.h>

#include "exportfs.h"
#include "nfs_ucred.h"

#include "xlog.h"

void nfs_ucred_squash_groups(struct nfs_ucred *cred, const struct exportent *ep)
{
	int i;

	if (!(ep->e_flags & NFSEXP_ROOTSQUASH))
		return;
	if (cred->gid == 0)
		cred->gid = ep->e_anongid;
	for (i = 0; i < cred->ngroups; i++) {
		if (cred->groups[i] == 0)
			cred->groups[i] = ep->e_anongid;
	}
}

static int nfs_ucred_init_effective(struct nfs_ucred *cred)
{
	int ngroups = getgroups(0, NULL);

	if (ngroups > 0) {
		size_t sz = ngroups * sizeof(gid_t);
		gid_t *groups = malloc(sz);
		if (groups == NULL)
			return ENOMEM;
		if (getgroups(ngroups, groups) == -1) {
			free(groups);
			return errno;
		}
		nfs_ucred_init_groups(cred, groups, ngroups);
	} else
		nfs_ucred_init_groups(cred, NULL, 0);
	cred->uid = geteuid();
	cred->gid = getegid();
	return 0;
}

static size_t nfs_ucred_getpw_r_size_max(void)
{
	long buflen = sysconf(_SC_GETPW_R_SIZE_MAX);

	if (buflen == -1)
		return 16384;
	return buflen;
}

int nfs_ucred_reload_groups(struct nfs_ucred *cred, const struct exportent *ep)
{
	struct passwd pwd, *pw;
	uid_t uid = cred->uid;
	gid_t gid = cred->gid;
	size_t buflen;
	char *buf;
	int ngroups = 0;
	int ret;

	if (ep->e_flags & (NFSEXP_ALLSQUASH | NFSEXP_ROOTSQUASH) &&
	    (int)uid == ep->e_anonuid)
		return 0;
	buflen = nfs_ucred_getpw_r_size_max();
	buf = alloca(buflen);
	ret = getpwuid_r(uid, &pwd, buf, buflen, &pw);
	if (ret != 0)
		return ret;
	if (!pw)
		return ENOENT;
	if (getgrouplist(pw->pw_name, gid, NULL, &ngroups) == -1 &&
	    ngroups > 0) {
		gid_t *groups = malloc(ngroups * sizeof(groups[0]));
		if (groups == NULL)
			return ENOMEM;
		if (getgrouplist(pw->pw_name, gid, groups, &ngroups) == -1) {
			free(groups);
			return ENOMEM;
		}
		free(cred->groups);
		nfs_ucred_init_groups(cred, groups, ngroups);
		nfs_ucred_squash_groups(cred, ep);
	} else
		nfs_ucred_free_groups(cred);
	return 0;
}

static int nfs_ucred_set_effective(const struct nfs_ucred *cred,
				   const struct nfs_ucred *saved)
{
	uid_t suid = saved ? saved->uid : geteuid();
	gid_t sgid = saved ? saved->gid : getegid();
	int ret;

	/* Start with a privileged effective user */
	if (setresuid(-1, 0, -1) < 0) {
		xlog(L_WARNING, "can't change privileged user %u-%u. %s",
		     geteuid(), getegid(), strerror(errno));
		return errno;
	}

	if (setgroups(cred->ngroups, cred->groups) == -1) {
		xlog(L_WARNING, "can't change groups for user %u-%u. %s",
		     geteuid(), getegid(), strerror(errno));
		return errno;
	}
	if (setresgid(-1, cred->gid, sgid) == -1) {
		xlog(L_WARNING, "can't change gid for user %u-%u. %s",
		     geteuid(), getegid(), strerror(errno));
		ret = errno;
		goto restore_groups;
	}
	if (setresuid(-1, cred->uid, suid) == -1) {
		xlog(L_WARNING, "can't change uid for user %u-%u. %s",
		     geteuid(), getegid(), strerror(errno));
		ret = errno;
		goto restore_gid;
	}
	return 0;
restore_gid:
	if (setresgid(-1, sgid, -1) < 0) {
		xlog(L_WARNING, "can't restore privileged user %u-%u. %s",
		     geteuid(), getegid(), strerror(errno));
	}
restore_groups:
	if (saved)
		setgroups(saved->ngroups, saved->groups);
	else
		setgroups(0, NULL);
	return ret;
}

int nfs_ucred_swap_effective(const struct nfs_ucred *cred,
			     struct nfs_ucred **savedp)
{
	struct nfs_ucred *saved = malloc(sizeof(*saved));
	int ret;

	if (saved == NULL)
		return ENOMEM;
	ret = nfs_ucred_init_effective(saved);
	if (ret != 0) {
		free(saved);
		return ret;
	}
	ret = nfs_ucred_set_effective(cred, saved);
	if (savedp == NULL || ret != 0)
		nfs_ucred_free(saved);
	else
		*savedp = saved;
	return ret;
}
