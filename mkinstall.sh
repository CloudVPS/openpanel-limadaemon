#!/bin/sh
# This file is part of OpenPanel - The Open Source Control Panel
# OpenPanel is free software: you can redistribute it and/or modify it 
# under the terms of the GNU General Public License as published by the Free 
# Software Foundation, using version 3 of the License.
#
# Please note that use of the OpenPanel trademark may be subject to additional 
# restrictions. For more information, please visit the Legal Information 
# section of the OpenPanel website on http://www.openpanel.com/

id lima >/dev/null 2>&1 || {
  useradd -r lima
}
if [ ! -d /var/lima ]; then
  mkdir -p -m 0660 /var/lima
  mkdir /var/lima/bin
  mkdir /var/lima/lists
  mkdir /var/lima/queue
  mkdir /var/lima/queue/in
  mkdir /var/lima/queue/moderation
  mkdir /var/lima/queue/out
  mkdir /var/lima/templates
  chown -R lima /var/lima
  chgrp -R lima /var/lima
fi
cp -r limadaemon.app /var/lima/bin/
cp limacreate /var/lima/bin/
cp limaremove /var/lima/bin/
cp -r limasubmit/limasubmit.app /var/lima/bin/
if [ ! -e /var/lima/bin/limadaemon ]; then
  ln -s limadaemon.app/exec /var/lima/bin/limadaemon
fi
if [ ! -e /var/lima/bin/limasubmit ]; then
  ln -s limasubmit.app/exec /var/lima/bin/limasubmit
fi
cp templates/*.msg /var/lima/templates
