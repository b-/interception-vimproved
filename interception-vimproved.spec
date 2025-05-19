Name:           interception-vimproved
Version:        0.1.0
Release:        %autorelease
Summary:        Interception plugin for vimproved input
License:        GPL-3.0-or-later
URL:            https://github.com/maricn/interception-vimproved
#Source:        https://github.com/maricn/interception-vimproved/archive/refs/heads/master.tar.gz
Source:        https://github.com/0Karakurt0/interception-vimproved/archive/refs/heads/master.tar.gz
BuildRequires:  gcc-c++
BuildRequires:  meson
BuildRequires:  yaml-cpp-devel


%description
Humble, performant key remapping C++ code that should work on Linux for any input device that emits keys.


%prep
%autosetup -n caps2esc-v%{version}


%build
make


%install
make install


%check


%files
%license
%doc README.md
%{_bindir}/interception-vimproved


%changelog
