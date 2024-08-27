Name:           xrootd-lotman
Version:        0.0.1
Release:        1%{?dist}
Summary:        A purge plugin for XRootD that uses Lotman's tracking for informed cache disk management

License:        Apache-2.0
URL:            https://github.com/PelicanPlatform/xrootd-lotman
Source0:        https://github.com/PelicanPlatform/xrootd-lotman/archive/v0.0.1.tar.gz

BuildRequires:  cmake
BuildRequires:  g++
BuildRequires:  make
BuildRequires:  lotman
BuildRequires:  xrootd

%description
This package provides a purge plugin for XRootD that uses Lotman's tracking for informed purges.

%prep
%setup -q -n xrootd-lotman-%version

%build

%cmake
%cmake_build

%install
%cmake_install

%files
%{_libdir}/libXrdPurgeLotMan.so*
%{_includedir}/XrdPurgeLotMan.hh

%changelog
* Tue Aug 27 2024 Justin Hiemstra <jhiemstra@wisc.edu> - 0.0.1-1
- Initial package
