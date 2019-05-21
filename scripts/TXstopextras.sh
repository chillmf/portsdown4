#! /bin/bash

# This script is called when the transmitter is stopped.  The primary role is to stop the Jetson transmitting
# But it can be added to for any purpose

############ Set Environment Variables ###############

PATHSCRIPT=/home/pi/rpidatv/scripts
PATHRPI=/home/pi/rpidatv/bin
PCONFIGFILE="/home/pi/rpidatv/scripts/portsdown_config.txt"
PATHCONFIGS="/home/pi/rpidatv/scripts/configs"  ## Path to config files

############ Function to Read from Config File ###############

get_config_var() {
lua - "$1" "$2" <<EOF
local key=assert(arg[1])
local fn=assert(arg[2])
local file=assert(io.open(fn))
for line in file:lines() do
local val = line:match("^#?%s*"..key.."=(.*)$")
if (val ~= nil) then
print(val)
break
end
end
EOF
}

MODE_OUTPUT=$(get_config_var modeoutput $PCONFIGFILE)

# Stop Jetson transmitting if required

case "$MODE_OUTPUT" in
"JLIME" | "JEXPRESS")

sshpass -p "jetson" ssh nano@192.168.2.113 'bash -s' <<'ENDSSH' 
killall gst-launch-1.0
killall ffmpeg
killall limesdr_dvb
ENDSSH
;;
esac

exit