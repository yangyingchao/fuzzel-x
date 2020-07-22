pkgname=fuzzel
pkgver=1.3.0
pkgrel=1
pkgdesc="Simplistic application launcher for wayland"
arch=('x86_64' 'aarch64')
url=https://codeberg.org/dnkl/fuzzel
license=(mit)
makedepends=('meson' 'ninja' 'scdoc' 'tllist>=1.0.1')
depends=('libxkbcommon' 'wayland' 'cairo' 'librsvg' 'fcft>=2.0.0')
source=()

pkgver() {
  cd ../.git &> /dev/null && git describe --tags --long | sed 's/^v//;s/\([^-]*-g\)/r\1/;s/-/./g' ||
      head -3 ../meson.build | grep version | cut -d "'" -f 2
}

build() {
  meson --prefix=/usr --buildtype=release --wrap-mode=nofallback -Db_lto=true ..
  ninja
}

package() {
  DESTDIR="${pkgdir}/" ninja install
}
