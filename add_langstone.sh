#!/bin/bash

# Install script to add Langstone to Portsdown 4

# First, update packages to the latest standard

sudo dpkg --configure -a     # Make sure that all the packages are properly configured
sudo apt-get clean           # Clean up the old archived packages
sudo apt-get update          # Update the package list
sudo apt-get -y dist-upgrade # Upgrade all the installed packages to their latest version

# Langstone packages to install
sudo apt-get -y install libxml2 libxml2-dev bison flex libcdk5-dev
sudo apt-get -y install libaio-dev libserialport-dev libxml2-dev libavahi-client-dev 
sudo apt-get -y install gr-iio
sudo apt-get -y install gnuradio
sudo apt-get -y install raspi-gpio

# Install WiringPi:
cd /tmp
wget https://project-downloads.drogon.net/wiringpi-latest.deb
sudo dpkg -i wiringpi-latest.deb

echo
# Install libiio and dependencies if required (may already be there for Pluto SigGen)
if [ ! -d  /home/pi/libiio ]; then
  echo "Installing libiio"
  echo
  cd /home/pi
  git clone https://github.com/analogdevicesinc/libiio.git
  cd libiio
  cmake ./
  make all
  sudo make install
  cd /home/pi
else
  echo "Found libiio installed"
  echo
fi

# Enable i2c support
sudo raspi-config nonint do_i2c 0

echo "#################################"
echo "##     Installing Langstone    ##"
echo "#################################"

cd /home/pi
git clone https://github.com/g4eml/Langstone.git
cd Langstone
chmod +x build
chmod +x run
chmod +x stop
chmod +x update
./build

echo "alias stop='/home/pi/Langstone/stop'"  >> /home/pi/.bash_aliases
echo "alias run='/home/pi/Langstone/run'"  >> /home/pi/.bash_aliases


echo "#################################"
echo "##       Reboot and Start      ##"
echo "#################################"

#Reboot and start
sudo reboot now


