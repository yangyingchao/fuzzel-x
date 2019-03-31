pkgname=f00sel
pkgver=0.9.0.r94.gadb1761
pkgrel=1
pkgdesc="Simplistic application launcher for wayland"
arch=('x86_64')
url=https://gitlab.com/dnkl/f00sel
license=(mit)
makedepends=('scdoc')
depends=(
  'libxkbcommon'
  'wayland' 'wlroots'
  'freetype2' 'fontconfig' 'cairo')
source=()

pkgver() {
  git describe --tags --long | sed 's/^v//;s/\([^-]*-g\)/r\1/;s/-/./g'
}

build() {
  cmake -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX=/usr ../
  ninja
}

package() {
  DESTDIR="${pkgdir}/" ninja install
}
