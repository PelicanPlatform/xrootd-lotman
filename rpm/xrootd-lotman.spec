Name:           xrootd-lotman
Version:        0.0.2
Release:        1%{?dist}
Summary:        A purge plugin for XRootD that uses Lotman's tracking for informed cache disk management

License:        Apache-2.0
URL:            https://github.com/PelicanPlatform/xrootd-lotman
Source0:        https://github.com/PelicanPlatform/xrootd-lotman/archive/v%{version}/xrootd-lotman-%{version}.tar.gz

BuildRequires:  cmake
BuildRequires:  gcc-c++
BuildRequires:  make
BuildRequires:  lotman
BuildRequires:  nlohmann-json-devel
BuildRequires:  alja-xrootd-server-devel

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
%license LICENSE
%doc README.md
%{_libdir}/libXrdPurgeLotMan.so*
%{_includedir}/XrdPurgeLotMan.hh

%changelog
* Thu Sep 19 2024 Justin Hiemstra <jhiemstra@wisc.edu> - 0.0.2-1
- Fixes to packaging and better CMake error messages

* Tue Aug 27 2024 Justin Hiemstra <jhiemstra@wisc.edu> - 0.0.1-1
- Initial package
