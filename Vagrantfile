# -*- mode: ruby -*-
# vi: set ft=ruby :

# All Vagrant configuration is done below. The "2" in Vagrant.configure
# configures the configuration version (we support older styles for
# backwards compatibility). Please don't change it unless you know what
# you're doing.

Vagrant.configure(2) do |config|

  # 64 bit Ubuntu Vagrant Box
  config.vm.box = "ubuntu/trusty64"

  ## Configure hostname and port forwarding
  config.vm.hostname = "ubuntu"
  config.ssh.forward_x11 = true
  config.vm.network "forwarded_port", guest: 8080, host: 8888

  vagrant_root = File.dirname(__FILE__)

  ## Provisioning
  config.vm.provision "shell", inline: <<-SHELL
     sudo apt-get update
     sudo apt-get -y upgrade
     sudo apt-get install -y emacs
     sudo apt-get install -y vim
     sudo apt-get install -y git
     sudo apt-get install -y python-dev
     curl https://bootstrap.pypa.io/get-pip.py > get-pip.py
     sudo python get-pip.py
     rm -f get-pip.py
     # Set correct permissions for bash scripts
     find /vagrant -name "*.sh" | xargs chmod -v 744
     # If the repository was pulled from Windows, convert line breaks to Unix-style
     sudo apt-get install -y dos2unix
     printf "Using dos2unix to convert files to Unix format if necessary..."
     find /vagrant -name "*" -type f | xargs dos2unix -q

     sudo apt-get install -y gdb
     sudo apt-get install -y valgrind

     #Set network buffer size
     sudo sysctl -w net.core.rmem_max=33554432
     sudo sysctl -w net.core.netdev_max_backlog=2000

     sudo apt-get install -y python-tk
     sudo pip install nbconvert
     sudo apt-get install -y mininet
     sudo apt-get install -y python-numpy
     sudo apt-get install -y python-matplotlib
     sudo apt-get install -y whois
     sudo pip install ipaddress
     sudo apt-get install -y apache2-utils
     sudo update-alternatives --install /usr/bin/python python /usr/bin/python3 10
     # Start in /vagrant instead of /home/vagrant
     if ! grep -Fxq "cd /vagrant" /home/vagrant/.bashrc
     then
      echo "cd /vagrant" >> /home/vagrant/.bashrc
     fi
  SHELL

  ## CPU & RAM
  config.vm.provider "virtualbox" do |vb|
    vb.customize ["modifyvm", :id, "--cpuexecutioncap", "100"]
    vb.memory = 2048
    vb.cpus = 1
  end

end
