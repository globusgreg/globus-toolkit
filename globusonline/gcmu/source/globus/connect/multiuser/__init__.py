# Copyright 2012-2013 University of Chicago
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#     http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.

import pkgutil
import platform
import re
import socket
import sys
import urllib

__path__ = pkgutil.extend_path(__path__, __name__)

def to_unicode(data):
    """
    Coerce any type to unicode, assuming utf-8 encoding for strings.
    """
    if isinstance(data, unicode):
        return data
    if isinstance(data, str):
        return unicode(data, "utf-8")
    else:
        return unicode(data)

def is_ec2():
    url = 'http://169.254.169.254/latest/meta-data/'
    value = None
    try:
        socket.setdefaulttimeout(3.0)
        value = urllib.urlopen(url).read()
    except IOError:
        pass

    if value is not None and "404 - Not Found" in value:
        value = None

    return value is not None

def public_name():
    """
    Try to guess the public host name of this machine. If this is
    on a machine which is able to access ec2 metadata, it will use
    that; otherwise platform.node()
    """
    url = 'http://169.254.169.254/latest/meta-data/public-hostname'
    value = None
    try:
        socket.setdefaulttimeout(3.0)
        value = urllib.urlopen(url).read()
    except IOError:
        pass

    if value is not None and "404 - Not Found" in value:
        value = None

    if value is None:
        value = platform.node()
    return value

def public_ip():
    """
    Try to guess the public IP address of this machine. If this is
    on a machine which is able to access ec2 metadata, it will use
    that; otherwise it will return None
    """
    url = 'http://169.254.169.254/latest/meta-data/public-ipv4'

    value = None
    try:
        socket.setdefaulttimeout(3.0)
        value = urllib.urlopen(url).read()
    except IOError:
        pass

    if value is not None and "404 - Not Found" in value:
        value = None

    return value

def is_private_ip(name):
    """
    Determine if a host name resolves to an ip address in a private
    block.
    """
    if re.match("^(?:(?:25[0-5]|2[0-4][0-9]|[01]?[0-9][0-9]?)\.){3}(?:25[0-5]|2[0-4][0-9]|[01]?[0-9][0-9]?)$", name):
        addr = name
    else:
        addr = socket.gethostbyname(name)
    octets = [int(x) for x in addr.split(".")]
    return (octets[0] == 10 or \
            (octets[0] == 172 and octets[1] >= 16 and octets[1] <= 31) or \
            (octets[0] == 192 and octets[1] == 168) or \
            (octets[0] == 127))

def is_local_service(name):
    """
    Determine if a service definition describes a service running on
    the local node. This is true if the service URL is for localhost,
    matches the machine's name, or ec2 public name
    """
    if name is None:
        return False
    if "://" in name:
        url = urlparse.urlparse(name)
        if ":" in url.netloc:
            name = url.netloc.split(":")[0]
        else:
            name = url.netloc
    elif ":" in name:
        name = name.split(":")[0]

    if name == "localhost":
        return True

    if '.' in name:
        name = name.split('.')[0]
    node = platform.node()
    if '.' in node:
        node = node.split('.')[0]

    if name == node:
        return True
    pn = public_name()
    if pn is not None and pn.split(".")[0] == name:
        return True
    return False
# vim: filetype=python:
