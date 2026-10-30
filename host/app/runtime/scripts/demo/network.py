"""Single networking owner for the frozen dedicated eth0 board link."""
INTERFACES = '''auto lo
iface lo inet loopback

auto eth0
iface eth0 inet static
    address 192.168.13.10
    netmask 255.255.255.0
'''
INIT = '''#!/bin/sh
export PATH=/usr/sbin:/usr/bin:/sbin:/bin
case "$1" in
start)
    ip link set dev eth0 up || exit $?
    ip addr replace 192.168.13.10/24 dev eth0 || exit $?
    ;;
stop) : ;;
*) exit 0 ;;
esac
'''
APPEND = '''FILESEXTRAPATHS_prepend := "${THISDIR}/files:"
SRC_URI += " file://interfaces"
'''

def install_direct_network(meta):
    from pathlib import Path
    meta=Path(meta)
    for name,content in [('recipes-core/init-ifupdown/files/interfaces',INTERFACES),('recipes-core/init-ifupdown/init-ifupdown_%.bbappend',APPEND),('recipes-apps/codex-firstboot/files/codex-direct-network',INIT)]:
        p=meta/name;p.parent.mkdir(parents=True,exist_ok=True);p.write_bytes(content.encode('ascii'))

def network_errors(files):
    import re
    errors=[]
    raw=files.get('etc/network/interfaces',b'')
    try:lines=[x.split('#',1)[0].split() for x in raw.decode('ascii').splitlines()]
    except UnicodeError:return ['non-ASCII network configuration']
    declarations=[x for x in lines if len(x)>=4 and x[:3]==['iface','eth0','inet']]
    if declarations!=[['iface','eth0','inet','static']]:errors.append('eth0 must have exactly one static IPv4 declaration')
    required=[['auto','lo'],['iface','lo','inet','loopback'],['auto','eth0'],['address','192.168.13.10'],['netmask','255.255.255.0']]
    if any(x not in lines for x in required):errors.append('missing frozen dedicated-link network fields')
    if re.search(rb'\b(?:udhcpc|dhclient)\b',raw):errors.append('unexpected DHCP command')
    if files.get('etc/init.d/codex-direct-network')!=INIT.encode('ascii'):errors.append('direct network init differs from audited configuration')
    return errors
