#!/usr/bin/env python3
"""
Read in the /etc/default/nfs-{common,kernel-server} files and
set the corresponding values in nfs.conf
"""

from __future__ import print_function
import sys
import getopt
import subprocess

CONF_NFS = '/etc/nfs.conf.d/local.conf'
CONF_TOOL = '/usr/sbin/nfsconf'

# options for nfsd found in RPCNFSDOPTS
OPTS_NFSD = 'dH:p:rR:N:V:stTuUG:L:'
LONG_NFSD = ['debug', 'host=', 'port=', 'rdma=', 'nfs-version=', 'no-nfs-version=',
             'tcp', 'no-tcp', 'udp', 'no-udp', 'grace-time=', 'lease-time=']
CONV_NFSD = {'-d': (CONF_NFS, 'nfsd', 'debug', 'all'),
             '-H': (CONF_NFS, 'nfsd', 'host', ','),
             '-p': (CONF_NFS, 'nfsd', 'port', '$1'),
             '-r': (CONF_NFS, 'nfsd', 'rdma', 'nfsrdma'),
             '-R': (CONF_NFS, 'nfsd', 'rdma', '$1'),
             '-N': (CONF_NFS, 'nfsd', 'vers$1', 'n'),
             '-V': (CONF_NFS, 'nfsd', 'vers$1', 'y'),
             '-t': (CONF_NFS, 'nfsd', 'tcp', '1'),
             '-T': (CONF_NFS, 'nfsd', 'tcp', '0'),
             '-u': (CONF_NFS, 'nfsd', 'udp', '1'),
             '-U': (CONF_NFS, 'nfsd', 'udp', '0'),
             '-G': (CONF_NFS, 'nfsd', 'grace-time', '$1'),
             '-L': (CONF_NFS, 'nfsd', 'lease-time', '$1'),
             '$1': (CONF_NFS, 'nfsd', 'threads', '$1'),
             '--debug': (CONF_NFS, 'nfsd', 'debug', 'all'),
             '--host': (CONF_NFS, 'nfsd', 'host', ','),
             '--port': (CONF_NFS, 'nfsd', 'port', '$1'),
             '--rdma': (CONF_NFS, 'nfsd', 'rdma', '$1'),
             '--no-nfs-version': (CONF_NFS, 'nfsd', 'vers$1', 'n'),
             '--nfs-version': (CONF_NFS, 'nfsd', 'vers$1', 'y'),
             '--tcp': (CONF_NFS, 'nfsd', 'tcp', '1'),
             '--no-tcp': (CONF_NFS, 'nfsd', 'tcp', '0'),
             '--udp': (CONF_NFS, 'nfsd', 'udp', '1'),
             '--no-udp': (CONF_NFS, 'nfsd', 'udp', '0'),
             '--grace-time': (CONF_NFS, 'nfsd', 'grace-time', '$1'),
             '--lease-time': (CONF_NFS, 'nfsd', 'lease-time', '$1'),
            }

# options for mountd found in RPCMOUNTDOPTS
OPTS_MOUNTD = 'go:d:H:p:N:nrus:t:V:'
LONG_MOUNTD = ['descriptors=', 'debug=', 'nfs-version=', 'no-nfs-version=',
               'port=', 'no-tcp', 'ha-callout=', 'state-directory-path=',
               'num-threads=', 'reverse-lookup', 'manage-gids', 'no-udp']

CONV_MOUNTD = {'-g': (CONF_NFS, 'mountd', 'manage-gids', '1'),
               '-o': (CONF_NFS, 'mountd', 'descriptors', '$1'),
               '-d': (CONF_NFS, 'mountd', 'debug', '$1'),
               '-H': (CONF_NFS, 'mountd', 'ha-callout', '$1'),
               '-p': (CONF_NFS, 'mountd', 'port', '$1'),
               '-N': (CONF_NFS, 'nfsd', 'vers$1', 'n'),
               '-V': (CONF_NFS, 'nfsd', 'vers$1', 'y'),
               '-n': (CONF_NFS, 'nfsd', 'tcp', '0'),
               '-s': (CONF_NFS, 'mountd', 'stat-directory-path', '$1'),
               '-t': (CONF_NFS, 'mountd', 'threads', '$1'),
               '-r': (CONF_NFS, 'mountd', 'reverse-lookup', '1'),
               '-u': (CONF_NFS, 'nfsd', 'udp', '0'),
               '--manage-gids': (CONF_NFS, 'mountd', 'manage-gids', '1'),
               '--descriptors': (CONF_NFS, 'mountd', 'descriptors', '$1'),
               '--debug': (CONF_NFS, 'mountd', 'debug', '$1'),
               '--ha-callout': (CONF_NFS, 'mountd', 'ha-callout', '$1'),
               '--port': (CONF_NFS, 'mountd', 'port', '$1'),
               '--nfs-version': (CONF_NFS, 'nfsd', 'vers$1', 'y'),
               '--no-nfs-version': (CONF_NFS, 'nfsd', 'vers$1', 'n'),
               '--no-tcp': (CONF_NFS, 'nfsd', 'tcp', '0'),
               '--state-directory-path': (CONF_NFS, 'mountd', 'state-directory-path', '$1'),
               '--num-threads': (CONF_NFS, 'mountd', 'threads', '$1'),
               '--reverse-lookup': (CONF_NFS, 'mountd', 'reverse-lookup', '1'),
               '--no-udp': (CONF_NFS, 'nfsd', 'udp', '0'),
              }
# We enable manage-gids by default.  But if /etc/default/nfs-common
# was modified then we need to set manage-gids = 0 unless we see
# --manage-gids.
INIT_MOUNTD = [(CONF_NFS, 'mountd', 'manage-gids', '0')]

# options for statd found in STATDOPTS
OPTS_STATD = 'o:p:T:U:n:P:H:'
LONG_STATD = ['outgoing-port=', 'port=', 'name=', 'state-directory-path=',
              'ha-callout=', 'nlm-port=', 'nlm-udp-port=']
CONV_STATD = {'-o': (CONF_NFS, 'statd', 'outgoing-port', '$1'),
              '-p': (CONF_NFS, 'statd', 'port', '$1'),
              '-T': (CONF_NFS, 'lockd', 'port', '$1'),
              '-U': (CONF_NFS, 'lockd', 'udp-port', '$1'),
              '-n': (CONF_NFS, 'statd', 'name', '$1'),
              '-P': (CONF_NFS, 'statd', 'state-directory-path', '$1'),
              '-H': (CONF_NFS, 'statd', 'ha-callout', '$1'),
              '--outgoing-port': (CONF_NFS, 'statd', 'outgoing-port', '$1'),
              '--port': (CONF_NFS, 'statd', 'port', '$1'),
              '--name': (CONF_NFS, 'statd', 'name', '$1'),
              '--state-directory-path': (CONF_NFS, 'statd', 'state-directory-path', '$1'),
              '--ha-callout': (CONF_NFS, 'statd', 'ha-callout', '$1'),
              '--nlm-port': (CONF_NFS, 'lockd', 'port', '$1'),
              '--nlm-udp-port': (CONF_NFS, 'lockd', 'udp-port', '$1'),
             }

# options for gssd found in RPCGSSDOPTS
OPTS_GSSD = 'Mnvrp:k:d:t:T:R:lD'
CONV_GSSD = {'-M': (CONF_NFS, 'gssd', 'use-memcache', '1'),
             '-n': (CONF_NFS, 'gssd', 'root_uses_machine_creds', '0'),
             '-v': (CONF_NFS, 'gssd', 'verbosity', '+'),
             '-r': (CONF_NFS, 'gssd', 'rpc-verbosity', '+'),
             '-p': (CONF_NFS, 'general', 'pipefs-directory', '$1'),
             '-k': (CONF_NFS, 'gssd', 'keytab-file', '$1'),
             '-d': (CONF_NFS, 'gssd', 'cred-cache-directory', '$1'),
             '-t': (CONF_NFS, 'gssd', 'context-timeout', '$1'),
             '-T': (CONF_NFS, 'gssd', 'rpc-timeout', '$1'),
             '-R': (CONF_NFS, 'gssd', 'preferred-realm', '$1'),
             '-l': (CONF_NFS, 'gssd', 'limit-to-legacy-enctypes', '0'),
             '-D': (CONF_NFS, 'gssd', 'avoid-dns', '0'),
            }

# options for svcgssd found in RPCSVCGSSDOPTS
OPTS_SVCGSSD = 'ivrnp:'
CONV_SVCGSSD = {'-i': (CONF_NFS, 'svcgssd', 'idmap-verbosity', '+'),
                '-v': (CONF_NFS, 'svcgssd', 'verbosity', '+'),
                '-r': (CONF_NFS, 'svcgssd', 'rpc-verbosity', '+'),
                '-n': (CONF_NFS, 'svcgssd', 'principal', 'system'),
                '-p': (CONF_NFS, 'svcgssd', 'principal', '$1'),
               }

# meta list of all the getopt lists
GETOPT_MAPS = [('RPCNFSDOPTS', OPTS_NFSD, LONG_NFSD, CONV_NFSD, []),
               ('RPCMOUNTDOPTS', OPTS_MOUNTD, LONG_MOUNTD, CONV_MOUNTD, INIT_MOUNTD),
               ('STATDOPTS', OPTS_STATD, LONG_STATD, CONV_STATD, []),
               ('RPCGSSDOPTS', OPTS_GSSD, [], CONV_GSSD, []),
               ('RPCSVCGSSDOPTS', OPTS_SVCGSSD, [], CONV_SVCGSSD, []),
              ]

# map for all of the single option values
VALUE_MAPS = {'RPCNFSDCOUNT': (CONF_NFS, 'nfsd', 'threads', '$1'),
             }

def eprint(*args, **kwargs):
    """ Print error to stderr """
    print(*args, file=sys.stderr, **kwargs)

def makesub(param, value):
    """ Variable substitution """
    return param.replace('$1', value)

def set_value(value, entry):
    """ Set a configuration value by running nfsconf tool"""
    cfile, section, tag, param = entry

    tag = makesub(tag, value)
    param = makesub(param, value)
    if param == '+':
        param = value
    if param == ',':
        param = value
    args = [CONF_TOOL, "--file", cfile, "--set", section, tag, param]

    try:
        subprocess.check_output(args, stderr=subprocess.STDOUT)
    except subprocess.CalledProcessError as e:
        print("Error running nfs-conf tool:\n %s" % (e.output.decode()))
        print("Args: %s\n" % args)
        raise Exception

def convert_getopt(optname, options, optstring, longopts, conversions, init):
    """ Parse option string into seperate config items

        Take a getopt string and a table of conversions
        parse it all and spit out the converted config

        Keyword arguments:
        options -- the argv string to convert
        optstring --  getopt format option list
        conversions -- table of translations
    """
    optcount = 0
    try:
        args = options.strip('\"').split()
        optlist, optargs = getopt.gnu_getopt(args, optstring, longopts=longopts)
    except getopt.GetoptError as err:
        eprint(err)
        raise Exception

    for c in init:
        set_value('', c)

    setlist = {}
    for (k, v) in optlist:
        if k in conversions:
            # it's already been set once
            param = conversions[k][3]
            tag = k + makesub(conversions[k][2], v)
            if tag in setlist:
                value = setlist[tag][0]
                # is it a cummulative entry
                if param == '+':
                    value = str(int(value) + 1)
                if param == ',':
                    value += "," + v
            else:
                if param == '+':
                    value = "1"
                elif param == ',':
                    value = v
                else:
                    value = v
            setlist[tag] = (value, conversions[k])
        else:
            if v:
                eprint("Ignoring unrecognised option %s=%s in %s" % (k, v, optname))
            else:
                eprint("Ignoring unrecognised option %s in %s" % (k, optname))


    for v, c in setlist.values():
        try:
            set_value(v, c)
            optcount += 1
        except Exception:
            raise

    i = 1
    for o in optargs:
        opname = '$' + str(i)
        if opname in conversions:
            try:
                set_value(o, conversions[opname])
                optcount += 1
            except Exception:
                raise
        else:
            eprint("Unrecognised trailing arguments")
            raise Exception
        i += 1

    return optcount

def load_old_config():
    """ Load the old configuration

        Since "default" files were always meant to be parsed by a
        shell, run a shell script to read them and dump the values.
    """
    names = ([name for (name, _, _, _, _) in GETOPT_MAPS]
             + list(VALUE_MAPS.keys()))
    script = ''.join(
        [f'{name}=\n' for name in names]
        + [f'[ -r {file_name} ] && . {file_name} >/dev/null\n'
           for file_name in ['/etc/default/nfs-common',
                             '/etc/default/nfs-kernel-server']]
        + [f'printf \'{name}=%s\\n\' "${name}"\n' for name in names])
    config = {}
    with subprocess.Popen(script, shell=True,
                          stdout=subprocess.PIPE, text=True) as proc:
        for line in proc.stdout:
            name, value = line.rstrip('\n').split('=', 1)
            config[name] = value
    return config

def map_values():
    """ Main function """
    mapcount = 0

    config = load_old_config()

    # Map all the getopt option lists
    for (name, opts, lopts, conv, init) in GETOPT_MAPS:
        if name in config:
            try:
                mapcount += convert_getopt(name, config[name], opts,
                                           lopts, conv, init)
            except Exception:
                eprint("Error whilst converting %s to nfsconf options." % (name))
                raise

    # Map the single value options
    for name, opts in VALUE_MAPS.items():
        if name in config:
            try:
                value = config[name]
                set_value(value.strip('\"'), opts)
                mapcount += 1
            except Exception:
                raise

# Main routine
try:
    map_values()
except Exception as e:
    eprint(e)
    eprint("Conversion failed. Please correct the error and try again.")
    sys.exit(1)
