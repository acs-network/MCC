#!/bin/bash

. /etc/os-release

centos_packages=(
  gmp-devel
  numactl-devel
  libnet-devel
  libpcap-devel
  boost-devel
  bc 
  centos-release-scl
  automake
  pciutils
  wget
  ntp
  devtoolset-8
)

yum install -y "${centos_packets}"

yum install -y gmp-devel numactl-devel libnet-devel libpcap-devel boost-devel bc centos-release-scl automake pciutils wget ntp devtoolset-8

if [ "$ID" = "centos" ] ; then
  # install libnids
#	yum install gcc-c++ -y
#	yum install -y libgnomeui-devel
#	cd src/stream_gen/libnids-1.24
#	./configure && make && make install && make clean
#	cd ../../../

  # Download dependencies
  mkdir -p deps
  cd downloads
  
  # update curl
  yum update -y nss curl libcurl
  
	# install cmake3
#  wget https://cmake.org/files/v3.13/cmake-3.13.5.tar.gz
  cp * ../deps/
  cd ../deps/

  unzip dev-8.zip
  cd mcc-rpm
  yum install -y zlib-devel
  rpm -ivh devtoolset-8*

  cd ../

  tar -zxvf cmake-3.13.5.tar.gz
  cd cmake-3.13.5/
  ./bootstrap && gmake && gmake install
  ln -s /usr/local/bin/cmake /usr/local/bin/cmake3
  

  # yum install -y centos-release-scl epel-release
  # rpm --import https://www.elrepo.org/RPM-GPG-KEY-elrepo.org
	# yum install -y https://www.elrepo.org/elrepo-release-7.0-3.el7.elrepo.noarch.rpm

  # upgrade kernel in elrepo
	# yum install -y --enablerepo elrepo-kernel kernel-lt kernel-lt-devel kernel-lt-headers
  
	# this will replace existing c++ compiler in order to compile protobuf and fmt
  ln -s /opt/rh/devtoolset-8/root/bin/c++ /usr/bin/c++

  cd ../
  
  # install protobuf
  #wget https://github.com/protocolbuffers/protobuf/releases/download/v3.5.0/protobuf-cpp-3.5.0.tar.gz 
  tar -zxvf protobuf-cpp-3.5.0.tar.gz
  cd protobuf-3.5.0
  ./configure
  make
  make install 
  cd ../
  echo /usr/local/lib >> /etc/ld.so.conf
  ldconfig

  # install fmt 
  #wget https://github.com/fmtlib/fmt/archive/5.3.0.tar.gz
  #wget https://github.com/fmtlib/fmt/archive/6.1.0.tar.gz
  tar xvf fmt-6.1.0.tar.gz
  # if cmake is installed with source code(add Symbolic link cmake3 to cmake)
  cd fmt-6.1.0 && cmake3 ./ && make && make install && cd ../../

  # warning: this will replace current linux kernel
	# grub2-set-default 0
  # grub2-mkconfig -o /boot/efi/EFI/centos/grub.cfg
  # (or) grub2-mkconfig -o /boot/grub2/grub.cfg
fi




