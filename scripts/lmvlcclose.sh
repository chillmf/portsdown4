#!/bin/bash

# Called (as a forked thread) to stop VLC nicely
# Only works for a UDP stream otherwise longmynd's output fifo gets blocked

# The -i 1 parameter to put a second between commands is required to ensure reset at low SRs

nc -i 1 127.0.0.1 1111 >/dev/null 2>/dev/null << EOF
shutdown
quit
EOF

VLC_RUNNING=0

while [ $VLC_RUNNING -eq 0 ]
do
  sleep 0.5
  ps -el | grep usr/bin/vlc
  RESPONSE=$?

  if [ $RESPONSE -ne 0 ]; then  #  vlc has stopped
    VLC_RUNNING=1
  else                   # need to shutdown vlc
    sudo killall vlc
  fi
done

exit





