pkgname=fuzzel
pkgver=1.2.0
pkgrel=1
pkgdesc="Simplistic application launcher for wayland"
arch=('x86_64')
url=https://codeberg.org/dnkl/fuzzel
license=(mit)
makedepends=('meson' 'ninja' 'scdoc')
depends=(
  'libxkbcommon'
  'wayland' 'wlroots'
  'freetype2' 'fontconfig' 'cairo' 'librsvg')
source=()

pkgver() {
  cd ../.git &> /dev/null && git describe --tags --long | sed 's/^v//;s/\([^-]*-g\)/r\1/;s/-/./g' ||
      head -3 ../meson.build | grep version | cut -d "'" -f 2
}

build() {
  meson --prefix=/usr --buildtype=release -Db_lto=true ..
  ninja
}

package() {
  DESTDIR="${pkgdir}/" ninja install
}
