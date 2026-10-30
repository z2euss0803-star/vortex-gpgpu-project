"""Reject guessed kernel config symbols; validate against supplied actual Kconfig text."""
import re
def symbols(text):
    return set(re.findall(r'^\s*(?:menuconfig|config)\s+(\w+)\s*$',text,re.M))
def validate(fragment,kconfigs):
    configured=set(re.findall(r'^CONFIG_(\w+)=y$',fragment,re.M))
    unknown=configured-symbols(kconfigs)
    if unknown:raise ValueError('UNKNOWN_KCONFIG_SYMBOLS:'+','.join(sorted(unknown)))
    return sorted(configured)
