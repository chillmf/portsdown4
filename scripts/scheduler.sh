#!/usr/bin/env bash

# set -x

# This script is sourced (run) by startup.sh if the touchscreen interface
# is required.
# It enables the various touchscreen applications to call each other
# by checking their return code
# If any applications exits abnormally (with a 1 or a 0 exit code)
# it currently terminates or (for interactive sessions) goes back to a prompt

############ Set Environment Variables ###############

PATHSCRIPT=/home/pi/rpidatv/scripts
PATHRPI=/home/pi/rpidatv/bin
PATHCONFIGS="/home/pi/rpidatv/scripts/configs"  ## Path to config files
PCONFIGFILE="/home/pi/rpidatv/scripts/portsdown_config.txt"

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

########### Function to display message ####################

DisplayMsg() {
  # Delete any old update message image
  rm /home/pi/tmp/update.jpg >/dev/null 2>/dev/null

  # Create the update image in the tempfs folder
  convert -size 800x480 xc:black -font FreeSans -fill white \
    -gravity Center -pointsize 30 -annotate 0 "$1" \
    /home/pi/tmp/update.jpg

  # Display the update message on the desktop
  sudo fbi -T 1 -noverbose -a /home/pi/tmp/update.jpg >/dev/null 2>/dev/null
  (sleep 1; sudo killall -9 fbi >/dev/null 2>/dev/null) &  ## kill fbi once it has done its work
}

############ Function to check which BandViewer to start ####################

ChooseBandViewerSDR()
{
  # Set default start code to be Portsdown main menu
  BANDVIEW_START_CODE=129

  # Check for presence of Airspy   
  lsusb | grep -q 'Airspy'
  if [ $? == 0 ]; then   ## Present
    BANDVIEW_START_CODE=140
    return
  fi

  # Check for presence of Lime Mini
  lsusb | grep -q '0403:601f'
  if [ $? == 0 ]; then   ## Present
    BANDVIEW_START_CODE=136
    return
  fi

  # Check for presence of Lime USB
  lsusb | grep -q '1d50:6108' # Lime USB
  if [ $? == 0 ]; then   ## Present
    BANDVIEW_START_CODE=136
    return
  fi

  # Look up Pluto IP
  PLUTOIP=$(get_config_var plutoip $PCONFIGFILE)

  # Ping Pluto
  timeout 0.2 ping "$PLUTOIP" -c1 | head -n 5 | tail -n 1 | grep -o "1 received,"
  if [ $? == 0 ]; then   ## Present
    BANDVIEW_START_CODE=143
    return
  fi

  # Check for the presence of an RTL-SDR
  lsusb | grep -E -q "RTL|DVB"
  if [ $? == 0 ]; then   ## Present
    BANDVIEW_START_CODE=141
    return
  fi 
}

##################################################################

# set -x

# Return Codes
#
# <128  Exit leaving system running
# 128  Exit leaving system running
# 129  Exit from any app requesting restart of main rpidatvgui
# 130  Exit from rpidatvgui requesting start of siggen
# 132  Run Update Script for production load
# 133  Run Update Script for development load
# 134  Exit from rpidatvgui requesting start of the XY Display
# 135  Run the Langstone TRX V1
# 136  Exit from rpidatvgui requesting start of LimeSDR BandViewer
# 137  Exit from rpidatvgui requesting start of Power Meter
# 138  Exit from rpidatvgui requesting start of NF Meter
# 139  Exit from rpidatvgui requesting start of Sweeper
# 140  Exit from rpidatvgui requesting start of Airspy BandViewer
# 141  Exit from rpidatvgui requesting start of RTL-SDR BandViewer
# 142  Exit from rpidatvgui requesting start of DMM Display
# 143  Exit from rpidatvgui requesting start of Pluto BandViewer
# 144  Exit from rpidatvgui requesting start of SDRPlay BandViewer
# 145  Run the Langstone TRX V2 Lime
# 146  Run the Langstone TRX V2 Pluto
# 147  Exit from rpidatvgui requesting start of Noise Meter
# 149  SDRPlay BeaconRX Server no GUI
# 150  Run the Meteor Viewer
# 160  Shutdown from GUI
# 192  Reboot from GUI
# 193  Rotate 7 inch and reboot

MODE_STARTUP=$(get_config_var startup $PCONFIGFILE)

# Display the web not enabled caption
# It will get over-written if web is enabled
cp /home/pi/rpidatv/scripts/images/web_not_enabled.png /home/pi/tmp/screen.png

case "$MODE_STARTUP" in
  Display_boot)
    # Start the Portsdown Touchscreen
    GUI_RETURN_CODE=129
  ;;
  Langstone_boot)
    # Start the Langstone
    LANGSTONE_VERSION=$(get_config_var langstone $PCONFIGFILE)
    case "$LANGSTONE_VERSION" in
      v1pluto)
        GUI_RETURN_CODE=135
      ;;
      v2lime)
        GUI_RETURN_CODE=145
      ;;
      v2pluto)
        GUI_RETURN_CODE=146
      ;;
      none)
        GUI_RETURN_CODE=129
      ;;
    esac
  ;;
  Bandview_boot)
    # Start the Band Viewer
    ChooseBandViewerSDR
    GUI_RETURN_CODE=$BANDVIEW_START_CODE
  ;;
  Meteorbeacon_boot)
    # Start the Meteor Beacon RX Server
    GUI_RETURN_CODE=149
    DisplayMsg "Waiting 15 seconds for the\n\nLeo Bodnar GPS Ref to stabilise"
    /home/pi/rpidatv/scripts/single_screen_grab_for_web.sh &
    sleep 15  # Wait for the Leo Bodnar frequency reference to stabilise
  ;;
  Meteorview_boot)
    # Start the Meteor Viewer
    GUI_RETURN_CODE=150
    DisplayMsg "Waiting 15 seconds for the\n\nLeo Bodnar GPS Ref to stabilise"
    /home/pi/rpidatv/scripts/single_screen_grab_for_web.sh &
    sleep 15  # Wait for the Leo Bodnar frequency reference to stabilise
  ;;
  *)
    # Default to Portsdown
    GUI_RETURN_CODE=129
  ;;
esac

while [ "$GUI_RETURN_CODE" -gt 127 ] || [ "$GUI_RETURN_CODE" -eq 0 ];  do
  case "$GUI_RETURN_CODE" in
    0)
      /home/pi/rpidatv/bin/rpidatvgui > /tmp/PortsdownGUI.log 2>&1
      GUI_RETURN_CODE="$?"
    ;;
    128)
      # Jump out of the loop
      break
    ;;
    129)
      /home/pi/rpidatv/bin/rpidatvgui > /tmp/PortsdownGUI.log 2>&1
      GUI_RETURN_CODE="$?"
    ;;
    130)
      /home/pi/rpidatv/bin/siggen
      GUI_RETURN_CODE="$?"
    ;;
    132)
      cd /home/pi
      /home/pi/update.sh -p
    ;;
    133)
      cd /home/pi
      /home/pi/update.sh -d
    ;;
    134)
      /home/pi/rpidatv/bin/power_meter xy
      GUI_RETURN_CODE="$?"
    ;;
    135)
      cd /home/pi
      /home/pi/Langstone/run
      /home/pi/Langstone/stop
      PLUTOIP=$(get_config_var plutoip $PCONFIGFILE)
      ssh-keygen -f "/home/pi/.ssh/known_hosts" -R "$PLUTOIP" >/dev/null 2>/dev/null
      # ssh-keygen -f "/home/pi/.ssh/known_hosts" -R "pluto.local" >/dev/null 2>/dev/null
      timeout 2 sshpass -p analog ssh -o StrictHostKeyChecking=no root@"$PLUTOIP" 'PATH=/bin:/sbin:/usr/bin:/usr/sbin;reboot'
      sleep 2
      GUI_RETURN_CODE="129"
    ;;
    136)
      sleep 1
      /home/pi/rpidatv/bin/bandview
      GUI_RETURN_CODE="$?"
    ;;
    137)
      /home/pi/rpidatv/bin/power_meter
      GUI_RETURN_CODE="$?"
    ;;
    138)
      /home/pi/rpidatv/bin/nf_meter
      GUI_RETURN_CODE="$?"
    ;;
    139)
      sleep 1
      /home/pi/rpidatv/bin/sweeper
      GUI_RETURN_CODE="$?"
    ;;
    140)
      sleep 1
      /home/pi/rpidatv/bin/airspyview
      GUI_RETURN_CODE="$?"
    ;;
    141)
      sleep 1
      /home/pi/rpidatv/bin/rtlsdrview
      GUI_RETURN_CODE="$?"
    ;;
    142)
      sleep 1
      /home/pi/rpidatv/bin/dmm
      GUI_RETURN_CODE="$?"
    ;;
    143)
      sleep 1
      /home/pi/rpidatv/bin/plutoview
      GUI_RETURN_CODE="$?"
    ;;
    144)
      sleep 1
      /home/pi/rpidatv/bin/sdrplayview
      GUI_RETURN_CODE="$?"
    ;;
    145)                              # Langstone V2 Lime
      cd /home/pi
      /home/pi/Langstone/run_lime
      /home/pi/Langstone/stop_lime
      sleep 2
      GUI_RETURN_CODE="129"
    ;;
    146)                              # Langstone V2 Pluto
      cd /home/pi
      /home/pi/Langstone/run_pluto
      /home/pi/Langstone/stop_pluto
      PLUTOIP=$(get_config_var plutoip $PCONFIGFILE)
      ssh-keygen -f "/home/pi/.ssh/known_hosts" -R "$PLUTOIP" >/dev/null 2>/dev/null
      # ssh-keygen -f "/home/pi/.ssh/known_hosts" -R "pluto.local" >/dev/null 2>/dev/null
      timeout 2 sshpass -p analog ssh -o StrictHostKeyChecking=no root@"$PLUTOIP" 'PATH=/bin:/sbin:/usr/bin:/usr/sbin;reboot'
      sleep 2
      GUI_RETURN_CODE="129"
    ;;
    147)
      /home/pi/rpidatv/bin/noise_meter
      GUI_RETURN_CODE="$?"
    ;;

    149)                              # SDRPlay Beacon RX Server no GUI
      RPISTATE="Not_Ready"
      while [[ "$RPISTATE" == "Not_Ready" ]]
      do
        DisplayMsg "Restarting SDRPlay Service\n\nThis may take up to 90 seconds"
        /home/pi/rpidatv/scripts/single_screen_grab_for_web.sh &

        sudo systemctl restart sdrplay

        DisplayMsg " "                      # Display Blank screen
        /home/pi/rpidatv/scripts/single_screen_grab_for_web.sh &

        lsusb | grep -q '1df7:'             # check for SDRPlay
        if [ $? != 0 ]; then                # Not detected
          DisplayMsg "Unable to detect SDRPlay\n\nResetting the USB Bus"
          /home/pi/rpidatv/scripts/single_screen_grab_for_web.sh &
          sudo uhubctl -R -a 2              # So reset USB bus
          sleep 1

          lsusb | grep -q '1df7:'           # Check again
          if [ $? != 0 ]; then              # Not detected   
            DisplayMsg "Unable to detect SDRPlay\n\nResetting the USB Bus"
            /home/pi/rpidatv/scripts/single_screen_grab_for_web.sh &
            sudo uhubctl -R -a 2            # Try reset USB bus again
            sleep 1

            lsusb | grep -q '1df7:'         # Has that worked?         
            if [ $? != 0 ]; then            # No
              DisplayMsg "Still Unable to detect SDRPlay\n\n\nCheck connections"
              /home/pi/rpidatv/scripts/single_screen_grab_for_web.sh &
              sleep 2
              DisplayMsg " "                # Display Blank screen
            else
              RPISTATE="Ready"
            fi
          else
            RPISTATE="Ready"
          fi
        else
          RPISTATE="Ready"
        fi
      done
      
      DisplayMsg "Starting the beacon RX server\n\nThis caption stays on while it is running\n"
      /home/pi/rpidatv/scripts/single_screen_grab_for_web.sh &

      /home/pi/rpidatv/bin/beacon
      GUI_RETURN_CODE="$?"

      if [ $GUI_RETURN_CODE != 129 ] && [ $GUI_RETURN_CODE != 160 ]; then   # Not Portsdown and not shutdown
        DisplayMsg "Beacon RX server did not start properly\nTrying again\n"
        /home/pi/rpidatv/scripts/single_screen_grab_for_web.sh &

        GUI_RETURN_CODE=149                         # So try to restart beacon        
      fi
    ;;

    150)                              # SDRPlay Meteor Viewer
      RPISTATE="Not_Ready"
      while [[ "$RPISTATE" == "Not_Ready" ]]
      do
        DisplayMsg "Restarting SDRPlay Service\n\nThis may take up to 90 seconds"
        /home/pi/rpidatv/scripts/single_screen_grab_for_web.sh &

        sudo systemctl restart sdrplay

        DisplayMsg " "                      # Display Blank screen
        /home/pi/rpidatv/scripts/single_screen_grab_for_web.sh &

        lsusb | grep -q '1df7:'             # check for SDRPlay
        if [ $? != 0 ]; then                # Not detected
          DisplayMsg "Unable to detect SDRPlay\n\nResetting the USB Bus"
          /home/pi/rpidatv/scripts/single_screen_grab_for_web.sh &
          sudo uhubctl -R -a 2              # So reset USB bus
          sleep 1

          lsusb | grep -q '1df7:'           # Check again
          if [ $? != 0 ]; then              # Not detected
            DisplayMsg "Unable to detect SDRPlay\n\nResetting the USB Bus"
            /home/pi/rpidatv/scripts/single_screen_grab_for_web.sh &
            sudo uhubctl -R -a 2            # Try reset USB bus again
            sleep 1

            lsusb | grep -q '1df7:'         # Has that worked?
            if [ $? != 0 ]; then            # No
              DisplayMsg "Still Unable to detect SDRPlay\n\n\nCheck connections"
              /home/pi/rpidatv/scripts/single_screen_grab_for_web.sh &
              sleep 2
              DisplayMsg " "                # Display Blank screen
              /home/pi/rpidatv/scripts/single_screen_grab_for_web.sh &
            else
              RPISTATE="Ready"     
            fi
          else
            RPISTATE="Ready"
          fi
        else
          RPISTATE="Ready"
        fi
      done

      /home/pi/rpidatv/bin/meteorview
      GUI_RETURN_CODE="$?"

      if [ $GUI_RETURN_CODE != 129 ] && [ $GUI_RETURN_CODE != 160 ]; then     # Not Portsdown and not shutdown
        GUI_RETURN_CODE=150                         # So restart meteorview        
      fi
    ;;
    160)
      sleep 1
      sudo swapoff -a
      sudo shutdown now
      break
    ;;
    192)
      PLUTOIP=$(get_config_var plutoip $PCONFIGFILE)
      ssh-keygen -f "/home/pi/.ssh/known_hosts" -R "$PLUTOIP" >/dev/null 2>/dev/null
      # ssh-keygen -f "/home/pi/.ssh/known_hosts" -R "pluto.local" >/dev/null 2>/dev/null
      timeout 2 sshpass -p analog ssh -o StrictHostKeyChecking=no root@"$PLUTOIP" 'PATH=/bin:/sbin:/usr/bin:/usr/sbin;reboot'
      sleep 2
      sudo swapoff -a
      sudo reboot now
      break
    ;;
    193)
      # Rotate 7 inch display
      # Three scenarios:
      #  (1) No text in /boot/config.txt, so append it
      #  (2) Rotate text is in /boot/config.txt, so comment it out
      #  (3) Commented text in /boot/config.txt, so uncomment it

      # Test for Scenario 1
      if ! grep -q 'lcd_rotate=2' /boot/config.txt; then
        # No relevant text, so append it (Scenario 1)
        sudo sh -c 'echo "\n## Rotate 7 inch Display\nlcd_rotate=2\n" >> /boot/config.txt'
      else
        # Text exists, so see if it is commented or not
        TEST_STRING="#lcd_rotate=2"
        if ! grep -q -F $TEST_STRING /boot/config.txt; then
          # Scenario 2
          sudo sed -i '/lcd_rotate=2/c\#lcd_rotate=2' /boot/config.txt >/dev/null 2>/dev/null
        else
          # Scenario 3
          sudo sed -i '/#lcd_rotate=2/c\lcd_rotate=2' /boot/config.txt  >/dev/null 2>/dev/null
        fi
      fi

      # Make sure that scheduler reboots and does not repeat 7 inch rotation
      GUI_RETURN_CODE=192
      sleep 1
      sudo swapoff -a
      sudo reboot now
      break
    ;;
    *)
      /home/pi/rpidatv/bin/rpidatvgui
      GUI_RETURN_CODE="$?"
    ;;
  esac
done


