#!/bin/sh
# Create /run/sysconfig/nfs-utils from NFS' /etc/default/ files, for
# nfs-config.service

nfs_config=/etc/sysconfig/nfs
[ -r /etc/default/nfs-common ] && . /etc/default/nfs-common
[ -r /etc/default/nfs-kernel-server ] && . /etc/default/nfs-kernel-server

mkdir -p /run/sysconfig
{
echo PIPEFS_MOUNTPOINT=/run/rpc_pipefs
echo RPCNFSDARGS=\"$RPCNFSDOPTS ${RPCNFSDCOUNT:-8}\"
echo RPCMOUNTDARGS=\"$RPCMOUNTDOPTS\"
echo STATDARGS=\"$STATDOPTS\"
echo RPCSVCGSSDARGS=\"$RPCSVCGSSDOPTS\"
} > /run/sysconfig/nfs-utils

# the following are supported by the systemd units, but not exposed in default files
# echo SMNOTIFYARGS=\"$SMNOTIFYARGS\"
# echo RPCIDMAPDARGS=\"$RPCIDMAPDARGS\"
# echo RPCGSSDARGS=\"$RPCGSSDARGS\"
# echo BLKMAPDARGS=\"$BLKMAPDARGS\"
# echo GSS_USE_PROXY=\"$GSS_USE_PROXY\"
